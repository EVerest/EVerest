// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 ModDefOrg and Contributors to EVerest
//
// RsModDefMeter — a generic EVerest powermeter driven by a ModDef device profile.
//
// * Metering: publishes the `powermeter` variable for ANY profile by querying
//   measurand-tagged points (base_quantity + phase/direction/aggregation), so no
//   per-model code is needed.
// * Transactions: for profiles that declare a start/stop transaction command
//   (matched by `command_ref` role tag "ocmf:start_transaction" /
//   "ocmf:stop_transaction", else by command_id), maps the EVerest TransactionReq
//   onto the command's typed params and runs it via ModDef's command construct.
//   Profiles without such commands advertise plain metering and return
//   NOT_SUPPORTED. This works for typed-register OCMF meters (e.g. Carlo Gavazzi
//   EM580) where each OCMF field is its own register; opaque-blob meters that need
//   caller-side payload composition are out of scope (see the module README).
//
// Modbus RTU goes through the required serial_communication_hub. ModDef-core's
// async Device/Transport methods are driven with `pollster::block_on`: the hub
// calls and the delay are blocking, so every await completes without a reactor.

include!(concat!(env!("OUT_DIR"), "/generated.rs"));

use std::collections::BTreeMap;
use std::sync::{Arc, Mutex, OnceLock};

use generated::errors::powermeter::{Error, PowermeterError};
use generated::types::powermeter::{
    Powermeter, TransactionReq, TransactionRequestStatus, TransactionStartResponse,
    TransactionStopResponse,
};
use generated::types::serial_comm_hub_requests::{StatusCodeEnum, VectorUint16};
use generated::types::units::{Current, Energy, Frequency, Power, ReactivePower, Voltage};
use generated::types::units_signed::SignedMeterValue;
use generated::{Module, SerialCommunicationHubClientPublisher};

use moddef_core::measurand::measurand_matches;
use moddef_core::schema::{Accumulation, Aggregation, Direction, ModDefDocument, PhaseRef};
use moddef_core::{DecodedValue, Device, MeasurandQuery, ParamValue, Value};

/// Role tags a profile's commands may carry (`Command.command_ref`).
const ROLE_START: &str = "ocmf:start_transaction";
const ROLE_STOP: &str = "ocmf:stop_transaction";
/// Fallback command ids when a profile doesn't set `command_ref`.
const ID_START: &str = "start_transaction";
const ID_STOP: &str = "stop_transaction";

// ---------------------------------------------------------------------------
// Transport bridge: moddef_core::Transport over the serial_communication_hub.
// ---------------------------------------------------------------------------

/// Error surface of the hub-backed transport.
#[derive(Debug)]
enum HubError {
    /// The hub command itself failed (framework/transport error).
    Everest(::everestrs::Error),
    /// The device replied with a non-Success Modbus status.
    Status(StatusCodeEnum),
    /// The reply had fewer/more words than requested, or no payload.
    ShortResponse,
    /// The Modbus function is not offered by the serial_communication_hub.
    Unsupported(&'static str),
}

impl std::fmt::Display for HubError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            HubError::Everest(e) => write!(f, "serial_comm_hub error: {e:?}"),
            HubError::Status(s) => write!(f, "modbus status {s:?}"),
            HubError::ShortResponse => write!(f, "short/empty modbus response"),
            HubError::Unsupported(what) => write!(f, "unsupported by serial_comm_hub: {what}"),
        }
    }
}
impl std::error::Error for HubError {}

/// Bridges ModDef register reads/writes onto the required serial hub. Holds a
/// clone of the hub publisher and the target Modbus unit id.
struct HubBridge {
    hub: SerialCommunicationHubClientPublisher,
    unit_id: i64,
}

impl HubBridge {
    fn status(sc: StatusCodeEnum) -> Result<(), HubError> {
        match sc {
            StatusCodeEnum::Success => Ok(()),
            other => Err(HubError::Status(other)),
        }
    }
}

// Note: the everestrs-generated hub command arguments are ordered ALPHABETICALLY
// by argument name, not in manifest order — e.g.
// modbus_read_holding_registers(first_register_address, num_registers_to_read,
// target_device_id). Matches the RsIskraMeter usage.
impl moddef_core::Transport for HubBridge {
    type Error = HubError;

    async fn read_holding(&mut self, offset: u16, out: &mut [u16]) -> Result<(), HubError> {
        let res = self
            .hub
            .modbus_read_holding_registers(offset as i64, out.len() as i64, self.unit_id)
            .map_err(HubError::Everest)?;
        HubBridge::status(res.status_code)?;
        copy_words(res.value, out)
    }

    async fn read_input(&mut self, offset: u16, out: &mut [u16]) -> Result<(), HubError> {
        let res = self
            .hub
            .modbus_read_input_registers(offset as i64, out.len() as i64, self.unit_id)
            .map_err(HubError::Everest)?;
        HubBridge::status(res.status_code)?;
        copy_words(res.value, out)
    }

