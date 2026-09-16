// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! What the module tells the ISO 15118 stack once, at boot.
//!
//! A direct port of `modules/EVSE/EvseManager/EvseManager.cpp:971-995`, which is
//! the tail of the `if (hlc_enabled)` block in `EvseManager::ready`, plus the
//! session setup at `:362` near that block's head. The order is
//! the C++ order and is preserved: the session setup, then the receipt
//! setting, then the identity and mode announcement, then the advertised
//! transfer modes, then the optional bidirectional setup, then the error
//! reset.

use crate::core::config::{
    BptChannel as ConfigChannel, BptGeneratorMode, BptIslandMethod, Settings, Wiring,
};
use crate::core::effect::{Effect, HlcUpdate};
use crate::core::hlc::dc_limits::{MaximumLimits, MinimumLimits};
use crate::core::session::EnergyTransferMode;

use super::session::{self, PlugAndCharge, Trigger};
use super::ConnectorKind;

/// Which charge mode the port is presenting to the vehicle right now.
///
/// Only the `ac_with_soc` power path has more than one answer. Every other
/// deployment presents what `charge_mode` configured for the whole run, which
/// is why this is not a second copy of `config::ChargeMode`: it names a fact
/// that changes, and it changes in exactly one place
/// (`path::ac_with_soc::AcWithSoc::present`).
///
/// The C++ carries the same fact as `EvseManager::fake_dc_enabled`, seeded from
/// `config.ac_with_soc` and written by `EvseManager::setup_fake_DC_mode` and
/// `setup_AC_mode`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PresentedMode {
    /// The fake DC mode that exists only to make the vehicle report a state of
    /// charge. `EvseManager::setup_fake_DC_mode`.
    Dc,
    /// Basic AC charging, which is what the session runs as once the state of
    /// charge has arrived. `EvseManager::setup_AC_mode`.
    Ac,
}

/// `types::iso15118::SaeJ2847BidiMode`.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum SaeBidiMode {
    #[default]
    None,
    V2g,
    V2h,
}

/// `types::iso15118::BptChannel`. Distinct from `config::BptChannel`, which
/// carries a `None` spelling for "not configured"; this one cannot, so a
/// `BptSetup` that exists always names a real channel.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BptChannel {
    Unified,
    Separated,
}

/// `types::iso15118::GeneratorMode`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GeneratorMode {
    GridFollowing,
    GridForming,
}

/// `types::iso15118::GridCodeIslandingDetectionMethod`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GridCodeIslandingDetection {
    Active,
    Passive,
}

/// `types::iso15118::BptSetup`.
///
/// It exists only when the deployment names both a channel and a generator
/// mode, which is the C++ guard at `EvseManager.cpp:977-978`. The island
/// detection method is separately optional there (`:987-991`) and stays so.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BptSetup {
    pub channel: BptChannel,
    pub generator_mode: GeneratorMode,
    pub grid_code_detection: Option<GridCodeIslandingDetection>,
}

impl BptSetup {
    /// `None` unless both halves of the C++ conjunction are named. Returning an
    /// `Option` rather than a struct with two optional fields is what makes
    /// "emitted only when both are configured" a property of the type instead
    /// of a check the emitter has to remember.
    pub fn from_settings(settings: &Settings) -> Option<Self> {
        let channel = match settings.bpt.channel {
            ConfigChannel::Unified => BptChannel::Unified,
            ConfigChannel::Separated => BptChannel::Separated,
            ConfigChannel::None => return None,
        };
        let generator_mode = match settings.bpt.generator_mode {
            BptGeneratorMode::GridFollowing => GeneratorMode::GridFollowing,
            BptGeneratorMode::GridForming => GeneratorMode::GridForming,
            BptGeneratorMode::None => return None,
        };
        let grid_code_detection = match settings.bpt.grid_code_island_method {
            BptIslandMethod::Active => Some(GridCodeIslandingDetection::Active),
            BptIslandMethod::Passive => Some(GridCodeIslandingDetection::Passive),
            BptIslandMethod::None => None,
        };
        Some(Self {
            channel,
            generator_mode,
            grid_code_detection,
        })
    }
}

