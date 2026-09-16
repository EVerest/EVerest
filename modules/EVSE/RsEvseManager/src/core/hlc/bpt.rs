// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Bidirectional power transfer, and the DER availability the AC advertised set
//! reads.
//!
//! Three C++ facts live here, because all three answer the same question and
//! the C++ keeps them apart only by where its members happen to be declared:
//!
//! - `selected_d20_energy_service` (`EvseManager.hpp:428`), the ISO 15118-20
//!   service the vehicle selected, written by `subscribe_selected_service_parameters`
//!   (`EvseManager.cpp:961-969`) and reset by the data link terminate callback
//!   (`:388`).
//! - `sae_bidi_active` (`EvseManager.hpp:242`), raised by
//!   `subscribe_sae_bidi_mode_active` (`:931-942`) and cleared by
//!   `subscribe_current_demand_finished` (`:594`).
//! - `der_available` (`EvseManager.hpp:296`), written by the `set_der_available`
//!   command (`evse/evse_managerImpl.cpp:567-576`).
//!
//! The first two are two of the three sources of the bidirectional fact; the
//! third is a boot time wiring declaration that only widens the AC advertised
//! set. They share a home because the port has one place a vehicle's power
//! direction is decided, which is what ADR-0018 asks for.

use crate::core::hlc::SelectedService;
use crate::core::session::SessionProfile;

/// The bidirectional and DER state of the port.
///
/// `discharge_withdrawn` is not a C++ member. It is the refusal ADR-0018
/// decides on: while the power supply reports no bidirectional capability, no
/// discharge is permitted whatever the three sources say. It tracks the
/// capability the supply reports now rather than latching for the session, so
/// a capability that comes back permits discharge again. See the revision note
/// in ADR-0018 for why the session long half was withdrawn.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Bpt {
    sae_bidi_active: bool,
    der_available: bool,
    selected_service: Option<SelectedService>,
    discharge_withdrawn: bool,
}

impl Bpt {
    /// The service the vehicle selected, or `None` while none has been
    /// selected on this session.
    pub fn selected_service(&self) -> Option<SelectedService> {
        self.selected_service
    }

    /// `EvseManager.cpp:963`.
    pub fn note_selected_service(&mut self, service: SelectedService) {
        self.selected_service = Some(service);
    }

    /// `EvseManager.cpp:388`, the first line of the data link terminate
    /// callback and of no other. A pause keeps the selection, because the
    /// session it belongs to can resume.
    pub fn forget_selected_service(&mut self) {
        self.selected_service = None;
    }

    /// `EvseManager.cpp:932`. The flag is raised unconditionally; the core
    /// routes the mode specific V2H schedule update to `EnergyTree` beside it.
    pub fn note_sae_bidi_active(&mut self) {
        self.sae_bidi_active = true;
    }

    pub fn sae_bidi_active(&self) -> bool {
        self.sae_bidi_active
    }

    /// `EvseManager.cpp:594`, inside the current demand finished callback. It
    /// is the only writer of `false`, so a session that ends without a current
    /// demand having finished carries the flag no further than the port does.
    pub fn note_current_demand_finished(&mut self) {
        self.sae_bidi_active = false;
    }

    pub fn der_available(&self) -> bool {
        self.der_available
    }

    /// `evse/evse_managerImpl.cpp:571`. Reports whether the stored value
    /// changed, so the caller republishes only on a change rather than on
    /// every declaration.
    pub fn set_der_available(&mut self, available: bool) -> bool {
        let changed = self.der_available != available;
        self.der_available = available;
        changed
    }

    /// ADR-0018. The power supply withdrew its bidirectional capability while a
    /// session was live. Reports whether this call is the edge, so the ramp
    /// down is asked for once rather than on every capability report that
    /// repeats the withdrawal.
    pub fn withdraw_discharge(&mut self) -> bool {
        let edge = !self.discharge_withdrawn;
        self.discharge_withdrawn = true;
        edge
    }

