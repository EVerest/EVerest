// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef RS_PAYMENT_TERMINAL_HPP
#define RS_PAYMENT_TERMINAL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/auth_token_provider/Implementation.hpp>
#include <generated/interfaces/auth_token_validator/Implementation.hpp>
#include <generated/interfaces/payment_terminal/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/bank_session_token_provider/Interface.hpp>
#include <generated/interfaces/session_cost/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string ip;
    std::string feig_serial;
    std::string terminal_id;
    int currency;
    int pre_authorization_amount;
    int read_card_timeout;
    int read_card_debounce;
    int transactions_max_num;
    int password;
    int end_of_day_max_interval;
};

class RsPaymentTerminal : public Everest::ModuleBase {
public:
    RsPaymentTerminal() = delete;
    RsPaymentTerminal(const ModuleInfo& info, std::unique_ptr<auth_token_providerImplBase> p_token_provider,
                      std::unique_ptr<auth_token_validatorImplBase> p_token_validator,
                      std::unique_ptr<payment_terminalImplBase> p_payment_terminal,
                      std::vector<std::unique_ptr<session_costIntf>> r_session,
                      std::vector<std::unique_ptr<bank_session_token_providerIntf>> r_bank_session_token,
                      Conf& config) :
        ModuleBase(info),
        p_token_provider(std::move(p_token_provider)),
        p_token_validator(std::move(p_token_validator)),
        p_payment_terminal(std::move(p_payment_terminal)),
        r_session(std::move(r_session)),
        r_bank_session_token(std::move(r_bank_session_token)),
        config(config){};

    const std::unique_ptr<auth_token_providerImplBase> p_token_provider;
    const std::unique_ptr<auth_token_validatorImplBase> p_token_validator;
    const std::unique_ptr<payment_terminalImplBase> p_payment_terminal;
    const std::vector<std::unique_ptr<session_costIntf>> r_session;
    const std::vector<std::unique_ptr<bank_session_token_providerIntf>> r_bank_session_token;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // insert your private definitions here
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // RS_PAYMENT_TERMINAL_HPP
