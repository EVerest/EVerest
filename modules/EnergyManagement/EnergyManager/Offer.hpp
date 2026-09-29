// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef OFFER_HPP
#define OFFER_HPP

#include <optional>
#include <vector>

#include "Market.hpp"
#include <generated/interfaces/energy/Interface.hpp>

namespace module {

class Offer {
public:
    /// \brief The offer to a connector at \p market that draws on \p phases.
    Offer(Market& market, const PhaseSet& phases = ALL_GRID_PHASES);

    std::optional<types::energy::OptimizerTarget> optimizer_target;
    ScheduleReq import_offer, export_offer;

private:
    void create_offer_for_local_market(Market& market, const PhaseSet& phases);
};

std::ostream& operator<<(std::ostream& out, const Offer& self);

} // namespace module

#endif // OFFER_HPP