/// Everything the boot sequence and the advertised set derivation read from
/// configuration, resolved once at startup.
///
/// **Its existence is `hlc_enabled`.** The C++ derives that flag at
/// `EvseManager.cpp:174-187` and then gates the whole `if (hlc_enabled)` block
/// on it; here `HlcConfig::for_deployment` is the only constructor and it
/// answers `None` for a deployment the flag would have been false on, so there
/// is no configuration to read and no port to build. Nothing downstream carries
/// a second copy of the answer: this type held an `enabled: bool` field until
/// the guards it fed were counted, thirty of them, each one hiding code that
/// only a person reading it could tell was reachable.
#[derive(Clone, Debug, PartialEq)]
pub struct HlcConfig {
    pub connector: ConnectorKind,
    pub evse_id: String,
    pub evse_id_din: String,
    /// `config.session_logging`, which reaches the stack as its `debug_mode`.
    pub session_logging: bool,
    /// `config.ev_receipt_required`.
    pub receipt_required: bool,
    /// `config.supported_iso_ac_bpt`, one of the three AC set inputs.
    pub supported_iso_ac_bpt: bool,
    /// `config.hack_allow_bpt_with_iso2`, the static config source of the
    /// bidirectional fact. It is grouped under the AC settings by the manifest
    /// and it is not an AC fact: every C++ read of it is at a DC site
    /// (`EvseManager.cpp:2334`, `:2373`) or at the charge mode independent one
    /// (`:2523`), so it is carried here rather than through the AC settings.
    pub allow_bpt_with_iso2: bool,
    /// `config.payment_enable_eim`, read by every session setup derivation.
    pub payment_enable_eim: bool,
    /// The plug and charge state as configuration seeds it
    /// (`EvseManager.cpp:198-200`). It lives here because it is where the
    /// deployment's answer arrives; `HlcPort` takes a mutable copy, because
    /// after boot the `set_plug_and_charge_configuration` command owns it.
    pub plug_and_charge: PlugAndCharge,
    pub sae_mode: SaeBidiMode,
    pub bpt: Option<BptSetup>,
    /// `config.enable_autocharge`. Read by the external identification means
    /// handler, which offers the vehicle's own identity as a token when no
    /// other authorization has arrived (`EvseManager.cpp:1008-1010`).
    pub enable_autocharge: bool,
    /// `config.autocharge_use_slac_instead_of_hlc`. It decides which layer
    /// reports the vehicle identity the autocharge token is built from: the
    /// SLAC MAC address arm (`EvseManager.cpp:179-182`) or the stack's
    /// `evcc_id` (`:1016-1028`). The two are exclusive in the C++ and only the
    /// second one stores a token for the handler to publish.
    pub autocharge_from_slac: bool,
    /// `config.dbg_hlc_auth_after_tstep`. It swaps which question the external
    /// identification means handler asks about an authorization already held
    /// (`EvseManager.cpp:1000-1001`).
    pub dbg_auth_after_tstep: bool,
    /// `{config.connector_id}`, the connector list every token this module
    /// publishes carries (`EvseManager.cpp:47`, `:1035`).
    pub connectors: Vec<i64>,
    /// `config.ac_nominal_voltage`. Read by every AC parameter emission: it is
    /// the voltage the boot physical values announce and the one every ampere
    /// figure is converted to watts at.
    pub ac_nominal_voltage_v: f64,
    /// `config.ac_max_reactive_power`. Zero means "not declared", which is why
    /// it is a plain number here and an `Option` on the wire message; see
    /// `ac_params::parameters`.
    pub ac_max_reactive_power_var: f64,
}

impl HlcConfig {
    /// The one place a deployment's high level communication answer is decided.
    ///
    /// `None` is `hlc_enabled == false`: the stack is not wired, or SLAC is
    /// not, or an AC deployment asked for neither `ac_hlc_enabled` nor
    /// `ac_with_soc`. `crate::core::config::hlc_enabled` is the predicate and
    /// this is its only caller, so the derivation has exactly one consumer and
    /// the consumer is a constructor: an `HlcConfig` that exists was derived
    /// enabled, and one that would not be enabled does not exist.
    pub fn for_deployment(settings: &Settings, wiring: &Wiring) -> Option<Self> {
        if !crate::core::config::hlc_enabled(settings, wiring) {
            return None;
        }
        Some(Self {
            connector: ConnectorKind::parse(&settings.connector_type),
            evse_id: settings.evse_id.clone(),
            evse_id_din: settings.evse_id_din.clone(),
            session_logging: settings.logging.session_logging,
            receipt_required: settings.payment.ev_receipt_required,
            supported_iso_ac_bpt: settings.ac.supported_iso_ac_bpt,
            allow_bpt_with_iso2: settings.ac.allow_bpt_with_iso2,
            payment_enable_eim: settings.payment.enable_eim,
            plug_and_charge: PlugAndCharge::from_settings(settings),
            sae_mode: sae_mode_of(settings),
            bpt: BptSetup::from_settings(settings),
            enable_autocharge: settings.payment.enable_autocharge,
            autocharge_from_slac: settings.payment.autocharge_use_slac_instead_of_hlc,
            dbg_auth_after_tstep: settings.logging.dbg_hlc_auth_after_tstep,
            connectors: vec![settings.legacy_topic_id],
            ac_nominal_voltage_v: settings.ac.nominal_voltage_v,
            ac_max_reactive_power_var: settings.ac.max_reactive_power_var,
        })
    }
}

