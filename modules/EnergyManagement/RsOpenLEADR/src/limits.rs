// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//! Conversion of OpenADR 3 events into EVerest external energy limits.
//!
//! `IMPORT_CAPACITY_LIMIT` payloads become `schedule_import` entries and
//! `EXPORT_CAPACITY_LIMIT` payloads become `schedule_export` entries, as
//! `total_power_W` limits for payloads in kW and as `ac_max_current_A` limits
//! for payloads in A. Time ranges without such a payload get an entry with
//! empty limits, which lets the EnergyNode fall back to its own configured
//! limits.

use crate::generated::types::energy::{
    ExternalLimits, LimitsReq, NumberWithSource, ScheduleReqEntry,
};
use chrono::{DateTime, SecondsFormat, Utc};
use openleadr_client::Timeline;
use openleadr_wire::event::{EventContent, EventType, EventValuesMap};
use openleadr_wire::values_map::Value;
use openleadr_wire::Unit;
use std::ops::Range;

const SOURCE: &str = "OpenADR";

#[derive(Debug, Clone, Copy, PartialEq)]
enum Direction {
    Import,
    Export,
}

const DIRECTIONS: [Direction; 2] = [Direction::Import, Direction::Export];

impl Direction {
    /// The OpenADR payload type; after [`normalize_events`] its values are in W.
    fn power_type(self) -> EventType {
        match self {
            Direction::Import => EventType::ImportCapacityLimit,
            Direction::Export => EventType::ExportCapacityLimit,
        }
    }

