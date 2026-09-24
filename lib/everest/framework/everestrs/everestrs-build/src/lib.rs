pub mod codegen;
pub mod manifest_resolver;
pub mod schema;

use anyhow::{Context, Result};
use std::io::Write;
use std::path::PathBuf;
use std::process::Command;

pub use manifest_resolver::build_test_manifest;

#[derive(Debug, Default)]
pub struct Builder {
    everest_root: Vec<PathBuf>,
    // TODO(hrapp): This is almost always the same anyways.
    manifest_path: PathBuf,
    out_dir: Option<PathBuf>,
}

impl Builder {
    pub fn new(manifest_path: impl Into<PathBuf>, everest_root: Vec<impl Into<PathBuf>>) -> Self {
        Self {
            everest_root: everest_root
                .into_iter()
                .map(|element| element.into())
                .collect::<Vec<_>>(),
            manifest_path: manifest_path.into(),
            ..Builder::default()
        }
    }

    pub fn out_dir(mut self, path: impl Into<PathBuf>) -> Self {
        self.out_dir = Some(path.into());
        self
    }

    pub fn generate(self) -> Result<()> {
        let path = self
            .out_dir
            .unwrap_or_else(|| PathBuf::from(std::env::var("OUT_DIR").unwrap()))
            .join("generated.rs");

        let manifest_path = self.manifest_path.clone();
        let emitted = codegen::emit_with_inputs(self.manifest_path, self.everest_root)?;

        // Cargo replaces its default "rerun if anything in the package changed"
        // with whatever the build script declares, so every YAML the generator
        // read has to be declared here or an edit to one is invisible. Doing it
        // here rather than in each build.rs means a new module gets this for
        // free instead of it being one more thing to remember.
        if std::env::var_os("CARGO").is_some() {
            println!("cargo:rerun-if-env-changed=EVEREST_CORE_ROOT");
            println!("cargo:rerun-if-changed={}", manifest_path.display());
            for input in &emitted.dependencies {
                println!("cargo:rerun-if-changed={}", input.display());
            }
        }

        // Cargo emits no runpath of its own and install(PROGRAMS) does no
        // RPATH rewriting. This has to be printed from the module's own build
        // script: a link arg printed by a dependency's build script, everestrs'
        // included, applies only to that package's targets. Config rustflags
        // are no substitute: cargo drops them whenever RUSTFLAGS is set.
        if let Some(rpath) = std::env::var_os("EVEREST_RS_INSTALL_RPATH") {
            println!("cargo:rerun-if-env-changed=EVEREST_RS_INSTALL_RPATH");
            println!(
                "cargo:rustc-link-arg-bins=-Wl,-rpath,$ORIGIN/{}",
                PathBuf::from(rpath).display()
            );
        }

        let mut f = std::fs::File::create(&path).context("Could not generate the output file.")?;
        f.write_all(emitted.generated.as_bytes())?;

        if let Err(_) = Command::new("rustfmt").args(path.to_str()).output() {
            println!("Failed to format code");
        }
        Ok(())
    }
}
