// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <iso15118/detail/helper.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>

namespace iso15118::ev {

// Request/response slots for the pre-20 generations (ISO 15118-2, DIN SPEC 70121). Every message
// travels as V2GTP payload type 0x8001 and is decoded by protocol context. Codec supplies
// `Variant`, `Type`, `serialize(msg, view)`.
template <typename Codec> class MessageExchange {
public:
    using Variant = typename Codec::Variant;
    using Type = typename Codec::Type;

    MessageExchange() = default;

    // Serialization is deferred to transmit time. Only one request may be pending.
    template <typename Msg> void set_request(const Msg& msg) {
        if (request.has_value()) {
            throw std::logic_error("EV request slot already occupied");
        }
        PendingRequest entry;
        entry.serialize = [msg](io::StreamOutputView view) { return Codec::serialize(msg, view); };
        entry.type = Codec::template type_of<Msg>();
        request = std::move(entry);
    }

    // Already-encoded EXI (a request the EV signed itself, Plug & Charge).
    void set_raw_request(std::vector<uint8_t> exi, Type type) {
        if (request.has_value()) {
            throw std::logic_error("EV request slot already occupied");
        }
        PendingRequest entry;
        entry.serialize = [exi = std::move(exi)](io::StreamOutputView view) {
            if (exi.size() > view.payload_len) {
                throw std::runtime_error("raw request exceeds the output buffer");
            }
            std::copy(exi.begin(), exi.end(), view.payload);
            return exi.size();
        };
        entry.type = type;
        request = std::move(entry);
    }

    bool has_request() const {
        return request.has_value();
    }

    Type pending_request_type() const {
        return request.has_value() ? request->type : Type::None;
    }

    // EXI bytes of the pending request; nullopt on no request or encode failure.
    std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> take_request() {
        if (not request.has_value()) {
            logf_error("take_request called with no pending request");
            return std::nullopt;
        }
        auto entry = std::move(request.value());
        request.reset();
        try {
            const auto size = entry.serialize(io::StreamOutputView{out_buffer.data(), out_buffer.size()});
            return std::make_pair(std::vector<uint8_t>(out_buffer.begin(), out_buffer.begin() + size),
                                  io::v2gtp::PayloadType::SAP);
        } catch (const std::exception& e) {
            logf_error("EV request encode failed (buffer=%zu bytes): %s", out_buffer.size(), e.what());
            return std::nullopt;
        }
    }

    void set_response(std::unique_ptr<Variant> new_response) {
        if (response) {
            throw std::runtime_error("Previous V2G message has not been handled yet");
        }
        response = std::move(new_response);
    }

    std::unique_ptr<Variant> pull_response() {
        if (not response) {
            throw std::runtime_error("Tried to access V2G message, but there is none");
        }
        return std::move(response);
    }

    Type peek_response_type() const {
        if (not response) {
            return Type::None;
        }
        return response->get_type();
    }

private:
    // A PaymentDetailsReq carries a contract chain (leaf + up to 4 sub certificates) plus signature.
    static constexpr std::size_t OUT_BUFFER_SIZE = 8192;

    struct PendingRequest {
        std::function<std::size_t(io::StreamOutputView)> serialize;
        Type type{Type::None};
    };

    std::optional<PendingRequest> request;
    std::unique_ptr<Variant> response{nullptr};

    // Last member: the cbv2g encoder can overrun on an oversized payload.
    std::array<uint8_t, OUT_BUFFER_SIZE> out_buffer{};
};

} // namespace iso15118::ev
