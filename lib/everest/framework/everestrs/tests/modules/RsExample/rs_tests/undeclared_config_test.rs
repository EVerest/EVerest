#![allow(non_snake_case)]

//! Tests for the undeclared config key accessor on the generated `Module`.
//!
//! A key the config file supplies and `manifest.yaml` does not declare never
//! reaches `Module::get_raw_config`, because the framework builds the parsed
//! config by iterating the manifest schema and keeps only the name of anything
//! left over. A module that must refuse a config key it no longer supports
//! needs that name, and needs it for every group it owns: its own config group
//! and the config group of each interface it provides.

use everestrs::{UndeclaredConfigKey, MODULE_CONFIG_GROUP};

/// Stands in for the refusal a module performs at startup. Mirrors the contract
/// of `RsEvseManager`'s `reject_unsupported_keys`: name every offending key,
/// and say which group carried it when that is not the module's own.
fn refuse(unsupported: &[&str], supplied: &[UndeclaredConfigKey]) -> Result<(), String> {
    let offending: Vec<String> = supplied
        .iter()
        .filter(|key| unsupported.contains(&key.name.as_str()))
        .map(|key| {
            if key.group == MODULE_CONFIG_GROUP {
                key.name.clone()
            } else {
                format!("{} (under implementation {})", key.name, key.group)
            }
        })
        .collect();
    if offending.is_empty() {
        return Ok(());
    }
    Err(format!("unsupported config key(s) {}", offending.join(", ")))
}

/// The keys of one group, in the order the accessor reported them.
fn keys_of(supplied: &[UndeclaredConfigKey], group: &str) -> Vec<String> {
    supplied
        .iter()
        .filter(|key| key.group == group)
        .map(|key| key.name.clone())
        .collect()
}

#[everestrs::harness(config = "config_undeclared.yaml", module = "example_0")]
mod undeclared_config {
    use super::{keys_of, refuse};
    use everestrs::MODULE_CONFIG_GROUP;

    /// A key the manifest does not declare must be reported by name under the
    /// group the config file supplied it in, and the declared keys must not be.
    #[everestrs::test(config = "config_undeclared.yaml", module = "example_0")]
    fn test_undeclared_keys_are_reported_per_group(module: &Module) {
        let undeclared = module.get_undeclared_config_keys();

        assert_eq!(
            keys_of(&undeclared, MODULE_CONFIG_GROUP),
            vec![
                "a_key_the_manifest_does_not_declare".to_string(),
                "another_undeclared_key".to_string(),
            ],
        );
        assert_eq!(
            keys_of(&undeclared, "foobar"),
            vec!["an_undeclared_interface_key".to_string()],
        );
    }

    /// The framework holds the keys in a sorted map of sorted sets, so the
    /// order is the module's own group first and then each provided
    /// implementation, both alphabetical. A refusal message built from it is
    /// therefore stable.
    #[everestrs::test(config = "config_undeclared.yaml", module = "example_0")]
    fn test_the_reported_order_is_stable(module: &Module) {
        let reported: Vec<(String, String)> = module
            .get_undeclared_config_keys()
            .into_iter()
            .map(|key| (key.group, key.name))
            .collect();

        let mut sorted = reported.clone();
        sorted.sort();
        assert_eq!(reported, sorted);
    }

    /// The undeclared keys are exactly the ones `get_raw_config` cannot carry,
    /// which is why the accessor exists.
    #[everestrs::test(config = "config_undeclared.yaml", module = "example_0")]
    fn test_raw_config_does_not_carry_undeclared_keys(module: &Module) {
        let raw = module.get_raw_config();

        assert!(raw.get("a_key_the_manifest_does_not_declare").is_none());
        assert!(raw.get("another_undeclared_key").is_none());
        assert!(raw.get("an_undeclared_interface_key").is_none());

        // The declared key is still there, so nothing was traded away.
        assert_eq!(
            raw.get("some_string_config"),
            Some(&everestrs::serde_json::json!("Hello world")),
        );
    }

    /// A key supplied under a provided interface's config group belongs to that
    /// group. Reporting it as a key of the module's own group would tell a
    /// module it was handed a key it was not, so the group travels with it.
    #[everestrs::test(config = "config_undeclared_impl_only.yaml", module = "example_0")]
    fn test_an_undeclared_interface_key_keeps_its_own_group(module: &Module) {
        let undeclared = module.get_undeclared_config_keys();

        assert!(keys_of(&undeclared, MODULE_CONFIG_GROUP).is_empty());
        assert_eq!(
            keys_of(&undeclared, "foobar"),
            vec!["an_undeclared_interface_key".to_string()],
        );
    }

    /// A module that excludes a key the config file still sets must be able to
    /// refuse startup and name it. Before the accessor existed the key was
    /// invisible and the setting was silently dropped instead.
    #[everestrs::test(config = "config_undeclared.yaml", module = "example_0")]
    fn test_an_excluded_key_can_refuse_startup_naming_it(module: &Module) {
        let error = refuse(
            &["a_key_the_manifest_does_not_declare"],
            &module.get_undeclared_config_keys(),
        )
        .expect_err("the config file supplies the excluded key");

        assert!(
            error.contains("a_key_the_manifest_does_not_declare"),
            "{error}"
        );
    }

    /// An excluded key parked in a provided interface's config group is refused
    /// as well, and the message says where it was found. Refusing only the
    /// module's own group would leave the operator's setting silently doing
    /// nothing, which is the failure the refusal exists to prevent.
    #[everestrs::test(config = "config_undeclared_impl_only.yaml", module = "example_0")]
    fn test_an_excluded_key_under_a_provided_interface_refuses_startup(module: &Module) {
        let error = refuse(
            &["an_undeclared_interface_key"],
            &module.get_undeclared_config_keys(),
        )
        .expect_err("the config file supplies the excluded key under foobar");

        assert!(
            error.contains("an_undeclared_interface_key (under implementation foobar)"),
            "{error}"
        );
    }

    /// The same refusal over a key nobody supplied must not fire, so the
    /// accessor is not reporting phantom keys.
    #[everestrs::test(config = "config_undeclared.yaml", module = "example_0")]
    fn test_an_excluded_key_nobody_supplies_does_not_refuse(module: &Module) {
        assert!(refuse(&["ac_with_soc"], &module.get_undeclared_config_keys()).is_ok());
    }
}
