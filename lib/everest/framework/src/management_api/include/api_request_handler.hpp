// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/// \file Shared request/reply plumbing used by the management APIs (configuration and lifecycle).

#pragma once

#include <everest/logging.hpp>
#include <everest_api_types/generic/API.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>
#include <utils/mqtt_abstraction.hpp>

#include <exception>
#include <string>

namespace ev_API = everest::lib::API;

// Both helpers live in the enclosing Everest::api namespace so that the unqualified call sites in
// Everest::api::configuration and Everest::api::lifecycle keep resolving to them.
namespace Everest::api {

/// \brief Publishes \p reply, serialized, on the replyTo topic of the request \p msg.
template <typename ReplyT>
void publish_reply(MQTTAbstraction& mqtt, ev_API::V1_0::types::generic::RequestReply const& msg, ReplyT const& reply) {
    // Keep the second argument a std::string. MQTTAbstraction also declares publish(topic, json), which dumps
    // its argument: handing it a json here would double-encode the already serialized reply - the client would
    // receive a quoted JSON string instead of an object - and would publish with QOS2 instead of QOS0.
    mqtt.publish(msg.replyTo, serialize(reply));
}

/// \brief Parses the RequestReply envelope in \p data and runs work(msg) for the command \p command.
///
/// Once the envelope parses the replyTo topic is known, so every subsequent outcome must answer the client:
/// work returns false when the inner payload is invalid, and the service call it makes may throw (a call
/// dispatched to another actor is rethrown into this MQTT worker via future.get()). On either a false return
/// or an escaping exception, \p failure_reply is published, so a waiting client sees a failure instead of
/// hanging until its own timeout.
///
/// \returns false only when the envelope itself is unparseable, i.e. when there is no replyTo to answer.
template <typename FailureT, typename WorkFn>
bool handle_request(MQTTAbstraction& mqtt, std::string const& data, std::string const& command,
                    FailureT const& failure_reply, WorkFn&& work) {
    ev_API::V1_0::types::generic::RequestReply msg;
    // Qualified deliberately: this is ev_API's two-argument deserialize, which reports a bad envelope by
    // returning false. The one-argument form that ADL finds via msg, generic::deserialize<RequestReply>,
    // also links but reports by throwing, which would leave this function through the exception path
    // instead of the false return documented above.
    if (not ev_API::deserialize(data, msg)) {
        EVLOG_warning << "Failed to deserialize " << command << " request.";
        return false;
    }
    // headers.replyTo is optional per the spec, so an empty one is no reason to reject the request: the
    // command still runs. But MQTTAbstraction::publish() no-ops on an empty topic, so every reply below is
    // discarded - warn instead of letting that happen silently.
    if (msg.replyTo.empty()) {
        EVLOG_warning << command << " request has no headers.replyTo; executing anyway, but the reply is discarded.";
    }
    try {
        if (not work(msg)) {
            EVLOG_warning << "Failed to deserialize " << command << " payload.";
            publish_reply(mqtt, msg, failure_reply);
        }
    } catch (const std::exception& e) {
        EVLOG_warning << command << " request failed: " << e.what();
        publish_reply(mqtt, msg, failure_reply);
    } catch (...) {
        EVLOG_warning << command << " request failed with an unknown error.";
        publish_reply(mqtt, msg, failure_reply);
    }
    return true;
}

} // namespace Everest::api
