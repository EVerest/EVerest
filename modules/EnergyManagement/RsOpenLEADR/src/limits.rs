// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//! Conversion of OpenADR 3 events into EVerest external energy limits.
//!
//! `IMPORT_CAPACITY_LIMIT` payloads become `schedule_import` entries and
//! `EXPORT_CAPACITY_LIMIT` payloads become `schedule_export` entries, both as
//! `total_power_W` limits. Time ranges without such a payload get an entry
//! with empty limits, which lets the EnergyNode fall back to its own
//! configured limits.

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

const LIMIT_TYPES: [EventType; 2] = [
    EventType::ImportCapacityLimit,
    EventType::ExportCapacityLimit,
];

/// Converts capacity limit payloads of `events` into Watts, so the values in
/// the resulting [`Timeline`] are unit-free. The timeline only keeps the
/// payload values, not the payload descriptors that carry the units.
///
/// Payloads without a unit are interpreted as kW. Payloads with any other unit
/// are dropped with a warning.
pub fn normalize_events(mut events: Vec<EventContent>) -> Vec<EventContent> {
    for event in &mut events {
        for limit_type in &LIMIT_TYPES {
            let unit = event
                .payload_descriptors
                .iter()
                .flatten()
                .find(|d| &d.payload_type == limit_type)
                .and_then(|d| d.units.clone());

            let factor = match unit {
                None | Some(Unit::KW) => Some(1000.0),
                Some(other) => {
                    log::warn!(
                        "Ignoring {limit_type:?} payload of event {:?} with unsupported unit {other:?}, only KW is supported",
                        event.event_name
                    );
                    None
                }
            };

            for interval in &mut event.intervals {
                match factor {
                    Some(factor) => {
                        for payload in interval
                            .payloads
                            .iter_mut()
                            .filter(|p| &p.value_type == limit_type)
                        {
                            for value in &mut payload.values {
                                if let Some(v) = value_as_f64(value) {
                                    *value = Value::Number(v * factor);
                                }
                            }
                        }
                    }
                    None => interval.payloads.retain(|p| &p.value_type != limit_type),
                }
            }
        }
    }
    events
}

/// Builds the import and export schedules for everything in `timeline` from `now` on.
pub fn timeline_to_external_limits(timeline: &Timeline, now: DateTime<Utc>) -> ExternalLimits {
    let schedule_for = |limit_type: &EventType| {
        let segments = timeline.iter().map(|(range, interval)| {
            (
                range.clone(),
                power_limit_w(interval.value_map(), limit_type),
            )
        });
        build_schedule(segments, now)
    };

    ExternalLimits {
        schedule_import: schedule_for(&EventType::ImportCapacityLimit),
        schedule_export: schedule_for(&EventType::ExportCapacityLimit),
        schedule_setpoints: Vec::new(),
    }
}

