use super::types::{BooleanOptions, IntegerOptions, NumberOptions, StringOptions};
use serde::Deserialize;
use std::collections::{BTreeMap, HashSet};

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub metadata: Option<Metadata>,
    pub provides: BTreeMap<String, ProvidesEntry>,
    #[serde(default)]
    pub requires: BTreeMap<String, RequiresEntry>,
    #[serde(default)]
    pub enable_telemetry: bool,
    // This is just here, so that we do not crash for deny_unknown_fields,
    // this is never used in Rust code.
    #[allow(dead_code)]
    #[serde(default)]
    pub enable_external_mqtt: bool,

    #[serde(default)]
    pub config: BTreeMap<String, ConfigEntry>,

    #[serde(default)]
    pub capabilities: Vec<String>,

    #[serde(default)]
    pub enable_global_errors: bool,

    // This is just here, so that we do not crash for deny_unknown_fields,
    // this is never used in Rust code. The manager reports it.
    #[allow(dead_code)]
    #[serde(default)]
    pub deprecated: Option<serde_yaml::Value>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ProvidesEntry {
    pub interface: String,
    pub description: String,
    #[serde(default)]
    pub config: BTreeMap<String, ConfigEntry>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RequiresEntry {
    pub interface: String,
    pub min_connections: Option<i64>,
    pub max_connections: Option<i64>,
    #[serde(default)]
    pub ignore: Ignore,
}

#[derive(Debug, Deserialize, Default)]
#[serde(deny_unknown_fields)]
pub struct Ignore {
    #[serde(default)]
    pub vars: HashSet<String>,

    #[serde(default)]
    pub errors: bool,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Metadata {
    pub license: String,
    pub authors: Vec<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ConfigEntry {
    pub description: Option<String>,
    // Consumed here so that it is not swept into the flattened ConfigEnum below, which denies
    // unknown fields. Never used in Rust code, the manager reports it.
    #[allow(dead_code)]
    #[serde(default)]
    pub deprecated: Option<serde_yaml::Value>,
    #[serde(flatten)]
    pub value: ConfigEnum,
    #[serde(default = "MutabilityEnum::default")]
    pub mutability: MutabilityEnum,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase", tag = "type", deny_unknown_fields)]
pub enum ConfigEnum {
    Boolean(BooleanOptions),
    String(StringOptions),
    Integer(IntegerOptions),
    Number(NumberOptions),
}

#[derive(Debug, Clone, Deserialize)]
pub enum MutabilityEnum {
    ReadOnly,
    ReadWrite,
    WriteOnly,
}

impl MutabilityEnum {
    fn default() -> Self {
        MutabilityEnum::ReadOnly
    }
}

#[cfg(test)]
mod test {
    use super::*;

    /// A manifest may declare deprecations for the module and for individual config entries. Rust
    /// does not evaluate them, but must not reject them: the manifests of required modules are
    /// parsed here too.
    #[test]
    fn test_deserialization_of_deprecations() {
        let manifest = serde_yaml::from_str::<Manifest>(
            r#"
description: A deprecated module.
deprecated:
  component: Example (1.0)
  deprecated_in: 2026.10.0
  earliest_removal: 2027.04.0
provides:
  main:
    description: An implementation.
    interface: example
config:
  deprecated_entry:
    description: A deprecated config entry.
    type: boolean
    default: true
    deprecated:
      deprecated_in: 2026.10.0
      earliest_removal: 2027.04.0
      when: false
"#,
        )
        .unwrap();

        assert!(manifest.deprecated.is_some());
        assert!(manifest.config["deprecated_entry"].deprecated.is_some());
    }
}
