// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef GRID_SUPPORT_GRID_SUPPORT_IMPL_HPP
#define GRID_SUPPORT_GRID_SUPPORT_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/grid_support/Implementation.hpp>

#include "../Evse15118D20.hpp"

// ev@9d65c5b8-78c2-4208-95b5-ed872434a08d:v1
// insert your custom include headers here
#include <mutex>
// ev@9d65c5b8-78c2-4208-95b5-ed872434a08d:v1

namespace module {
namespace grid_support {

struct Conf {};

class grid_supportImpl : public grid_supportImplBase {
public:
    grid_supportImpl() = delete;
    grid_supportImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<Evse15118D20>& mod, Conf& config) :
        grid_supportImplBase(ev, "grid_support"), mod(mod), config(config){};

    // ev@ce703c19-c14d-4f4b-8056-2ad0f5dd0070:v1
    // insert your public definitions here
    // ev@ce703c19-c14d-4f4b-8056-2ad0f5dd0070:v1

protected:
    // command handler functions (virtual)
    virtual types::grid_support::SetDirectivesResponse
    handle_set_active_directives(types::grid_support::ActiveDirectiveSet& directives) override;

    // ev@d37d73a2-baca-45a7-9089-521171267dd9:v1
    // insert your protected definitions here
    // ev@d37d73a2-baca-45a7-9089-521171267dd9:v1

private:
    const Everest::PtrContainer<Evse15118D20>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@d014281f-feb6-4c48-8134-117ade18f2ba:v1
    // insert your private definitions here
    // Logged the first time a non-empty directive set arrives: applying directives to the EV
    // (the DER control-function relay) is not yet wired, so accepting them has no effect yet.
    std::once_flag directive_seam_log_flag;
    // ev@d014281f-feb6-4c48-8134-117ade18f2ba:v1
};

// ev@e05d7d33-4bca-4f9f-bc15-b2d61d72b99f:v1
// insert other definitions here
// ev@e05d7d33-4bca-4f9f-bc15-b2d61d72b99f:v1

} // namespace grid_support
} // namespace module

#endif // GRID_SUPPORT_GRID_SUPPORT_IMPL_HPP
