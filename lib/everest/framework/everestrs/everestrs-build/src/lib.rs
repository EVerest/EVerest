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
        let (out, inputs) = codegen::emit_with_inputs(self.manifest_path, self.everest_root)?;

        // Cargo replaces its default "rerun if anything in the package changed"
        // with whatever the build script declares, so every YAML the generator
        // read has to be declared here or an edit to one is invisible. Doing it
        // here rather than in each build.rs means a new module gets this for
        // free instead of it being one more thing to remember.
        if std::env::var_os("CARGO").is_some() {
            println!("cargo:rerun-if-env-changed=EVEREST_CORE_ROOT");
            println!("cargo:rerun-if-changed={}", manifest_path.display());
            for input in &inputs {
                println!("cargo:rerun-if-changed={}", input.display());
            }
        }

        let mut f = std::fs::File::create(&path).context("Could not generate the output file.")?;
        f.write_all(out.as_bytes())?;

        if let Err(_) = Command::new("rustfmt").args(path.to_str()).output() {
            println!("Failed to format code");
        }
        Ok(())
    }
}
