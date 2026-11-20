// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

use nix::fcntl::{open, OFlag};
use nix::sys::stat;
use nix::unistd;
use std::fs;
use std::io::{self};
use std::path::{Path, PathBuf};
use std::process::{Child, Command};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;
use tempfile::TempDir;

/// The directory the manager looks for the user-config in, next to the config.
const USER_CONFIG_DIR: &str = "user-config";

/// The directory of a test with a `setup` (`#[everestrs::test(setup = ...)]`). It holds a copy
/// of the config of the test, which the manager loads, and the user-config the manager merges
/// over it: a copy of the one of the EVerest prefix, if there is one. The setup writes into
/// the directory. Dropping the `TestSetup` removes the directory.
pub struct TestSetup {
    prefix: PathBuf,
    test_dir: TempDir,
    config: PathBuf,
}

impl TestSetup {
    /// Creates the directory of a test with a copy of `config`, and of its user-config if the
    /// EVerest `prefix` has one. Under Bazel, the directory is in `TEST_TMPDIR`, which Bazel
    /// also removes after a test it killed.
    pub fn new(prefix: PathBuf, config: &Path) -> io::Result<Self> {
        let name = config.file_name().ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("The config {config:?} is not a file"),
            )
        })?;
        let root = std::env::var_os("TEST_TMPDIR").map_or_else(std::env::temp_dir, PathBuf::from);
        let test_dir = tempfile::Builder::new()
            .prefix("everest_test_")
            .tempdir_in(root)?;
        let this = Self {
            prefix,
            config: test_dir.path().join(name),
            test_dir,
        };
        copy_writable(config, &this.config)?;
        fs::create_dir(this.test_dir().join(USER_CONFIG_DIR))?;
        let user_config = config.with_file_name(USER_CONFIG_DIR).join(name);
        if user_config.exists() {
            copy_writable(&user_config, &this.user_config())?;
        }
        Ok(this)
    }

    /// The EVerest prefix. Read-only, shared by all tests of the binary.
    pub fn prefix(&self) -> &Path {
        &self.prefix
    }

    /// The directory of the test, writable.
    pub fn test_dir(&self) -> &Path {
        self.test_dir.path()
    }

    /// The config the manager loads, in the directory of the test.
    pub fn config(&self) -> &Path {
        &self.config
    }

    /// The user-config the manager merges over the config (a JSON merge patch). It exists if
    /// the EVerest prefix has one; a setup extending it keeps its entries.
    pub fn user_config(&self) -> PathBuf {
        self.config
            .with_file_name(USER_CONFIG_DIR)
            .join(self.config.file_name().expect("A file, see new"))
    }
}

/// Copies `from` to `to`, writable whatever the mode of `from`: the runfiles of Bazel are
/// read-only.
fn copy_writable(from: &Path, to: &Path) -> io::Result<()> {
    fs::write(to, fs::read(from)?)
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

        let mut fifo = None;

        if !standalone.is_empty() {
            let fifo_name = match mqtt_everest_prefix {
                Some(p) => format!("status-{}.fifo", p.replace('/', "_")),
                None => format!("status-{}.fifo", std::process::id()),
            };
            let fifo_path = std::env::temp_dir().join(fifo_name);
            unistd::mkfifo(&fifo_path, stat::Mode::S_IRWXU)?;

            fifo = Some(open(
                &fifo_path,
                OFlag::O_RDONLY | OFlag::O_NONBLOCK,
                stat::Mode::empty(),
            )?);
            cmd.arg("--status-fifo").arg(&fifo_path);
            cmd.arg("--standalone");
            for module_id in standalone {
                cmd.arg(module_id);
            }
        }

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
        if let Some(fd) = fifo {
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

    const CONFIG: &str = "config.yaml";
    const CONFIG_CONTENT: &str = "arbitrary config";
    const USER_CONFIG_CONTENT: &str = "arbitrary user-config";

    // An EVerest prefix with a read-only config, as in the runfiles of Bazel, and a
    // user-config next to it if one is given.
    fn prefix_with(user_config: Option<&str>) -> TempDir {
        let prefix = tempfile::tempdir().unwrap();
        let config_dir = prefix.path().join("etc/everest");
        fs::create_dir_all(config_dir.join(USER_CONFIG_DIR)).unwrap();
        write_read_only(&config_dir.join(CONFIG), CONFIG_CONTENT);
        if let Some(content) = user_config {
            write_read_only(&config_dir.join(USER_CONFIG_DIR).join(CONFIG), content);
        }
        prefix
    }

    fn write_read_only(path: &Path, content: &str) {
        fs::write(path, content).unwrap();
        let mut permissions = fs::metadata(path).unwrap().permissions();
        permissions.set_readonly(true);
        fs::set_permissions(path, permissions).unwrap();
    }

    fn setup_in(prefix: &TempDir) -> TestSetup {
        let config = prefix.path().join("etc/everest").join(CONFIG);
        TestSetup::new(prefix.path().to_path_buf(), &config).unwrap()
    }

    #[test]
    fn new_copies_the_config_and_its_user_config_writable() {
        let prefix = prefix_with(Some(USER_CONFIG_CONTENT));

        let setup = setup_in(&prefix);

        // The manager looks for the user-config next to the config, under its name.
        assert_eq!(setup.config(), setup.test_dir().join(CONFIG));
        assert_eq!(fs::read_to_string(setup.config()).unwrap(), CONFIG_CONTENT);
        assert_eq!(
            setup.user_config(),
            setup.test_dir().join(USER_CONFIG_DIR).join(CONFIG)
        );
        assert_eq!(
            fs::read_to_string(setup.user_config()).unwrap(),
            USER_CONFIG_CONTENT
        );
        let permissions = fs::metadata(setup.user_config()).unwrap().permissions();
        assert!(!permissions.readonly());
    }

    #[test]
    fn new_without_user_config_leaves_room_for_one() {
        let prefix = prefix_with(None);

        let setup = setup_in(&prefix);

        assert!(!setup.user_config().exists());
        assert!(setup.user_config().parent().unwrap().is_dir());
    }
}
