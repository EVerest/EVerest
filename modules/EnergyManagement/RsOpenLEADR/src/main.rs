// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
// EVerest expects binaries to be CamelCased, and Rust wants them to be snake_case. We yield to
// EVerest and shut up the compiler warning.
#![allow(non_snake_case)]
include!(concat!(env!("OUT_DIR"), "/generated.rs"));

mod limits;

use chrono::Utc;
use cron::Schedule;
use generated::types::energy::CapabilityLimits;
use generated::{
    Context, EmptyServiceSubscriber, ExternalEnergyLimitsClientPublisher,
    ExternalEnergyLimitsClientSubscriber, Module, ModuleConfig, ModulePublisher, OnReadySubscriber,
};
use openleadr_client::{Client, ClientCredentials, Filter, ProgramClient, Timeline};
use openleadr_wire::program::ProgramId;
use openleadr_wire::target::TargetType;
use openleadr_wire::Program;
use std::str::FromStr;
use std::sync::Arc;
use std::time::Duration;
use std::{thread, time};

/// Upper bound for a single sleep while waiting for the next retrieval, so
/// clock adjustments don't throw the retrieval schedule out of sync for long.
const MAX_SLEEP: Duration = Duration::from_secs(300);

pub struct OpenleadrClient {
    client: Client,
    program_id: ProgramId,
    filter: Option<(TargetType, Vec<String>)>,
    schedule: Schedule,
    rt: tokio::runtime::Runtime,
}

impl EmptyServiceSubscriber for OpenleadrClient {}

impl OnReadySubscriber for OpenleadrClient {
    fn on_ready(&self, publishers: &ModulePublisher) {
        self.rt.spawn(retrieval_loop(
            self.client.clone(),
            self.program_id.clone(),
            self.filter.clone(),
            self.schedule.clone(),
            publishers.limits_iface.clone(),
        ));
    }
}

impl ExternalEnergyLimitsClientSubscriber for OpenleadrClient {
    fn on_capabilities(&self, _context: &Context, value: CapabilityLimits) {
        log::debug!("Received capabilities: {:?}", value);
    }
}

/// Retrieves events once right away and then according to `schedule`, and
/// publishes the resulting limits. Failed retrievals are logged and retried at
/// the next scheduled time; the previously published limits stay in place.
async fn retrieval_loop(
    client: Client,
    program_id: ProgramId,
    filter: Option<(TargetType, Vec<String>)>,
    schedule: Schedule,
    publisher: ExternalEnergyLimitsClientPublisher,
) {
    let mut program: Option<ProgramClient> = None;

    loop {
        if program.is_none() {
            match client.get_program_by_id(&program_id).await {
                Ok(p) => program = Some(p),
                Err(e) => log::warn!("Could not retrieve OpenADR program {program_id:?}: {e:?}"),
            }
        }

        if let Some(program) = &program {
            match retrieve_timeline(program, &filter).await {
                Ok(timeline) => {
                    let limits = limits::timeline_to_external_limits(&timeline, Utc::now());
                    log::info!(
                        "Publishing external limits: import [{}], export [{}]",
                        limits::summarize(&limits.schedule_import),
                        limits::summarize(&limits.schedule_export)
                    );
                    log::debug!("External limits: {:?}", limits);
                    if let Err(e) = publisher.set_external_limits(limits) {
                        log::error!("Could not set external limits: {e:?}");
                    }
                }
                Err(e) => log::warn!("Could not retrieve OpenADR events: {e:?}"),
            }
        }

        let Some(next) = schedule.upcoming(Utc).next() else {
            log::warn!("Retrieve schedule has no upcoming times, stopping OpenADR event retrieval");
            return;
        };
        while let Ok(remaining) = (next - Utc::now()).to_std() {
            tokio::time::sleep(remaining.min(MAX_SLEEP)).await;
        }
    }
}

async fn retrieve_timeline(
    program: &ProgramClient,
    filter: &Option<(TargetType, Vec<String>)>,
) -> Result<Timeline, openleadr_client::Error> {
    let filter = match filter {
        Some((target_type, target_values)) => {
            Filter::By(target_type.clone(), target_values.as_slice())
        }
        None => Filter::None,
    };
    let events = program.get_event_list(filter).await?;
    let events = limits::normalize_events(events.iter().map(|e| e.content().clone()).collect());

    // ProgramClient::get_timeline() can't be used because the events need to be
    // normalized first, and ProgramClient doesn't expose its Program.
    let program = Program {
        id: program.id().clone(),
        created_date_time: program.created_date_time(),
        modification_date_time: program.modification_date_time(),
        content: program.content().clone(),
    };
    Timeline::from_events(&program, events.iter().collect())
        .ok_or(openleadr_client::Error::InvalidInterval)
}

fn get_credentials_from_config(config: &ModuleConfig) -> Option<ClientCredentials> {
    if !config.openadr_vtn_client_id.is_empty() && !config.openadr_vtn_client_secret.is_empty() {
        Some(ClientCredentials::new(
            config.openadr_vtn_client_id.clone(),
            config.openadr_vtn_client_secret.clone(),
        ))
    } else {
        None
    }
}

fn get_filter_from_config(config: &ModuleConfig) -> Option<(TargetType, Vec<String>)> {
    if !config.openadr_vtn_event_target_type.is_empty()
        && !config.openadr_vtn_event_target_values.is_empty()
    {
        let target_type = match config
            .openadr_vtn_event_target_type
            .to_uppercase()
            .replace("-", "_")
            .as_str()
        {
            "POWER_SERVICE_LOCATION" => TargetType::PowerServiceLocation,
            "SERVICE_AREA" => TargetType::ServiceArea,
            "GROUP" => TargetType::Group,
            "RESOURCE_NAME" => TargetType::ResourceName,
            "VEN_NAME" => TargetType::VENName,
            "EVENT_NAME" => TargetType::EventName,
            "PROGRAM_NAME" => TargetType::ProgramName,
            other => {
                log::warn!("Unknown event target type {other:?}, retrieving events without filter");
                return None;
            }
        };
        Some((
            target_type,
            config
                .openadr_vtn_event_target_values
                .split(',')
                .map(|s| s.trim().to_string())
                .collect(),
        ))
    } else {
        None
    }
}

#[everestrs::main]
fn main(module: &Module) {
    log::warn!(
        "This RsOpenLEADR module is currently experimental! Configuration parameters and the integration in \
         EVerest may change without further notice"
    );
    let config = module.get_config();
    // Multi-threaded so tasks spawned from EVerest callbacks are driven by the
    // runtime's own worker threads; main only sleeps after start-up.
    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .unwrap();

    let url = config
        .openadr_vtn_url
        .parse()
        .expect("openadr_vtn_url is not a valid URL");
    let client = Client::with_url(url, get_credentials_from_config(&config));
    let program_id = ProgramId::new(&config.openadr_vtn_program_id)
        .expect("openadr_vtn_program_id is not a valid program id");
    let schedule = Schedule::from_str(&config.openadr_vtn_retrieve_schedule)
        .expect("openadr_vtn_retrieve_schedule is not a valid cron expression");

    let adr_client = Arc::new(OpenleadrClient {
        client,
        program_id,
        filter: get_filter_from_config(&config),
        schedule,
        rt,
    });

    let _publishers = module.start(adr_client.clone(), adr_client.clone(), adr_client.clone());

    // Everest is driving execution in the background for us, nothing to do.
    loop {
        thread::sleep(time::Duration::from_millis(1000));
    }
}