/// The mode `call_setup` carries.
///
/// `EvseManager.cpp:442` initializes it to `None` and `:929` is the only
/// assignment, inside the DC branch and inside `if
/// (config.sae_j2847_2_bpt_enabled)`. So an AC deployment always announces
/// `None` however its SAE settings read, and that is not an omission: the SAE
/// bidirectional subscription it gates is set up in the same branch.
fn sae_mode_of(settings: &Settings) -> SaeBidiMode {
    use crate::core::config::{ChargeMode, SaeBptMode};
    if settings.charge_mode != ChargeMode::Dc || !settings.ac.sae_bpt_enabled {
        return SaeBidiMode::None;
    }
    match settings.ac.sae_bpt_mode {
        SaeBptMode::V2g => SaeBidiMode::V2g,
        SaeBptMode::V2h => SaeBidiMode::V2h,
    }
}

/// The boot sequence, as effects.
///
/// The whole C++ block is inside `if (hlc_enabled)`, and that guard is the
/// existence of `config`: a deployment without a stack holds no `HlcConfig`,
/// so this function is not reached rather than reached and emptied.
/// `mode_specific` is the charge mode branch's own boot emission: the DC limit
/// port's five commands (`EvseManager.cpp:544-561`) or the AC branch's single
/// physical values announcement (`:447-449`). It is spliced in here rather than
/// appended by the caller because this function is the one place the boot order
/// is decided, and in `ready` both of those blocks sit between the session
/// setup and the tail.
pub fn boot(
    config: &HlcConfig,
    advertised: &[EnergyTransferMode],
    pnc: PlugAndCharge,
    fake_dc: bool,
    mode_specific: Vec<Effect>,
) -> Vec<Effect> {
    let mut effects = vec![
        // `EvseManager.cpp:362`, which is near the head of the same
        // `if (hlc_enabled)` block and therefore reaches the stack before the
        // receipt setting below.
        Effect::HlcUpdate(HlcUpdate::SessionSetup(session::derive(
            Trigger::Boot,
            pnc,
            config.payment_enable_eim,
            fake_dc,
        ))),
    ];

    // `:447-449` or `:544-561`, between the session setup above and the tail
    // below.
    effects.extend(mode_specific);

    effects.extend([
        Effect::HlcUpdate(HlcUpdate::ReceiptRequired(config.receipt_required)),
        Effect::HlcUpdate(HlcUpdate::Setup {
            evse_id: config.evse_id.clone(),
            evse_id_din: config.evse_id_din.clone(),
            sae_mode: config.sae_mode,
            debug_mode: config.session_logging,
        }),
        // `publish_and_update_supported_energy_transfers` at `:975`, which is
        // the variable publish followed by the command. The two travel on
        // different execution lanes here, so their relative arrival order is
        // not fixed; nothing reads them as a sequence, unlike the session
        // events, and the stack is told the same set either way.
        Effect::PublishSupportedTransferModes(advertised.to_vec()),
        Effect::HlcUpdate(HlcUpdate::TransferModes(advertised.to_vec())),
    ]);

    if let Some(bpt) = &config.bpt {
        effects.push(Effect::HlcUpdate(HlcUpdate::BptSetup(bpt.clone())));
    }

    effects.push(Effect::HlcUpdate(HlcUpdate::ResetError));
    effects
}

/// The DC set the fake mode advertises, and the whole of it.
///
/// `EvseManager::setup_fake_DC_mode` pushes exactly these two, in this order,
/// rather than deriving a set from the connector the way an actual DC port
/// does. There is no supply behind them: the mode exists to make the vehicle
/// run a DC charge parameter discovery and report a state of charge.
const FAKE_DC_TRANSFER_MODES: [EnergyTransferMode; 2] =
    [EnergyTransferMode::DcExtended, EnergyTransferMode::DcCore];

