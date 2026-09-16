#![allow(non_snake_case)]

//! Tests for the untyped config and mapping accessors on the generated
//! `Module`. Unlike `Module::get_config`, these return the configuration and
//! the three tier model mapping as the config file declared them, which is what
//! a module needs to reject keys it does not support or to learn which EVSE it
//! belongs to.

#[everestrs::harness(config = "config_mapping.yaml", module = "example_0")]
mod raw_config {
    /// Every key declared in `manifest.yaml` under the top level `config` block
    /// must show up in the raw map, with the type the manifest gave it.
    #[everestrs::test(config = "config_mapping.yaml", module = "example_0")]
    fn test_raw_config_contains_manifest_keys(module: &Module) {
        let raw = module.get_raw_config();

        assert_eq!(
            raw.get("some_string_config"),
            Some(&everestrs::serde_json::json!("Hello world")),
        );
        assert_eq!(
            raw.get("some_number_config"),
            Some(&everestrs::serde_json::json!(42.0)),
        );
        assert_eq!(
            raw.get("some_multiline_config"),
            Some(&everestrs::serde_json::json!(7)),
        );

        // The per implementation groups are not part of the module's own
        // config, so they must not leak in here.
        assert!(raw.get("some_bool_config").is_none());
        assert!(raw.get("some_integer_config").is_none());
    }
}
