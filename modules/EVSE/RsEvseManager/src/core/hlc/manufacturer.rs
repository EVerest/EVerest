// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Which car the MAC address the vehicle named itself with belongs to.
//!
//! `get_manufacturer_from_mac` in `modules/EVSE/EvseManager/CarManufacturer.cpp`,
//! whole. It is a total function of the address and of nothing else: no
//! configuration, no session state and no clock, so it belongs in `core` and the
//! boundary only maps its answer onto the wire enum.
//!
//! The header beside that source is the reason the list is this short, and it is
//! worth keeping: most vehicles present the MAC of their on board charger's
//! manufacturer rather than their own, so an OUI that identifies an OBC vendor
//! identifies several brands and is deliberately left out. Adding one would name
//! a manufacturer the address does not actually name.

/// `types::evse_manager::CarManufacturer`, which declares exactly these three.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CarManufacturer {
    VolkswagenGroup,
    Tesla,
    Unknown,
}

/// Tesla's registered OUIs, from `http://standards-oui.ieee.org/oui.txt` by way
/// of the C++ set.
const TESLA_OUIS: [&str; 4] = ["0C:29:8F", "4C:FC:AA", "54:F8:F0", "98:ED:5C"];

/// Volkswagen Group's, used for every brand in the group.
const VOLKSWAGEN_GROUP_OUI: &str = "00:7D:FA";

/// Tesla also holds a /28 inside `DC:44:27`, which is why this one is matched a
/// nibble longer than an OUI.
const TESLA_SUBRANGE: &str = "DC:44:27:1";

/// The three prefix comparisons the C++ makes, in its order.
///
/// Short circuits on a too short address exactly as the C++ does, before any
/// comparison: `mac.substr(0, 8)` on a shorter string throws, and the guard
/// there is what keeps a truncated address from being an exception rather than
/// an unknown manufacturer.
///
/// Compared as prefixes rather than by splitting on `:`, because the C++
/// compares fixed length substrings and the sub range prefix is not on a colon
/// boundary. An address that arrives in a different notation therefore reads as
/// `Unknown`, which is the C++ answer too.
pub fn from_mac(mac: &str) -> CarManufacturer {
    if mac.len() < 8 {
        return CarManufacturer::Unknown;
    }
    if mac.starts_with(VOLKSWAGEN_GROUP_OUI) {
        return CarManufacturer::VolkswagenGroup;
    }
    if TESLA_OUIS.iter().any(|oui| mac.starts_with(oui)) {
        return CarManufacturer::Tesla;
    }
    if mac.starts_with(TESLA_SUBRANGE) {
        return CarManufacturer::Tesla;
    }
    CarManufacturer::Unknown
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Every prefix the C++ names, so a row dropped from either list is a
    /// failure rather than a silent `Unknown`.
    #[test]
    fn every_named_prefix_reaches_its_manufacturer() {
        for oui in TESLA_OUIS {
            assert_eq!(
                from_mac(&format!("{oui}:11:22:33")),
                CarManufacturer::Tesla,
                "{oui}"
            );
        }
        assert_eq!(
            from_mac("00:7D:FA:11:22:33"),
            CarManufacturer::VolkswagenGroup
        );
        assert_eq!(from_mac("DC:44:27:11:22:33"), CarManufacturer::Tesla);
    }

    /// The sub range is a /28 and not a /24: the C++ compares ten characters,
    /// so the second nibble of the fourth octet decides. A port that compared
    /// eight would claim every `DC:44:27` address for Tesla.
    #[test]
    fn the_tesla_subrange_is_a_nibble_narrower_than_an_oui() {
        assert_eq!(from_mac("DC:44:27:1F:00:00"), CarManufacturer::Tesla);
        assert_eq!(from_mac("DC:44:27:20:00:00"), CarManufacturer::Unknown);
        assert_eq!(from_mac("DC:44:27:01:00:00"), CarManufacturer::Unknown);
    }

    /// An OUI that identifies an on board charger vendor rather than a car, the
    /// class the C++ header lists and refuses. LG Innotek builds the chargers in
    /// both an Opel Ampera and a Mercedes EQC, so the address cannot decide
    /// between them.
    #[test]
    fn an_on_board_charger_vendor_names_no_car() {
        assert_eq!(from_mac("04:4E:AF:11:22:33"), CarManufacturer::Unknown);
        assert_eq!(from_mac("CC:88:26:11:22:33"), CarManufacturer::Unknown);
    }

    /// Shorter than one OUI in the C++'s own notation. The guard is the reason
    /// this answers rather than panicking on the slice.
    #[test]
    fn an_address_too_short_to_hold_an_oui_names_no_car() {
        assert_eq!(from_mac(""), CarManufacturer::Unknown);
        assert_eq!(from_mac("00:7D:F"), CarManufacturer::Unknown);
    }

    /// Exactly eight characters, the shortest the guard admits. The C++ takes
    /// `substr(0, 8)` of it and compares, so a boundary written as `<= 8` would
    /// refuse an address the C++ accepts.
    #[test]
    fn an_address_exactly_one_oui_long_is_compared() {
        assert_eq!(from_mac("00:7D:FA"), CarManufacturer::VolkswagenGroup);
        assert_eq!(from_mac("0C:29:8F"), CarManufacturer::Tesla);
    }

    /// The autocharge identity accepts an address written without colons
    /// (`autocharge_id_token`), and this does not: the C++ compares fixed
    /// substrings that include the separators. Pinned so that the difference
    /// between the two derivations from the same input is deliberate rather
    /// than discovered.
    #[test]
    fn an_address_without_separators_names_no_car() {
        assert_eq!(from_mac("007DFA112233"), CarManufacturer::Unknown);
    }
}
