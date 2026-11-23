// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

use nix::fcntl::{open, OFlag};
use nix::sys::stat;
use nix::unistd;
use std::io::{self};
use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

/// The directory the files of the tests go in: `TEST_TMPDIR` under Bazel, which removes it also
/// after a test it killed, the temp directory of the system elsewhere.
fn temp_root() -> PathBuf {
    std::env::var_os("TEST_TMPDIR").map_or_else(std::env::temp_dir, PathBuf::from)
}

/// An MQTT topic prefix of its own for a test, for the tests that share a broker: those of a
/// binary, which run in parallel, and those of other binaries.
pub fn unique_mqtt_prefix() -> String {
    // Random, as process and thread ids repeat across binaries in PID namespaces of their own.
    let mut random = [0u8; 8];
    getrandom::getrandom(&mut random).expect("Failed to get random bytes");
    format!("everest_test_{:016x}/", u64::from_ne_bytes(random))
}

/// Handle to a running EVerest manager process.
///
/// The manager is spawned as a subprocess using `{prefix}/bin/manager`. When
/// dropped, the manager process is killed if it is still running.
pub struct Manager {
    child: Arc<Mutex<Child>>,
    stopping: Arc<AtomicBool>,
    watcher: Option<thread::JoinHandle<()>>,
}

impl Manager {
    /// Start the EVerest manager as a subprocess and wait until it is ready.
    ///
    /// * `prefix` — the EVerest sysroot (passed as `--prefix`)
    /// * `config` — path to the config file (passed as `--config`)
    /// * `standalone` — module IDs that the manager should not spawn
    ///   (passed as `--standalone`). If non-empty, this function blocks until
    ///   the manager reports that all managed modules are started and it is
    ///   waiting for the standalone modules.
    /// * `mqtt_everest_prefix` — optional MQTT topic prefix override. When set,
    ///   passed as `--mqtt_everest_prefix` to the manager, allowing multiple
    ///   test instances to run in parallel without topic collisions.
    pub fn start(
        prefix: &Path,
        config: &Path,
        standalone: &[&str],
        mqtt_everest_prefix: Option<&str>,
    ) -> io::Result<Self> {
        let binary = Self::manager_binary(prefix);

        let mut cmd = Command::new(&binary);
        cmd.arg("--prefix").arg(prefix);
        cmd.arg("--config").arg(config);

        // No --db: each manager process then uses its own in-memory configuration database
        // seeded from the config file on every start, so parallel test instances cannot race
        // each other on a shared database file and no leftover state survives between runs.

        if let Some(mqtt_prefix) = mqtt_everest_prefix {
            cmd.arg("--mqtt_everest_prefix").arg(mqtt_prefix);
        }

        // The status fifo, in a directory of its own: its name is unique, whatever the MQTT
        // prefix. The manager opens the fifo when it starts; the fifo goes with its directory
        // once the manager is up.
        let status = if standalone.is_empty() {
            None
        } else {
            let dir = tempfile::Builder::new()
                .prefix("everest_status_")
                .tempdir_in(temp_root())?;
            let fifo_path = dir.path().join("status.fifo");
            unistd::mkfifo(&fifo_path, stat::Mode::S_IRWXU)?;

            let fifo = open(
                &fifo_path,
                OFlag::O_RDONLY | OFlag::O_NONBLOCK,
                stat::Mode::empty(),
            )?;
            cmd.arg("--status-fifo").arg(&fifo_path);
            cmd.arg("--standalone");
            for module_id in standalone {
                cmd.arg(module_id);
            }
            Some((dir, fifo))
        };

        let child = Arc::new(Mutex::new(cmd.spawn().map_err(|e| {
            io::Error::new(
                e.kind(),
                format!("Failed to start manager at {}: {e}", binary.display()),
            )
        })?));
        let stopping = Arc::new(AtomicBool::new(false));

        let watcher = {
            let child = child.clone();
            let stopping = stopping.clone();
            Some(std::thread::spawn(move || loop {
                let status = child.lock().unwrap().try_wait().unwrap();
                match status {
                    None => thread::sleep(Duration::from_millis(100)),
                    Some(status) => {
                        if !stopping.load(Ordering::Relaxed) {
                            panic!("Manager process exited unexpectedly with status: {status}");
                        }
                        return;
                    }
                }
            }))
        };

        // Wait for the manager to signal readiness via the status fifo.
        if let Some((_status_dir, fd)) = status {
            let mut buf = [0u8; 256];
            let mut accumulated = String::new();
            loop {
                match unistd::read(&fd, &mut buf) {
                    Ok(n) => {
                        accumulated.push_str(&String::from_utf8_lossy(&buf[..n]));
                        if accumulated.contains("WAITING_FOR_STANDALONE_MODULES")
                            || accumulated.contains("ALL_MODULES_STARTED")
                        {
                            break;
                        }
                    }
                    Err(nix::errno::Errno::EAGAIN) => {
                        thread::sleep(Duration::from_millis(50));
                    }
                    Err(e) => return Err(io::Error::from_raw_os_error(e as i32)),
                }

                // The watcher thread crashed - we won't see anything in our
                // fifo.
                if watcher.as_ref().unwrap().is_finished() {
                    break;
                }
            }
            let _ = unistd::close(fd);
        }

        Ok(Self {
            child,
            stopping,
            watcher,
        })
    }