    async fn read_coils(&mut self, offset: u16, out: &mut [bool]) -> Result<(), HubError> {
        let res = self
            .hub
            .modbus_read_coils(offset as i64, out.len() as i64, self.unit_id)
            .map_err(HubError::Everest)?;
        HubBridge::status(res.status_code)?;
        match res.value {
            Some(v) if v.len() == out.len() => {
                out.copy_from_slice(&v);
                Ok(())
            }
            _ => Err(HubError::ShortResponse),
        }
    }

    async fn read_discrete(&mut self, _offset: u16, _out: &mut [bool]) -> Result<(), HubError> {
        Err(HubError::Unsupported("read discrete inputs"))
    }

    async fn write_holding(&mut self, offset: u16, regs: &[u16]) -> Result<(), HubError> {
        let data = VectorUint16 {
            data: regs.iter().map(|w| *w as i64).collect(),
        };
        let sc = self
            .hub
            .modbus_write_multiple_registers(data, offset as i64, self.unit_id)
            .map_err(HubError::Everest)?;
        HubBridge::status(sc)
    }

    async fn write_coil(&mut self, offset: u16, on: bool) -> Result<(), HubError> {
        let sc = self
            .hub
            .modbus_write_single_coil(offset as i64, on, self.unit_id)
            .map_err(HubError::Everest)?;
        HubBridge::status(sc)
    }
}

fn copy_words(value: Option<Vec<i64>>, out: &mut [u16]) -> Result<(), HubError> {
    match value {
        Some(v) if v.len() == out.len() => {
            for (dst, src) in out.iter_mut().zip(v) {
                *dst = src as u16;
            }
            Ok(())
        }
        _ => Err(HubError::ShortResponse),
    }
}

/// Blocking delay for `run_command` poll steps. Because we drive the Device with
/// `pollster::block_on` on a dedicated (metering or handler) thread, sleeping the
/// thread is exactly the intended behaviour. ModDef derives command timeouts from
/// the sum of these delays, so no wall clock is needed.
struct BlockingDelay;
impl moddef_core::Delay for BlockingDelay {
    async fn delay_ms(&mut self, ms: u32) {
        std::thread::sleep(std::time::Duration::from_millis(ms as u64));
    }
}

type Dev = Device<'static, HubBridge>;

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

struct ModDefMeter {
    /// Leaked so the borrowed `Device<'static, _>` can outlive `on_ready`. The
    /// module runs for the process lifetime, so this is a one-time leak.
    doc: &'static ModDefDocument,
    device_index: usize,
    unit_id: i64,
    interval_ms: u64,
    errors_threshold: usize,
    /// Command ids resolved once at startup (None ⇒ NOT_SUPPORTED).
    start_cmd: Option<String>,
    stop_cmd: Option<String>,
    /// Set in `on_ready`, once the hub publisher is available.
    device: OnceLock<Arc<Mutex<Dev>>>,
}

impl generated::OnReadySubscriber for ModDefMeter {
    fn on_ready(&self, publishers: &generated::ModulePublisher) {
        let bridge = HubBridge {
            hub: publishers.serial_comm_hub.clone(),
            unit_id: self.unit_id,
        };
        let device = Device::from_profile(&self.doc.devices[self.device_index], bridge);
        let device = Arc::new(Mutex::new(device));
        if self.device.set(device.clone()).is_err() {
            log::warn!("on_ready called twice; ignoring");
            return;
        }

        // Best-effort: publish the OCMF public key if the profile exposes one.
        if let Some(pk) = read_public_key(&device) {
            if let Err(e) = publishers.meter.public_key_ocmf(pk) {
                log::warn!("failed to publish public_key_ocmf: {e:?}");
            }
        }

        // Fixed-rate metering loop.
        let meter_pub = publishers.meter.clone();
        let interval = std::time::Duration::from_millis(self.interval_ms);
        let threshold = self.errors_threshold;
        std::thread::spawn(move || {
            let mut consecutive_failures = 0usize;
            loop {
                let start = std::time::Instant::now();
                let result = {
                    let mut dev = device.lock().expect("device mutex poisoned");
                    read_powermeter(&mut dev)
                };
                match result {
                    Ok(meter) => {
                        if let Err(e) = meter_pub.powermeter(meter) {
                            log::error!("failed to publish powermeter: {e:?}");
                        }
                        if consecutive_failures >= threshold {
                            meter_pub.clear_error(Error::Powermeter(
                                PowermeterError::CommunicationFault,
                            ));
                        }
                        consecutive_failures = 0;
                    }
                    Err(e) => {
                        consecutive_failures += 1;
                        log::warn!("meter read failed ({consecutive_failures}/{threshold}): {e:?}");
                        if consecutive_failures == threshold {
                            meter_pub.raise_error(
                                Error::Powermeter(PowermeterError::CommunicationFault).into(),
                            );
                        }
                    }
                }
                let took = start.elapsed();
                if took < interval {
                    std::thread::sleep(interval - took);
                }
            }
        });
    }
}

impl generated::SerialCommunicationHubClientSubscriber for ModDefMeter {}

