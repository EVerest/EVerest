// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef OCPP201_TRANSACTION_ID_TAGS_HPP
#define OCPP201_TRANSACTION_ID_TAGS_HPP

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <generated/types/authorization.hpp>

namespace module {

/// \brief Remembers the bank card id tags that started transactions. The CSMS reports the final session cost with the
/// response to the Ended event, when the transaction data of the module is already reset; this allows attributing the
/// cost to the id tag nevertheless. The response may arrive long after the transaction, e.g. once the station is back
/// online, so entries are kept until the CSMS has answered the Ended event.
class TransactionIdTags {
public:
    /// The number of transactions kept at most. Entries are normally popped once the CSMS has answered the Ended
    /// event; this bound only guards against Ended events that libocpp drops without a response.
    static constexpr std::size_t MAX_TRANSACTIONS = 64;

    /// \brief Remembers \p id_tag as the id tag of the transaction \p transaction_id. Id tags other than bank cards
    /// are ignored. Once \ref MAX_TRANSACTIONS is reached, the oldest transaction is dropped.
    void push(const std::string& transaction_id, const types::authorization::ProvidedIdToken& id_tag) {
        if (id_tag.authorization_type != types::authorization::AuthorizationType::BankCard) {
            return;
        }
        std::lock_guard<std::mutex> lock(this->mutex);
        if (this->transactions.count(transaction_id) > 0) {
            return;
        }
        while (this->transactions.size() >= MAX_TRANSACTIONS) {
            const auto oldest = std::min_element(
                this->transactions.begin(), this->transactions.end(),
                [](const auto& lhs, const auto& rhs) { return lhs.second.second < rhs.second.second; });
            this->transactions.erase(oldest);
        }
        this->transactions.emplace(transaction_id, std::make_pair(id_tag, ++this->counter));
    }

    /// \brief Removes and returns the id tag of the transaction \p transaction_id, if known.
    std::optional<types::authorization::ProvidedIdToken> pop(const std::string& transaction_id) {
        std::lock_guard<std::mutex> lock(this->mutex);
        const auto it = this->transactions.find(transaction_id);
        if (it == this->transactions.end()) {
            return std::nullopt;
        }
        auto id_tag = std::move(it->second.first);
        this->transactions.erase(it);
        return id_tag;
    }

private:
    std::mutex mutex;
    /// Orders the transactions by insertion.
    std::uint64_t counter = 0;
    std::unordered_map<std::string, std::pair<types::authorization::ProvidedIdToken, std::uint64_t>> transactions;
};

} // namespace module

#endif // OCPP201_TRANSACTION_ID_TAGS_HPP
