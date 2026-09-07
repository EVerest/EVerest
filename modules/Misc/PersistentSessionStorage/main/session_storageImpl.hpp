// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_SESSION_STORAGE_IMPL_HPP
#define MAIN_SESSION_STORAGE_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/session_storage/Implementation.hpp>

#include "../PersistentSessionStorage.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {};

class session_storageImpl : public session_storageImplBase {
public:
    session_storageImpl() = delete;
    session_storageImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<PersistentSessionStorage>& mod,
                        Conf& config) :
        session_storageImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual types::session_storage::SessionList
    handle_get_sessions(types::session_storage::GetSessionsRequest& request) override;
    virtual types::session_storage::SessionResult
    handle_get_session(types::session_storage::SessionIdentifier& identifier) override;
    virtual types::session_storage::ClearSessionsResult handle_clear_sessions() override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<PersistentSessionStorage>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // insert your private definitions here
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_SESSION_STORAGE_IMPL_HPP