    /// The supply reports the capability again, or the session ended. Reports
    /// whether this call is the edge, so the caller refreshes the resolution
    /// and the vehicle's announcement once rather than on every report that
    /// carries a capability nothing had withdrawn.
    pub fn release_discharge_withdrawal(&mut self) -> bool {
        let edge = self.discharge_withdrawn;
        self.discharge_withdrawn = false;
        edge
    }

    /// The one resolution of the bidirectional fact.
    ///
    /// The C++ recomputes its disjunction at three call sites and the three are
    /// not the same expression: two name three terms (`EvseManager.cpp:2334`
    /// and `:2373`) and one names four (`:2523`, which adds the AC_BPT term).
    /// This is the four term form, because `SelectedService::is_bidirectional`
    /// answers for every bidirectional service rather than for one charge
    /// mode's. Only one of the three sites is ported and it is a DC only one,
    /// where the extra term cannot fire; `docs/architecture.md` carries the
    /// evidence for that under "The single bidirectional resolution carries
    /// AC_BPT".
    ///
    /// The withdrawal is a veto rather than a fourth source: it can only take
    /// the answer to false, and it holds only while the supply is reporting no
    /// capability.
    pub fn bidirectional(&self, allow_bpt_with_iso2: bool) -> bool {
        !self.discharge_withdrawn
            && SessionProfile::resolve_bidirectional(
                allow_bpt_with_iso2,
                self.sae_bidi_active,
                self.selected_service,
            )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The C++ tests `selected_d20_energy_service` against exactly two
    /// categories, `DC_BPT` at `EvseManager.cpp:2676` and `AC_BPT` at `:2671`.
    /// The four the port calls bidirectional are those two plus the two the
    /// same suffix names on the other connectors, which no C++ site reaches
    /// because no C++ site names them.
    #[test]
    fn the_selected_service_alone_resolves_the_fact_for_every_service() {
        let bidirectional = [
            SelectedService::AcBpt,
            SelectedService::DcBpt,
            SelectedService::DcAcdpBpt,
            SelectedService::McsBpt,
        ];
        let unidirectional = [
            SelectedService::Ac,
            SelectedService::Dc,
            SelectedService::Wpt,
            SelectedService::DcAcdp,
            SelectedService::Mcs,
            // The two DER services are AC services and are deliberately not
            // bidirectional: the C++ names `AC_BPT` and `DC_BPT` and no
            // others, so a DER session is not a discharging one by selection
            // alone. Widening this would let a vehicle export on a selection
            // the C++ reads as import only.
            SelectedService::AcDerIec,
            SelectedService::AcDerSae,
            SelectedService::Internet,
            SelectedService::ParkingStatus,
        ];
        for service in bidirectional {
            let mut bpt = Bpt::default();
            bpt.note_selected_service(service);
            assert!(
                bpt.bidirectional(false),
                "{service:?} must be bidirectional"
            );
        }
        for service in unidirectional {
            let mut bpt = Bpt::default();
            bpt.note_selected_service(service);
            assert!(
                !bpt.bidirectional(false),
                "{service:?} must not be bidirectional"
            );
        }
    }

    #[test]
    fn each_of_the_three_sources_resolves_the_fact_on_its_own() {
        assert!(!Bpt::default().bidirectional(false), "no source, no fact");
        assert!(Bpt::default().bidirectional(true), "the config alone");

        let mut sae = Bpt::default();
        sae.note_sae_bidi_active();
        assert!(sae.bidirectional(false), "the SAE flag alone");

        let mut selected = Bpt::default();
        selected.note_selected_service(SelectedService::DcBpt);
        assert!(selected.bidirectional(false), "the selected service alone");
    }

    /// Every combination of the three, so a later collapse of any pair fails
    /// here rather than passing quietly.
    #[test]
    fn the_three_sources_are_a_disjunction_across_every_combination() {
        for allow in [false, true] {
            for sae in [false, true] {
                for selected in [
                    None,
                    Some(SelectedService::Ac),
                    Some(SelectedService::AcBpt),
                ] {
                    let mut bpt = Bpt::default();
                    if sae {
                        bpt.note_sae_bidi_active();
                    }
                    if let Some(service) = selected {
                        bpt.note_selected_service(service);
                    }
                    let expected = allow || sae || selected == Some(SelectedService::AcBpt);
                    assert_eq!(
                        bpt.bidirectional(allow),
                        expected,
                        "allow={allow} sae={sae} selected={selected:?}"
                    );
                }
            }
        }
    }

    #[test]
    fn a_finished_current_demand_clears_the_sae_flag_and_nothing_else() {
        let mut bpt = Bpt::default();
        bpt.note_sae_bidi_active();
        bpt.note_current_demand_finished();
        assert!(!bpt.bidirectional(false), "the SAE source is gone");

        bpt.note_sae_bidi_active();
        bpt.note_selected_service(SelectedService::AcBpt);
        bpt.note_current_demand_finished();
        assert_eq!(bpt.selected_service(), Some(SelectedService::AcBpt));
        assert!(bpt.bidirectional(false), "the selection still stands");
    }

    #[test]
    fn a_terminated_data_link_forgets_the_selection_and_keeps_the_sae_flag() {
        let mut bpt = Bpt::default();
        bpt.note_sae_bidi_active();
        bpt.note_selected_service(SelectedService::DcBpt);

        bpt.forget_selected_service();

        assert_eq!(bpt.selected_service(), None);
        assert!(
            bpt.bidirectional(false),
            "the SAE flag survives a terminate, which only the finished current demand clears"
        );
    }

    /// ADR-0018 as revised. While the withdrawal stands the veto outranks all
    /// three sources, including the static config one, and no source lifts it;
    /// only the capability returning does, and then discharge is permitted
    /// again within the same session.
    #[test]
    fn a_withdrawn_discharge_vetoes_every_source_until_the_capability_returns() {
        let mut bpt = Bpt::default();
        bpt.note_selected_service(SelectedService::DcBpt);
        assert!(bpt.bidirectional(true));

        assert!(bpt.withdraw_discharge(), "the first call is the edge");
        assert!(!bpt.withdraw_discharge(), "a repeat is not");

        assert!(!bpt.bidirectional(true), "the config source is vetoed too");

        bpt.note_sae_bidi_active();
        bpt.note_selected_service(SelectedService::AcBpt);
        assert!(!bpt.bidirectional(true), "no source lifts the veto");

        assert!(
            bpt.release_discharge_withdrawal(),
            "the capability returning is the edge"
        );
        assert!(
            bpt.bidirectional(false),
            "the same session may discharge again"
        );
        assert!(
            !bpt.release_discharge_withdrawal(),
            "a report that withdrew nothing is not an edge"
        );

        assert!(
            bpt.withdraw_discharge(),
            "a second withdrawal is its own edge and earns its own ramp down"
        );
        assert!(!bpt.bidirectional(true));
    }

    #[test]
    fn der_availability_reports_only_the_edges() {
        let mut bpt = Bpt::default();
        assert!(!bpt.der_available());
        assert!(bpt.set_der_available(true));
        assert!(!bpt.set_der_available(true), "a repeat is not a change");
        assert!(bpt.der_available());
        assert!(bpt.set_der_available(false));
        assert!(!bpt.der_available());
    }

    /// DER availability is not a bidirectional source. It widens the AC
    /// advertised set and nothing else, which is the whole of what
    /// `evse/evse_managerImpl.cpp:567-576` does with it.
    #[test]
    fn der_availability_is_not_a_bidirectional_source() {
        let mut bpt = Bpt::default();
        bpt.set_der_available(true);
        assert!(!bpt.bidirectional(false));
    }
}