    /// Internal payload type that [`normalize_events`] gives to capacity limits
    /// in A, as the [`Timeline`] does not keep the units of payloads.
    fn current_type(self) -> EventType {
        match self {
            Direction::Import => EventType::Private("EVEREST_IMPORT_CAPACITY_LIMIT_AMPS".into()),
            Direction::Export => EventType::Private("EVEREST_EXPORT_CAPACITY_LIMIT_AMPS".into()),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Limit {
    PowerW(f64),
    CurrentA(f64),
}

/// Prepares capacity limit payloads of `events` for the [`Timeline`], which
/// only keeps payload values and not the payload descriptors with the units.
///
/// Payloads in kW, or without unit, are converted to W. Payloads in A are
/// moved to an internal payload type. Payloads with any other unit are dropped
/// with a warning.
pub fn normalize_events(mut events: Vec<EventContent>) -> Vec<EventContent> {
    for event in &mut events {
        for direction in DIRECTIONS {
            let power_type = direction.power_type();
            let unit = event
                .payload_descriptors
                .iter()
                .flatten()
                .find(|d| d.payload_type == power_type)
                .and_then(|d| d.units.clone());

            if let Some(other) = unit
                .as_ref()
                .filter(|u| !matches!(u, Unit::KW | Unit::Amps))
            {
                log::warn!(
                    "Ignoring {power_type:?} payloads of event {:?} with unsupported unit {other:?}, only KW and AMPS are supported",
                    event.event_name
                );
            }

            for interval in &mut event.intervals {
                let payloads = interval
                    .payloads
                    .iter_mut()
                    .filter(|p| p.value_type == power_type);
                match &unit {
                    None | Some(Unit::KW) => {
                        for payload in payloads {
                            for value in &mut payload.values {
                                if let Some(v) = value_as_f64(value) {
                                    *value = Value::Number(v * 1000.0);
                                }
                            }
                        }
                    }
                    Some(Unit::Amps) => {
                        for payload in payloads {
                            payload.value_type = direction.current_type();
                        }
                    }
                    Some(_) => interval.payloads.retain(|p| p.value_type != power_type),
                }
            }
        }
    }
    events
}

/// Builds the import and export schedules for everything in `timeline` from `now` on.
pub fn timeline_to_external_limits(timeline: &Timeline, now: DateTime<Utc>) -> ExternalLimits {
    let schedule_for = |direction: Direction| {
        let segments = timeline
            .iter()
            .map(|(range, interval)| (range.clone(), limit(interval.value_map(), direction)));
        build_schedule(segments, now)
    };

    ExternalLimits {
        schedule_import: schedule_for(Direction::Import),
        schedule_export: schedule_for(Direction::Export),
        schedule_setpoints: Vec::new(),
    }
}

/// Builds a schedule from sorted, non-overlapping time ranges with an optional
/// limit each.
///
/// The first entry is always at `now`. Every range that ends gets a closing
/// entry without limits, unless the next range starts right there. Consecutive
/// entries with the same limit are merged.
fn build_schedule(
    segments: impl Iterator<Item = (Range<DateTime<Utc>>, Option<Limit>)>,
    now: DateTime<Utc>,
) -> Vec<ScheduleReqEntry> {
    let mut points: Vec<(DateTime<Utc>, Option<Limit>)> = vec![(now, None)];

    let mut push = |time: DateTime<Utc>, limit: Option<Limit>| {
        // segments are sorted, so a point at the same time as the last one replaces it
        if points.last().is_some_and(|(t, _)| *t == time) {
            points.pop();
        }
        points.push((time, limit));
    };

    for (range, limit) in segments {
        if range.end <= now {
            continue;
        }
        push(range.start.max(now), limit);
        if range.end != DateTime::<Utc>::MAX_UTC {
            push(range.end, None);
        }
    }

    points.dedup_by(|current, previous| current.1 == previous.1);

    points
        .into_iter()
        .map(|(time, limit)| schedule_entry(time, limit))
        .collect()
}

/// Short, human readable form of a schedule for logging, e.g.
/// `2026-11-12T12:42:41.212Z 4000W, 2026-11-12T12:46:00.000Z 16A, 2026-11-12T13:00:00.000Z none`.
pub fn summarize(schedule: &[ScheduleReqEntry]) -> String {
    schedule
        .iter()
        .map(|entry| {
            let limits = &entry.limits_to_root;
            match (&limits.total_power_w, &limits.ac_max_current_a) {
                (Some(power), _) => format!("{} {}W", entry.timestamp, power.value),
                (None, Some(current)) => format!("{} {}A", entry.timestamp, current.value),
                (None, None) => format!("{} none", entry.timestamp),
            }
        })
        .collect::<Vec<_>>()
        .join(", ")
}

/// The limit of `direction` in an interval of a normalized timeline.
fn limit(value_map: &[EventValuesMap], direction: Direction) -> Option<Limit> {
    let (payload, to_limit): (_, fn(f64) -> Limit) = value_map
        .iter()
        .find(|p| p.value_type == direction.power_type())
        .map(|p| (p, Limit::PowerW as fn(f64) -> Limit))
        .or_else(|| {
            value_map
                .iter()
                .find(|p| p.value_type == direction.current_type())
                .map(|p| (p, Limit::CurrentA as fn(f64) -> Limit))
        })?;

    match payload.values.first().and_then(value_as_f64) {
        Some(v) if v >= 0.0 => Some(to_limit(v)),
        _ => {
            log::warn!(
                "Ignoring invalid {:?} payload values: {:?}",
                direction.power_type(),
                payload.values
            );
            None
        }
    }
}

fn value_as_f64(value: &Value) -> Option<f64> {
    match value {
        Value::Number(v) => Some(*v),
        Value::Integer(v) => Some(*v as f64),
        _ => None,
    }
}

fn schedule_entry(time: DateTime<Utc>, limit: Option<Limit>) -> ScheduleReqEntry {
    let with_source = |value| NumberWithSource {
        source: SOURCE.to_string(),
        value,
    };
    let limits = LimitsReq {
        ac_max_current_a: match limit {
            Some(Limit::CurrentA(a)) => Some(with_source(a)),
            _ => None,
        },
        ac_max_phase_count: None,
        ac_min_current_a: None,
        ac_min_phase_count: None,
        ac_number_of_active_phases: None,
        ac_supports_changing_phases_during_charging: None,
        total_power_w: match limit {
            Some(Limit::PowerW(w)) => Some(with_source(w)),
            _ => None,
        },
    };
    ScheduleReqEntry {
        timestamp: time.to_rfc3339_opts(SecondsFormat::Millis, true),
        limits_to_root: limits.clone(),
        limits_to_leaves: limits,
        conversion_efficiency: None,
        price_per_kwh: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::TimeZone;
    use openleadr_wire::event::{EventInterval, EventPayloadDescriptor};
    use openleadr_wire::program::ProgramId;

    const W: fn(f64) -> Option<Limit> = |w| Some(Limit::PowerW(w));

    fn t(hour: u32) -> DateTime<Utc> {
        Utc.with_ymd_and_hms(2026, 11, 12, hour, 0, 0).unwrap()
    }

    fn ts(hour: u32) -> String {
        t(hour).to_rfc3339_opts(SecondsFormat::Millis, true)
    }

    #[test]
    fn empty_timeline_clears_limits() {
        let schedule = build_schedule(std::iter::empty(), t(10));
        assert_eq!(summarize(&schedule), format!("{} none", ts(10)));
    }

    #[test]
    fn future_interval_is_opened_and_closed() {
        let schedule = build_schedule([(t(12)..t(14), W(5000.0))].into_iter(), t(10));
        assert_eq!(
            summarize(&schedule),
            format!("{} none, {} 5000W, {} none", ts(10), ts(12), ts(14))
        );
    }

    #[test]
    fn active_interval_starts_now_and_past_intervals_are_skipped() {
        let segments = [(t(6)..t(8), W(1000.0)), (t(9)..t(11), W(2000.0))];
        let schedule = build_schedule(segments.into_iter(), t(10));
        assert_eq!(
            summarize(&schedule),
            format!("{} 2000W, {} none", ts(10), ts(11))
        );
    }

    #[test]
    fn adjacent_intervals_have_no_gap_entry() {
        let segments = [
            (t(10)..t(11), W(1000.0)),
            (t(11)..t(12), Some(Limit::CurrentA(16.0))),
        ];
        let schedule = build_schedule(segments.into_iter(), t(10));
        assert_eq!(
            summarize(&schedule),
            format!("{} 1000W, {} 16A, {} none", ts(10), ts(11), ts(12))
        );
    }

    #[test]
    fn equal_consecutive_limits_are_merged() {
        let segments = [(t(10)..t(11), W(1000.0)), (t(11)..t(12), W(1000.0))];
        let schedule = build_schedule(segments.into_iter(), t(10));
        assert_eq!(
            summarize(&schedule),
            format!("{} 1000W, {} none", ts(10), ts(12))
        );
    }

    #[test]
    fn open_ended_interval_has_no_closing_entry() {
        let schedule = build_schedule(
            [(t(11)..DateTime::<Utc>::MAX_UTC, W(1000.0))].into_iter(),
            t(10),
        );
        assert_eq!(
            summarize(&schedule),
            format!("{} none, {} 1000W", ts(10), ts(11))
        );
    }

    #[test]
    fn limits_apply_to_root_and_leaves() {
        let entry = schedule_entry(t(10), W(1000.0));
        assert_eq!(entry.limits_to_root, entry.limits_to_leaves);
        assert_eq!(entry.limits_to_root.total_power_w.unwrap().source, SOURCE);
        assert_eq!(entry.limits_to_root.ac_max_current_a, None);

        let entry = schedule_entry(t(10), Some(Limit::CurrentA(16.0)));
        assert_eq!(entry.limits_to_root, entry.limits_to_leaves);
        assert_eq!(entry.limits_to_root.ac_max_current_a.unwrap().value, 16.0);
        assert_eq!(entry.limits_to_root.total_power_w, None);
    }

    fn event(unit: Option<Unit>, payloads: Vec<EventValuesMap>) -> EventContent {
        let mut event = EventContent::new(
            ProgramId::new("program-1").unwrap(),
            vec![EventInterval::new(0, payloads)],
        );
        event.payload_descriptors = unit.map(|unit| {
            vec![EventPayloadDescriptor {
                payload_type: EventType::ImportCapacityLimit,
                units: Some(unit),
                currency: None,
            }]
        });
        event
    }

    fn import_limit(value: Value) -> EventValuesMap {
        EventValuesMap {
            value_type: EventType::ImportCapacityLimit,
            values: vec![value],
        }
    }

    fn normalized_import_limit(unit: Option<Unit>, value: Value) -> Option<Limit> {
        let events = normalize_events(vec![event(unit, vec![import_limit(value)])]);
        limit(&events[0].intervals[0].payloads, Direction::Import)
    }

    #[test]
    fn kw_and_missing_units_become_power_limits_in_watts() {
        assert_eq!(normalized_import_limit(None, Value::Number(2.5)), W(2500.0));
        assert_eq!(
            normalized_import_limit(Some(Unit::KW), Value::Number(2.5)),
            W(2500.0)
        );
        assert_eq!(normalized_import_limit(None, Value::Integer(3)), W(3000.0));
    }

    #[test]
    fn amps_become_current_limits() {
        assert_eq!(
            normalized_import_limit(Some(Unit::Amps), Value::Number(16.0)),
            Some(Limit::CurrentA(16.0))
        );
        assert_eq!(
            normalized_import_limit(Some(Unit::Amps), Value::Integer(10)),
            Some(Limit::CurrentA(10.0))
        );
    }

    #[test]
    fn unsupported_units_are_dropped() {
        let other = EventValuesMap {
            value_type: EventType::Price,
            values: vec![Value::Number(0.3)],
        };
        let events = normalize_events(vec![event(
            Some(Unit::KVA),
            vec![import_limit(Value::Number(16.0)), other.clone()],
        )]);
        assert_eq!(events[0].intervals[0].payloads, vec![other]);
    }

    #[test]
    fn negative_limits_are_ignored() {
        assert_eq!(normalized_import_limit(None, Value::Number(-1.0)), None);
        assert_eq!(
            normalized_import_limit(Some(Unit::Amps), Value::Number(-1.0)),
            None
        );
    }

    #[test]
    fn limits_of_the_other_direction_are_ignored() {
        let events = normalize_events(vec![event(None, vec![import_limit(Value::Number(1.0))])]);
        assert_eq!(
            limit(&events[0].intervals[0].payloads, Direction::Export),
            None
        );
    }
}