    /// Returns the path to the manager binary for a given prefix.
    fn manager_binary(prefix: &Path) -> PathBuf {
        prefix.join("bin").join("manager")
    }
}

impl Drop for Manager {
    fn drop(&mut self) {
        self.stopping.store(true, Ordering::Relaxed);
        let _ = self.child.lock().unwrap().kill();
        if let Err(panic) = self.watcher.take().expect("always there").join() {
            std::panic::resume_unwind(panic);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::os::unix::fs::PermissionsExt;
    use tempfile::TempDir;

    const ARBITRARY_CONFIG: &str = "etc/everest/config.yaml";
    const ARBITRARY_MODULE: &str = "arbitrary_module";
    const ARBITRARY_MQTT_PREFIX: &str = "arbitrary_prefix/";

    // A manager that only tells its status fifo that it waits for the standalone modules, then
    // sleeps until killed. It notes the fifo in `status_fifos` of its prefix.
    const FAKE_MANAGER: &str = r#"#!/bin/sh
while [ $# -gt 0 ]; do
    if [ "$1" = --status-fifo ]; then fifo=$2; fi
    shift
done
echo "$fifo" >> "${0%/bin/manager}/status_fifos"
echo WAITING_FOR_STANDALONE_MODULES > "$fifo"
exec sleep 60
"#;

    // An EVerest prefix with the fake manager.
    fn prefix_with_fake_manager() -> TempDir {
        let prefix = tempfile::tempdir().unwrap();
        let manager = Manager::manager_binary(prefix.path());
        fs::create_dir_all(manager.parent().unwrap()).unwrap();
        fs::write(&manager, FAKE_MANAGER).unwrap();
        fs::set_permissions(&manager, fs::Permissions::from_mode(0o755)).unwrap();
        prefix
    }

    fn start_fake_manager(prefix: &TempDir) -> io::Result<Manager> {
        Manager::start(
            prefix.path(),
            &prefix.path().join(ARBITRARY_CONFIG),
            &[ARBITRARY_MODULE],
            Some(ARBITRARY_MQTT_PREFIX),
        )
    }

    #[test]
    fn start_leaves_no_status_fifo_behind() {
        // Two starts, one after the other, with the same MQTT prefix: a status fifo left by the
        // first would be in the way of the second.
        let prefix = prefix_with_fake_manager();
        for _ in 0..2 {
            start_fake_manager(&prefix).unwrap();
        }

        let fifos = fs::read_to_string(prefix.path().join("status_fifos")).unwrap();
        let fifos: Vec<&str> = fifos.lines().collect();
        assert_eq!(fifos.len(), 2, "{fifos:?}");
        // Each start has a fifo of its own, so starts at the same time do not collide either.
        assert_ne!(fifos[0], fifos[1]);
        for fifo in fifos {
            assert!(!Path::new(fifo).exists(), "{fifo} is left");
        }
    }
}
