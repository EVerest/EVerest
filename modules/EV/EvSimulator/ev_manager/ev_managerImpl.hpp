// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV_MANAGER_EV_MANAGER_IMPL_HPP
#define EV_MANAGER_EV_MANAGER_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/ev_manager/Implementation.hpp>

#include "../EvSimulator.hpp"

// ev@7b8b8589-7441-4bcc-88b2-5318649bd00d:v1
// insert your custom include headers here
// ev@7b8b8589-7441-4bcc-88b2-5318649bd00d:v1

namespace module {
namespace ev_manager {

struct Conf {};

class ev_managerImpl : public ev_managerImplBase {
public:
    ev_managerImpl() = delete;
    ev_managerImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<EvSimulator>& mod, Conf& config) :
        ev_managerImplBase(ev, "ev_manager"), mod(mod), config(config){};

    // ev@3d3549f3-b3cc-4b1e-9fef-8407982c0a5c:v1
    // insert your public definitions here
    // ev@3d3549f3-b3cc-4b1e-9fef-8407982c0a5c:v1

protected:
    // no commands defined for this interface

    // ev@fa362be6-78bc-496c-80aa-37ddc2a3c524:v1
    // insert your protected definitions here
    // ev@fa362be6-78bc-496c-80aa-37ddc2a3c524:v1

private:
    const Everest::PtrContainer<EvSimulator>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@12d58cad-e0b1-4f6c-83cc-8125b9ea23f5:v1
    // insert your private definitions here
    // ev@12d58cad-e0b1-4f6c-83cc-8125b9ea23f5:v1
};

// ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c8:v1
// insert other definitions here
// ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c8:v1

} // namespace ev_manager
} // namespace module

#endif // EV_MANAGER_EV_MANAGER_IMPL_HPP