impl generated::PowermeterServiceSubscriber for ModDefMeter {
    fn start_transaction(
        &self,
        _context: &generated::Context,
        value: TransactionReq,
    ) -> ::everestrs::Result<TransactionStartResponse> {
        let Some(cmd) = self.start_cmd.as_deref() else {
            return Ok(start_not_supported());
        };
        let Some(device) = self.device.get() else {
            return Err(::everestrs::Error::HandlerException(
                "not initialized".into(),
            ));
        };
        let mut dev = device
            .lock()
            .map_err(|_| ::everestrs::Error::HandlerException("device mutex poisoned".into()))?;
        match run_start(&mut dev, cmd, &value, self.doc) {
            Ok(resp) => Ok(resp),
            Err(e) => {
                log::error!("start_transaction failed: {e:?}");
                Ok(start_err(&e))
            }
        }
    }

    fn stop_transaction(
        &self,
        _context: &generated::Context,
        _transaction_id: String,
    ) -> ::everestrs::Result<TransactionStopResponse> {
        let Some(cmd) = self.stop_cmd.as_deref() else {
            return Ok(stop_not_supported());
        };
        let Some(device) = self.device.get() else {
            return Err(::everestrs::Error::HandlerException(
                "not initialized".into(),
            ));
        };
        let mut dev = device
            .lock()
            .map_err(|_| ::everestrs::Error::HandlerException("device mutex poisoned".into()))?;
        match run_stop(&mut dev, cmd) {
            Ok(resp) => Ok(resp),
            Err(e) => {
                log::error!("stop_transaction failed: {e:?}");
                Ok(stop_err(&e))
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Metering: measurand-tagged points → Powermeter
// ---------------------------------------------------------------------------

/// A physical quantity we read from the profile, with the unit the EVerest
/// Powermeter field expects (so we can rescale from the point's own unit).
#[derive(Clone, Copy)]
enum Kind {
    Energy, // Wh
    Power,  // W
    Var,    // var
    Volt,   // V
    Amp,    // A
    Hertz,  // Hz
}

/// Find the value for a measurand query, rescaled to `kind`'s canonical EVerest
/// unit. Returns Ok(None) when the profile has no matching point (field stays
/// absent); Err only on a real read/transport failure.
fn read_field(dev: &mut Dev, q: &MeasurandQuery, kind: Kind) -> anyhow::Result<Option<f64>> {
    // Collect matching (point_id, unit) first — `points()` borrows the device
    // immutably, so finish before the mutable `read_point`.
    let mut matches: Vec<(String, String)> = dev
        .points()
        .filter(|p| measurand_matches(p.measurand.as_ref(), q))
        .map(|p| (p.point_id.clone(), p.unit.clone()))
        .collect();
    if matches.is_empty() {
        return Ok(None);
    }
    if matches.len() > 1 {
        // Ambiguous tuple (e.g. a live value and its signed-snapshot twin).
        // Prefer the point whose unit already matches the target, else the first.
        matches.sort_by_key(|(_, unit)| {
            if unit_scale(unit, kind).is_some() {
                0
            } else {
                1
            }
        });
        log::debug!(
            "measurand {} matched {} points; using {}",
            q.base_quantity,
            matches.len(),
            matches[0].0
        );
    }
    let (id, unit) = &matches[0];
    let decoded =
        pollster::block_on(dev.read_point(id)).map_err(|e| anyhow::anyhow!("read {id}: {e}"))?;
    let Some(raw) = decoded.as_f64() else {
        return Ok(None);
    };
    let scale = unit_scale(unit, kind).unwrap_or(1.0);
    Ok(Some(raw * scale))
}

/// Multiplier taking a value in `unit` to `kind`'s canonical EVerest unit
/// (Wh / W / var / V / A / Hz). `None` if the unit isn't recognised for `kind`.
fn unit_scale(unit: &str, kind: Kind) -> Option<f64> {
    match kind {
        Kind::Energy => match unit {
            "Wh" => Some(1.0),
            "kWh" => Some(1_000.0),
            "MWh" => Some(1_000_000.0),
            _ => None,
        },
        Kind::Power => match unit {
            "W" => Some(1.0),
            "kW" => Some(1_000.0),
            _ => None,
        },
        Kind::Var => match unit {
            "var" => Some(1.0),
            "kvar" => Some(1_000.0),
            _ => None,
        },
        Kind::Volt => match unit {
            "V" => Some(1.0),
            "kV" => Some(1_000.0),
            "mV" => Some(0.001),
            _ => None,
        },
        Kind::Amp => match unit {
            "A" => Some(1.0),
            "mA" => Some(0.001),
            _ => None,
        },
        Kind::Hertz => match unit {
            "Hz" => Some(1.0),
            _ => None,
        },
    }
}

fn energy(dev: &mut Dev, dir: Direction) -> anyhow::Result<Option<Energy>> {
    let q = MeasurandQuery::base("energy_active")
        .direction(dir)
        .accumulation(Accumulation::Register)
        .aggregation(Aggregation::Total);
    Ok(read_field(dev, &q, Kind::Energy)?.map(|total| Energy {
        total,
        l_1: None,
        l_2: None,
        l_3: None,
    }))
}

fn read_powermeter(dev: &mut Dev) -> anyhow::Result<Powermeter> {
    // Per-phase + total active power.
    let p = |dev: &mut Dev, phase: Option<PhaseRef>| -> anyhow::Result<Option<f64>> {
        let mut q = MeasurandQuery::base("active_power");
        q = match phase {
            Some(ph) => q.phase(ph),
            None => q.aggregation(Aggregation::Total),
        };
        read_field(dev, &q, Kind::Power)
    };
    let power = Power {
        total: p(dev, None)?.unwrap_or(0.0),
        l_1: p(dev, Some(PhaseRef::L1))?,
        l_2: p(dev, Some(PhaseRef::L2))?,
        l_3: p(dev, Some(PhaseRef::L3))?,
    };

    let v = |dev: &mut Dev, phase: PhaseRef| {
        read_field(
            dev,
            &MeasurandQuery::base("voltage").phase(phase),
            Kind::Volt,
        )
    };
    let voltage = Voltage {
        dc: None,
        l_1: v(dev, PhaseRef::L1N)?,
        l_2: v(dev, PhaseRef::L2N)?,
        l_3: v(dev, PhaseRef::L3N)?,
    };

    let c = |dev: &mut Dev, phase: PhaseRef| {
        read_field(
            dev,
            &MeasurandQuery::base("current").phase(phase),
            Kind::Amp,
        )
    };
    let current = Current {
        dc: None,
        l_1: c(dev, PhaseRef::L1)?,
        l_2: c(dev, PhaseRef::L2)?,
        l_3: c(dev, PhaseRef::L3)?,
        n: None,
    };

    let r = |dev: &mut Dev, phase: Option<PhaseRef>| -> anyhow::Result<Option<f64>> {
        let mut q = MeasurandQuery::base("reactive_power");
        q = match phase {
            Some(ph) => q.phase(ph),
            None => q.aggregation(Aggregation::Total),
        };
        read_field(dev, &q, Kind::Var)
    };
    let var = match r(dev, None)? {
        Some(total) => {
            let l_1 = r(dev, Some(PhaseRef::L1))?;
            let l_2 = r(dev, Some(PhaseRef::L2))?;
            let l_3 = r(dev, Some(PhaseRef::L3))?;
            Some(ReactivePower {
                total,
                l_1,
                l_2,
                l_3,
            })
        }
        None => None,
    };

    let frequency =
        read_field(dev, &MeasurandQuery::base("frequency"), Kind::Hertz)?.map(|l_1| Frequency {
            l_1,
            l_2: None,
            l_3: None,
        });

    let energy_wh_import = energy(dev, Direction::Import)?.unwrap_or(Energy {
        total: 0.0,
        l_1: None,
        l_2: None,
        l_3: None,
    });
    let energy_wh_export = energy(dev, Direction::Export)?;

    Ok(Powermeter {
        timestamp: chrono::Utc::now().to_rfc3339(),
        energy_wh_import,
        energy_wh_export,
        power_w: Some(power),
        voltage_v: Some(voltage),
        current_a: Some(current),
        frequency_hz: frequency,
        var,
        meter_id: None,
        phase_seq_error: None,
        // Signed / extension fields are populated by the transaction flow, not
        // the live metering loop.
        energy_wh_import_signed: None,
        energy_wh_export_signed: None,
        power_w_signed: None,
        voltage_v_signed: None,
        current_a_signed: None,
        frequency_hz_signed: None,
        var_signed: None,
        signed_meter_value: None,
        temperatures: None,
    })
}

fn read_public_key(device: &Arc<Mutex<Dev>>) -> Option<String> {
    let mut dev = device.lock().ok()?;
    let id = dev
        .points()
        .find(|p| p.point_id.contains("public_key"))
        .map(|p| p.point_id.clone())?;
    match pollster::block_on(dev.read_point(&id)) {
        Ok(DecodedValue::Bytes(b)) => Some(hex(&b)),
        Ok(DecodedValue::Str(s)) => Some(s),
        _ => None,
    }
}

fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push_str(&format!("{b:02X}"));
    }
    s
}

// ---------------------------------------------------------------------------
// Transactions
// ---------------------------------------------------------------------------

/// Build the command params from a TransactionReq and run the start command.
fn run_start(
    dev: &mut Dev,
    cmd_id: &str,
    req: &TransactionReq,
    doc: &'static ModDefDocument,
) -> anyhow::Result<TransactionStartResponse> {
    // The command's declared params tell us which OCMF fields this meter exposes
    // as registers and which enum each maps to. Unset/absent params are skipped
    // by the executor (the meter's defaults are valid).
    let cmd = dev
        .profile()
        .commands
        .iter()
        .find(|c| c.command_id == cmd_id)
        .ok_or_else(|| anyhow::anyhow!("command {cmd_id} not found"))?;

    let has = |field: &str| cmd.params.iter().any(|p| p.field == field);

    let mut params: Vec<(&str, ParamValue<'_>)> = Vec::new();

    // String / free-text fields.
    if has("transaction_id") && !req.transaction_id.is_empty() {
        params.push(("transaction_id", ParamValue::Str(&req.transaction_id)));
    }
    if has("evse_id") && !req.evse_id.is_empty() {
        params.push(("evse_id", ParamValue::Str(&req.evse_id)));
    }
    if let Some(ref data) = req.identification_data {
        if has("identification_data") {
            params.push(("identification_data", ParamValue::Str(data)));
        }
    }
    if let Some(ref tt) = req.tariff_text {
        if has("tariff_text") {
            params.push(("tariff_text", ParamValue::Str(tt)));
        }
    }

    // Enum fields: resolve the EVerest enum variant NAME against the profile's
    // enum (value names match OCMF) to get the numeric register value. Kept as
    // flat statements (no closure over `enum_vals`) to avoid borrow conflicts.
    let mut enum_vals: Vec<(&'static str, i64)> = Vec::new();

    let status_name = enum_name(&req.identification_status);
    if let Some(v) = resolve_param(cmd, doc, "identification_status", &status_name) {
        enum_vals.push(("identification_status", v));
    }
    if let Some(ref lvl) = req.identification_level {
        let name = enum_name(lvl);
        if let Some(v) = resolve_param(cmd, doc, "identification_level", &name) {
            enum_vals.push(("identification_level", v));
        }
    }
    let type_name = enum_name(&req.identification_type);
    if let Some(v) = resolve_param(cmd, doc, "identification_type", &type_name) {
        enum_vals.push(("identification_type", v));
    }

    // Flags: route each flag to its group param (by name prefix) and resolve.
    for flag in &req.identification_flags {
        let name = enum_name(flag);
        if let Some(field) = flag_group_field(&name) {
            if let Some(v) = resolve_param(cmd, doc, field, &name) {
                enum_vals.push((field, v));
            }
        }
    }
    for (field, v) in &enum_vals {
        params.push((field, ParamValue::Value(Value::I64(*v))));
    }

    let mut delay = BlockingDelay;
    let out = pollster::block_on(dev.run_command(cmd_id, &params, &mut delay))
        .map_err(|e| anyhow::anyhow!("run_command {cmd_id}: {e}"))?;
    let _ = out; // start typically only signals status; success ⇒ OK below.

    Ok(TransactionStartResponse {
        status: TransactionRequestStatus::OK,
        error: None,
        transaction_min_stop_time: None,
        transaction_max_stop_time: None,
        signed_meter_value: None,
    })
}

fn run_stop(dev: &mut Dev, cmd_id: &str) -> anyhow::Result<TransactionStopResponse> {
    let mut delay = BlockingDelay;
    let out = pollster::block_on(dev.run_command(cmd_id, &[], &mut delay))
        .map_err(|e| anyhow::anyhow!("run_command {cmd_id}: {e}"))?;

    let smv = signed_meter_value_from(&out);
    Ok(TransactionStopResponse {
        status: TransactionRequestStatus::OK,
        error: None,
        start_signed_meter_value: None,
        signed_meter_value: smv,
    })
}

/// Assemble a SignedMeterValue from a stop command's results. Looks for an OCMF
/// payload result field (`ocmf` or `signed_meter_value`) and an optional
/// `public_key`.
fn signed_meter_value_from(out: &BTreeMap<&str, DecodedValue>) -> Option<SignedMeterValue> {
    let payload = out
        .get("ocmf")
        .or_else(|| out.get("signed_meter_value"))
        .or_else(|| out.get("message"))?;
    let signed_meter_data = match payload {
        DecodedValue::Str(s) => s.clone(),
        DecodedValue::Bytes(b) => String::from_utf8_lossy(b).into_owned(),
        _ => return None,
    };
    let public_key = out.get("public_key").and_then(|v| match v {
        DecodedValue::Str(s) => Some(s.clone()),
        DecodedValue::Bytes(b) => Some(hex(b)),
        _ => None,
    });
    Some(SignedMeterValue {
        signed_meter_data,
        signing_method: String::new(),
        encoding_method: "OCMF".to_string(),
        public_key,
        timestamp: None,
    })
}

/// The enum `type_id` a command param references, if it is an enum-typed param.
/// `ValueType` carries the reference inside its `kind` oneof.
fn param_enum_type_id<'a>(cmd: &'a moddef_core::schema::Command, field: &str) -> Option<&'a str> {
    use moddef_core::schema::value_type::Kind;
    let p = cmd.params.iter().find(|p| p.field == field)?;
    match p.value_type.as_ref()?.kind.as_ref()? {
        Kind::EnumRef(er) => Some(er.type_id.as_str()),
        _ => None,
    }
}

/// Resolve an EVerest enum value `name` to the numeric value the profile's
/// enum (referenced by the command param `field`) assigns it.
fn resolve_param(
    cmd: &moddef_core::schema::Command,
    doc: &ModDefDocument,
    field: &str,
    name: &str,
) -> Option<i64> {
    let type_id = param_enum_type_id(cmd, field)?;
    resolve_enum(doc, type_id, name)
}

/// Resolve an enum value name to its numeric value within `doc`'s enum table.
fn resolve_enum(doc: &ModDefDocument, type_id: &str, name: &str) -> Option<i64> {
    doc.enums
        .iter()
        .find(|e| e.type_id == type_id)?
        .values
        .iter()
        .find(|v| v.name == name)
        .map(|v| v.value)
}

/// Group param a given OCMF IF flag belongs to (EM580 splits IF into four
/// registers, one per group).
fn flag_group_field(flag_name: &str) -> Option<&'static str> {
    if flag_name.starts_with("RFID_") {
        Some("identification_flags_rfid")
    } else if flag_name.starts_with("OCPP_") {
        Some("identification_flags_ocpp")
    } else if flag_name.starts_with("ISO15118_") {
        Some("identification_flags_iso15118")
    } else if flag_name.starts_with("PLMN_") {
        Some("identification_flags_plmn")
    } else {
        None
    }
}

/// The EVerest OCMF enums are fieldless, so their Debug repr is exactly the
/// enum value name (e.g. `ISO14443`, `RFID_PLAIN`) — which matches the profile
/// enum value names. This avoids a ~50-arm hand-written mapping.
fn enum_name<T: std::fmt::Debug>(v: &T) -> String {
    format!("{v:?}")
}

// ---------------------------------------------------------------------------
// Response constructors
// ---------------------------------------------------------------------------

fn start_not_supported() -> TransactionStartResponse {
    TransactionStartResponse {
        status: TransactionRequestStatus::NOT_SUPPORTED,
        error: Some("profile declares no start_transaction command".into()),
        transaction_min_stop_time: None,
        transaction_max_stop_time: None,
        signed_meter_value: None,
    }
}
fn start_err<E: std::fmt::Debug>(e: &E) -> TransactionStartResponse {
    TransactionStartResponse {
        status: TransactionRequestStatus::UNEXPECTED_ERROR,
        error: Some(format!("{e:?}")),
        transaction_min_stop_time: None,
        transaction_max_stop_time: None,
        signed_meter_value: None,
    }
}
fn stop_not_supported() -> TransactionStopResponse {
    TransactionStopResponse {
        status: TransactionRequestStatus::NOT_SUPPORTED,
        error: Some("profile declares no stop_transaction command".into()),
        start_signed_meter_value: None,
        signed_meter_value: None,
    }
}
fn stop_err<E: std::fmt::Debug>(e: &E) -> TransactionStopResponse {
    TransactionStopResponse {
        status: TransactionRequestStatus::UNEXPECTED_ERROR,
        error: Some(format!("{e:?}")),
        start_signed_meter_value: None,
        signed_meter_value: None,
    }
}

// ---------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------

/// Resolve the command implementing a role: prefer a `command_ref` match, then
/// fall back to a well-known `command_id`.
fn find_command(
    profile: &moddef_core::schema::DeviceProfile,
    role: &str,
    id: &str,
) -> Option<String> {
    profile
        .commands
        .iter()
        .find(|c| c.command_ref == role)
        .or_else(|| profile.commands.iter().find(|c| c.command_id == id))
        .map(|c| c.command_id.clone())
}

#[everestrs::main]
fn main(module: &Module) {
    let config = module.get_config();

    let doc = moddef_core::load(&config.profile)
        .unwrap_or_else(|e| panic!("failed to load ModDef profile {}: {e}", config.profile));
    let doc: &'static ModDefDocument = Box::leak(Box::new(doc));

    if doc.devices.is_empty() {
        panic!("profile {} declares no devices", config.profile);
    }
    let device_index = if config.device_id.is_empty() {
        0
    } else {
        doc.devices
            .iter()
            .position(|d| d.device_id == config.device_id)
            .unwrap_or_else(|| panic!("device_id {} not found in profile", config.device_id))
    };
    let profile = &doc.devices[device_index];

    let start_cmd = find_command(profile, ROLE_START, ID_START);
    let stop_cmd = find_command(profile, ROLE_STOP, ID_STOP);
    log::info!(
        "RsModDefMeter driving {} (transactions: start={}, stop={})",
        profile.device_id,
        start_cmd.is_some(),
        stop_cmd.is_some(),
    );

    let class = Arc::new(ModDefMeter {
        doc,
        device_index,
        unit_id: config.powermeter_device_id,
        interval_ms: config.read_meter_values_interval_ms as u64,
        errors_threshold: config.communication_errors_threshold as usize,
        start_cmd,
        stop_cmd,
        device: OnceLock::new(),
    });

    let _publishers = module.start(class.clone(), class.clone(), class.clone());

    loop {
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use generated::types::powermeter::{
        OCMFIdentificationFlags, OCMFIdentificationType, OCMFUserIdentificationStatus,
    };
    use generated::types::serial_comm_hub_requests::Result as HubResult;
    use mockall::predicate::eq;

    const UNIT: i64 = 1;

    fn leak_doc(yaml: &str) -> &'static ModDefDocument {
        let doc = moddef_core::parse_document(yaml.as_bytes(), moddef_core::DocumentFormat::Yaml)
            .expect("parse test profile");
        Box::leak(Box::new(doc))
    }

    fn make_dev(doc: &'static ModDefDocument, hub: SerialCommunicationHubClientPublisher) -> Dev {
        Device::from_profile(&doc.devices[0], HubBridge { hub, unit_id: UNIT })
    }

    fn ok_read(words: Vec<i64>) -> ::everestrs::Result<HubResult> {
        Ok(HubResult {
            status_code: StatusCodeEnum::Success,
            value: Some(words),
        })
    }

    // A meter with a voltage (V) and an import-energy (kWh) point.
    const METER_YAML: &str = r#"
doc_id: test.meter
name: Test Meter
version: 1.0.0
devices:
  - device_id: test
    vendor: Test
    model: M
    supported_transports: [MODBUS_RTU]
    blocks:
      - block_id: m
        space: INPUT_REGISTER
        length_words: 4
        points:
          - point_id: v_l1
            access: READ_ONLY
            storage_type: S32
            value_type: { primitive: DECIMAL }
            mapping: { space: INPUT_REGISTER, offset: 0, length_words: 2, byte_order: BIG_ENDIAN, word_order: WORD_LITTLE_ENDIAN }
            transform: { scale: { numerator: 1, denominator: 10 } }
            unit: V
            measurand: { base_quantity: voltage, phase_ref: L1_N }
          - point_id: e_imp
            access: READ_ONLY
            storage_type: S32
            value_type: { primitive: DECIMAL }
            mapping: { space: INPUT_REGISTER, offset: 2, length_words: 2, byte_order: BIG_ENDIAN, word_order: WORD_LITTLE_ENDIAN }
            transform: { scale: { numerator: 1, denominator: 10 } }
            unit: kWh
            measurand: { base_quantity: energy_active, direction: IMPORT, accumulation: REGISTER, aggregation: TOTAL }
"#;

    // A meter that declares a typed-register start_transaction command.
    const TXN_YAML: &str = r#"
doc_id: test.txn
name: Test Txn
version: 1.0.0
enums:
  - type_id: id_type
    name: ID Type
    values:
      - { value: 0, name: NONE }
      - { value: 10, name: ISO14443 }
devices:
  - device_id: test
    vendor: Test
    model: M
    supported_transports: [MODBUS_RTU]
    blocks:
      - block_id: ctrl
        space: HOLDING_REGISTER
        start_offset: 30
        length_words: 2
        points:
          - point_id: cmd
            access: WRITE_ONLY
            storage_type: U16
            value_type: { primitive: UINT32 }
            mapping: { space: HOLDING_REGISTER, offset: 30, length_words: 1, byte_order: BIG_ENDIAN }
            write: { behavior: COMMAND_TRIGGER }
          - point_id: st
            access: READ_ONLY
            storage_type: U16
            value_type: { primitive: UINT32 }
            mapping: { space: HOLDING_REGISTER, offset: 31, length_words: 1, byte_order: BIG_ENDIAN }
    commands:
      - command_id: start_transaction
        command_ref: "ocmf:start_transaction"
        params:
          - field: identification_type
            storage_type: U16
            value_type: { enum_ref: { type_id: id_type } }
            mapping: { space: HOLDING_REGISTER, offset: 10, length_words: 1, byte_order: BIG_ENDIAN }
        steps:
          - name: w_type
            write: { param: identification_type }
          - name: begin
            write: { trigger: { point_id: cmd, value: 66 } }
        results:
          - field: status
            from: st
            value_type: { primitive: UINT32 }
"#;

    // ----- pure helpers -----

    #[test]
    fn unit_scale_rescales_to_everest_units() {
        assert_eq!(unit_scale("kWh", Kind::Energy), Some(1000.0));
        assert_eq!(unit_scale("Wh", Kind::Energy), Some(1.0));
        assert_eq!(unit_scale("kW", Kind::Power), Some(1000.0));
        assert_eq!(unit_scale("mA", Kind::Amp), Some(0.001));
        assert_eq!(unit_scale("V", Kind::Volt), Some(1.0));
        assert_eq!(unit_scale("Hz", Kind::Hertz), Some(1.0));
        // Unit that doesn't belong to the requested kind is rejected.
        assert_eq!(unit_scale("kWh", Kind::Power), None);
    }

    #[test]
    fn flag_group_routing() {
        assert_eq!(
            flag_group_field("RFID_PLAIN"),
            Some("identification_flags_rfid")
        );
        assert_eq!(
            flag_group_field("OCPP_RS"),
            Some("identification_flags_ocpp")
        );
        assert_eq!(
            flag_group_field("ISO15118_PNC"),
            Some("identification_flags_iso15118")
        );
        assert_eq!(
            flag_group_field("PLMN_SMS"),
            Some("identification_flags_plmn")
        );
        assert_eq!(flag_group_field("NONSENSE"), None);
    }

    #[test]
    fn everest_enum_debug_equals_ocmf_value_name() {
        // The enum mapping relies on Debug producing the exact OCMF value name.
        assert_eq!(enum_name(&OCMFIdentificationType::ISO14443), "ISO14443");
        assert_eq!(
            enum_name(&OCMFUserIdentificationStatus::ASSIGNED),
            "ASSIGNED"
        );
        assert_eq!(
            enum_name(&OCMFIdentificationFlags::RFID_PLAIN),
            "RFID_PLAIN"
        );
    }

    #[test]
    fn resolve_enum_by_value_name() {
        let doc = leak_doc(TXN_YAML);
        assert_eq!(resolve_enum(doc, "id_type", "ISO14443"), Some(10));
        assert_eq!(resolve_enum(doc, "id_type", "NONE"), Some(0));
        assert_eq!(resolve_enum(doc, "id_type", "MISSING"), None);
        assert_eq!(resolve_enum(doc, "no_such_enum", "ISO14443"), None);
    }

    #[test]
    fn capability_detection_via_command_ref() {
        let with = leak_doc(TXN_YAML);
        assert_eq!(
            find_command(&with.devices[0], ROLE_START, ID_START),
            Some("start_transaction".to_string())
        );
        // A metering-only profile advertises no transaction command.
        let without = leak_doc(METER_YAML);
        assert_eq!(
            find_command(&without.devices[0], ROLE_START, ID_START),
            None
        );
    }

    // ----- metering (mock hub) -----

    #[test]
    fn metering_maps_measurands_and_rescales_units() {
        let mut hub = SerialCommunicationHubClientPublisher::default();
        // voltage L1-N: S32, LSW-first, 2300 -> 230.0 V (kept in V).
        hub.expect_modbus_read_input_registers()
            .with(eq(0i64), eq(2i64), eq(UNIT))
            .times(1)
            .returning(|_, _, _| ok_read(vec![0x08FC, 0x0000]));
        // import energy: 5000 -> 500.0 kWh -> rescaled to 500000 Wh.
        hub.expect_modbus_read_input_registers()
            .with(eq(2i64), eq(2i64), eq(UNIT))
            .times(1)
            .returning(|_, _, _| ok_read(vec![0x1388, 0x0000]));

        let doc = leak_doc(METER_YAML);
        let mut dev = make_dev(doc, hub);
        let pm = read_powermeter(&mut dev).expect("read_powermeter");

        assert_eq!(pm.voltage_v.expect("voltage").l_1, Some(230.0));
        assert_eq!(pm.energy_wh_import.total, 500_000.0);
        // No active-power point in the profile -> total defaults to 0.0.
        assert_eq!(pm.power_w.expect("power").total, 0.0);
        // No export-energy point -> field absent.
        assert!(pm.energy_wh_export.is_none());
    }

    // ----- transactions (mock hub) -----

    #[test]
    fn start_transaction_resolves_enum_and_triggers() {
        let mut hub = SerialCommunicationHubClientPublisher::default();
        // identification_type ISO14443 -> value 10 written to holding reg 10.
        hub.expect_modbus_write_multiple_registers()
            .withf(|data, addr, unit| data.data == vec![10i64] && *addr == 10 && *unit == UNIT)
            .times(1)
            .returning(|_, _, _| Ok(StatusCodeEnum::Success));
        // begin command 'B' (0x42 = 66) written to the trigger register.
        hub.expect_modbus_write_multiple_registers()
            .withf(|data, addr, _| data.data == vec![66i64] && *addr == 30)
            .times(1)
            .returning(|_, _, _| Ok(StatusCodeEnum::Success));
        // result read of the status register.
        hub.expect_modbus_read_holding_registers()
            .with(eq(31i64), eq(1i64), eq(UNIT))
            .returning(|_, _, _| ok_read(vec![7]));

        let doc = leak_doc(TXN_YAML);
        let mut dev = make_dev(doc, hub);
        let req = TransactionReq {
            evse_id: String::new(),
            transaction_id: String::new(),
            identification_status: OCMFUserIdentificationStatus::ASSIGNED,
            identification_type: OCMFIdentificationType::ISO14443,
            identification_flags: Vec::new(),
            identification_data: None,
            identification_level: None,
            tariff_text: None,
        };
        let resp = run_start(&mut dev, "start_transaction", &req, doc).expect("run_start");
        assert!(matches!(resp.status, TransactionRequestStatus::OK));
    }

    #[test]
    fn start_transaction_not_supported_response() {
        let r = start_not_supported();
        assert!(matches!(r.status, TransactionRequestStatus::NOT_SUPPORTED));
        let s = stop_not_supported();
        assert!(matches!(s.status, TransactionRequestStatus::NOT_SUPPORTED));
    }
}