/// The limits the fake DC mode announces, hardcoded in
/// `EvseManager::setup_fake_DC_mode` and marked `FIXME` there.
///
/// They describe no hardware. An `ac_with_soc` deployment has no DC supply
/// wired at all, so there is nothing to derive them from and nothing that
/// could ever deliver them; the vehicle is told a plausible envelope so its
/// charge parameter discovery completes and its state of charge arrives.
const FAKE_DC_PRESENT_VOLTAGE_V: f64 = 400.0;
const FAKE_DC_MAXIMUM_CURRENT_A: f64 = 400.0;
const FAKE_DC_MAXIMUM_POWER_W: f64 = 200_000.0;
const FAKE_DC_MAXIMUM_VOLTAGE_V: f64 = 1000.0;

/// Re-announce the stack for the mode this port has just started presenting.
///
/// The boot sequence above runs once. This is the second and every later
/// announcement, which only the `ac_with_soc` path produces: `EvseManager`
/// re-runs a whole HLC setup on each flip, from `switch_DC_mode` into
/// `setup_fake_DC_mode` and from `switch_AC_mode` into `setup_AC_mode`.
///
/// Unreachable without a stack, for the same reason `boot` is: with no
/// `HlcConfig` there is no port to announce from. The C++ has no such guard
/// here and would dereference an empty `r_hlc`; `config::resolve` refuses the
/// deployment that reaches it instead.
pub fn announce(config: &HlcConfig, mode: PresentedMode) -> Vec<Effect> {
    match mode {
        // `EvseManager::setup_fake_DC_mode`, in its order: the present values,
        // the maximum set, the minimum set, the identity and mode
        // announcement, then the advertised set.
        PresentedMode::Dc => {
            let mut effects = vec![
                Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                    voltage_v: FAKE_DC_PRESENT_VOLTAGE_V,
                    current_a: 0.0,
                }),
                Effect::HlcUpdate(HlcUpdate::DcMaximumLimits(MaximumLimits {
                    maximum_current_a: FAKE_DC_MAXIMUM_CURRENT_A,
                    maximum_voltage_v: FAKE_DC_MAXIMUM_VOLTAGE_V,
                    maximum_power_w: FAKE_DC_MAXIMUM_POWER_W,
                    maximum_discharge_current_a: None,
                    maximum_discharge_power_w: None,
                })),
                Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(MinimumLimits {
                    minimum_current_a: 0.0,
                    minimum_voltage_v: 0.0,
                    minimum_power_w: 0.0,
                    minimum_discharge_current_a: None,
                    minimum_discharge_power_w: None,
                })),
                Effect::HlcUpdate(HlcUpdate::Setup {
                    evse_id: config.evse_id.clone(),
                    evse_id_din: config.evse_id_din.clone(),
                    // The literal `None` of `setup_fake_DC_mode`, not
                    // `config.sae_mode`. The C++ declares a local
                    // `constexpr sae_mode` there rather than reading the
                    // deployment's setting, and the two agree in any case:
                    // `sae_mode_of` answers `None` for every AC deployment,
                    // which is what an `ac_with_soc` port is.
                    sae_mode: SaeBidiMode::None,
                    debug_mode: config.session_logging,
                }),
            ];
            effects.push(Effect::PublishSupportedTransferModes(
                FAKE_DC_TRANSFER_MODES.to_vec(),
            ));
            effects.push(Effect::HlcUpdate(HlcUpdate::TransferModes(
                FAKE_DC_TRANSFER_MODES.to_vec(),
            )));
            effects
        }

        // Nothing, and this is the C++ behaviour rather than an omission.
        //
        // `EvseManager::setup_AC_mode` sends `call_setup` and the advertised
        // set only inside `if (ac_hlc_enabled)`, and that is the function's
        // argument rather than `config.ac_hlc_enabled`. Both call sites that
        // reach it in this mode pass `false`: `EvseManager::switch_AC_mode`,
        // which is the state of charge flip, and the `subscribe_dlink_error`
        // arm that flips back when the link fails. So the AC transfer mode
        // list the C++ builds two statements earlier is computed and
        // discarded, and the whole of the AC announcement is
        // `selected_protocol = "IEC61851-1"`, a field this port does not
        // carry.
        //
        // That is coherent with what the flip does: the ISO session is being
        // torn down and the vehicle is about to be reintroduced to a basic AC
        // port. Emitting an AC `call_setup` here would be inventing an
        // announcement no deployment has ever sent.
        PresentedMode::Ac => Vec::new(),
    }
}
