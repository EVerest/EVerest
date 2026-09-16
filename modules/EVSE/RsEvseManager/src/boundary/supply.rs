// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! DC setpoint API selection, shared by the framework adapter and boundary tests.

use crate::core::effect::{EffectOutcome, SupplyMode};

pub trait SupplySetpointApi {
    fn set_import_voltage_current(&self, current_a: f64, voltage_v: f64) -> EffectOutcome;
    fn set_export_voltage_current(&self, current_a: f64, voltage_v: f64) -> EffectOutcome;
}

pub fn write_supply_setpoint(
    supply: &impl SupplySetpointApi,
    mode: SupplyMode,
    voltage_v: f64,
    current_a: f64,
) -> EffectOutcome {
    match mode {
        SupplyMode::Import => supply.set_import_voltage_current(-current_a, voltage_v),
        SupplyMode::Export => supply.set_export_voltage_current(current_a, voltage_v),
        SupplyMode::Off => EffectOutcome::Failed("setpoint requires import or export mode".into()),
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::core::effect::Effect;
    use std::cell::RefCell;

    #[derive(Default)]
    pub(crate) struct RecordingSupply(RefCell<Vec<(SupplyMode, f64, f64)>>);

    impl SupplySetpointApi for RecordingSupply {
        fn set_import_voltage_current(&self, amps: f64, volts: f64) -> EffectOutcome {
            self.0.borrow_mut().push((SupplyMode::Import, amps, volts));
            EffectOutcome::Ok
        }
        fn set_export_voltage_current(&self, amps: f64, volts: f64) -> EffectOutcome {
            self.0.borrow_mut().push((SupplyMode::Export, amps, volts));
            EffectOutcome::Ok
        }
    }

    impl RecordingSupply {
        pub(crate) fn calls(&self) -> Vec<(SupplyMode, f64, f64)> {
            self.0.borrow().clone()
        }

        pub(crate) fn apply_setpoints(&self, effects: Vec<Effect>) {
            for effect in effects {
                if let Effect::SetSupplySetpoint {
                    mode,
                    voltage_v,
                    current_a,
                } = effect
                {
                    assert_eq!(
                        write_supply_setpoint(self, mode, voltage_v, current_a),
                        EffectOutcome::Ok
                    );
                }
            }
        }
    }

    #[test]
    fn the_commanded_direction_selects_the_api_even_for_signed_zero() {
        for (mode, current_a, magnitude) in [
            (SupplyMode::Import, -10.0, 10.0),
            (SupplyMode::Export, 10.0, 10.0),
            (SupplyMode::Import, 0.0, 0.0),
            (SupplyMode::Import, -0.0, 0.0),
            (SupplyMode::Export, 0.0, 0.0),
            (SupplyMode::Export, -0.0, 0.0),
        ] {
            let supply = RecordingSupply::default();
            assert_eq!(
                write_supply_setpoint(&supply, mode, 400.0, current_a),
                EffectOutcome::Ok
            );
            assert_eq!(supply.calls(), vec![(mode, magnitude, 400.0)]);
        }
        let supply = RecordingSupply::default();
        assert!(matches!(
            write_supply_setpoint(&supply, SupplyMode::Off, 400.0, 0.0),
            EffectOutcome::Failed(_)
        ));
        assert!(supply.calls().is_empty());
    }
}