/// Builds a schedule from sorted, non-overlapping time ranges with an optional
/// power limit each.
///
/// The first entry is always at `now`. Every range that ends gets a closing
/// entry without limits, unless the next range starts right there. Consecutive
/// entries with the same limit are merged.
fn build_schedule(
    segments: impl Iterator<Item = (Range<DateTime<Utc>>, Option<f64>)>,
    now: DateTime<Utc>,
) -> Vec<ScheduleReqEntry> {
    let mut points: Vec<(DateTime<Utc>, Option<f64>)> = vec![(now, None)];

    let mut push = |time: DateTime<Utc>, limit: Option<f64>| {
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
/// `2026-11-12T12:42:41.212Z 4000W, 2026-11-12T12:46:00.000Z none`.
pub fn summarize(schedule: &[ScheduleReqEntry]) -> String {
    schedule
        .iter()
        .map(|entry| match &entry.limits_to_root.total_power_w {
            Some(power) => format!("{} {}W", entry.timestamp, power.value),
            None => format!("{} none", entry.timestamp),
        })
        .collect::<Vec<_>>()
        .join(", ")
}

fn power_limit_w(value_map: &[EventValuesMap], limit_type: &EventType) -> Option<f64> {
    let payload = value_map.iter().find(|p| &p.value_type == limit_type)?;
    let value = payload.values.first().and_then(value_as_f64);
    match value {
        Some(v) if v >= 0.0 => Some(v),
        _ => {
            log::warn!(
                "Ignoring invalid {limit_type:?} payload values: {:?}",
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

fn schedule_entry(time: DateTime<Utc>, limit_w: Option<f64>) -> ScheduleReqEntry {
    let limits = LimitsReq {
        ac_max_current_a: None,
        ac_max_phase_count: None,
        ac_min_current_a: None,
        ac_min_phase_count: None,
        ac_number_of_active_phases: None,
        ac_supports_changing_phases_during_charging: None,
        total_power_w: limit_w.map(|value| NumberWithSource {
            source: SOURCE.to_string(),
            value,
        }),
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

    fn t(hour: u32) -> DateTime<Utc> {
        Utc.with_ymd_and_hms(2026, 11, 12, hour, 0, 0).unwrap()
    }

    fn summary(schedule: &[ScheduleReqEntry]) -> Vec<(String, Option<f64>)> {
        schedule
            .iter()
            .map(|e| {
                (
                    e.timestamp.clone(),
                    e.limits_to_root.total_power_w.as_ref().map(|p| p.value),
                )
            })
            .collect()
    }

    fn ts(hour: u32) -> String {
        t(hour).to_rfc3339_opts(SecondsFormat::Millis, true)
    }

    #[test]
    fn empty_timeline_clears_limits() {
        let schedule = build_schedule(std::iter::empty(), t(10));
        assert_eq!(summary(&schedule), vec![(ts(10), None)]);
    }

    #[test]
    fn future_interval_is_opened_and_closed() {
        let schedule = build_schedule([(t(12)..t(14), Some(5000.0))].into_iter(), t(10));
        assert_eq!(
            summary(&schedule),
            vec![(ts(10), None), (ts(12), Some(5000.0)), (ts(14), None)]
        );
    }

    #[test]
    fn active_interval_starts_now_and_past_intervals_are_skipped() {
        let segments = [(t(6)..t(8), Some(1000.0)), (t(9)..t(11), Some(2000.0))];
        let schedule = build_schedule(segments.into_iter(), t(10));
        assert_eq!(
            summary(&schedule),
            vec![(ts(10), Some(2000.0)), (ts(11), None)]
        );
    }

    #[test]
    fn adjacent_intervals_have_no_gap_entry() {
        let segments = [(t(10)..t(11), Some(1000.0)), (t(11)..t(12), Some(2000.0))];
        let schedule = build_schedule(segments.into_iter(), t(10));
        assert_eq!(
            summary(&schedule),
            vec![
                (ts(10), Some(1000.0)),
                (ts(11), Some(2000.0)),
                (ts(12), None)
            ]
        );
    }

    #[test]
    fn equal_consecutive_limits_are_merged() {
        let segments = [(t(10)..t(11), Some(1000.0)), (t(11)..t(12), Some(1000.0))];
        let schedule = build_schedule(segments.into_iter(), t(10));
        assert_eq!(
            summary(&schedule),
            vec![(ts(10), Some(1000.0)), (ts(12), None)]
        );
    }

    #[test]
    fn open_ended_interval_has_no_closing_entry() {
        let schedule = build_schedule(
            [(t(11)..DateTime::<Utc>::MAX_UTC, Some(1000.0))].into_iter(),
            t(10),
        );
        assert_eq!(
            summary(&schedule),
            vec![(ts(10), None), (ts(11), Some(1000.0))]
        );
    }

    #[test]
    fn summary_lists_limits_and_gaps() {
        let schedule = build_schedule([(t(11)..t(12), Some(4000.0))].into_iter(), t(10));
        assert_eq!(
            summarize(&schedule),
            format!("{} none, {} 4000W, {} none", ts(10), ts(11), ts(12))
        );
    }

    #[test]
    fn limits_apply_to_root_and_leaves() {
        let entry = schedule_entry(t(10), Some(1000.0));
        assert_eq!(entry.limits_to_root, entry.limits_to_leaves);
        assert_eq!(entry.limits_to_root.total_power_w.unwrap().source, SOURCE);
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

    #[test]
    fn kw_and_missing_units_are_converted_to_watts() {
        for unit in [None, Some(Unit::KW)] {
            let events =
                normalize_events(vec![event(unit, vec![import_limit(Value::Number(2.5))])]);
            assert_eq!(
                events[0].intervals[0].payloads[0].values,
                vec![Value::Number(2500.0)]
            );
        }
        let events = normalize_events(vec![event(None, vec![import_limit(Value::Integer(3))])]);
        assert_eq!(
            events[0].intervals[0].payloads[0].values,
            vec![Value::Number(3000.0)]
        );
    }

    #[test]
    fn unsupported_units_are_dropped() {
        let other = EventValuesMap {
            value_type: EventType::Price,
            values: vec![Value::Number(0.3)],
        };
        let events = normalize_events(vec![event(
            Some(Unit::Amps),
            vec![import_limit(Value::Number(16.0)), other.clone()],
        )]);
        assert_eq!(events[0].intervals[0].payloads, vec![other]);
    }

    #[test]
    fn negative_limits_are_ignored() {
        assert_eq!(
            power_limit_w(
                &[import_limit(Value::Number(-1.0))],
                &EventType::ImportCapacityLimit
            ),
            None
        );
        assert_eq!(
            power_limit_w(
                &[import_limit(Value::Number(1.0))],
                &EventType::ImportCapacityLimit
            ),
            Some(1.0)
        );
    }
}
