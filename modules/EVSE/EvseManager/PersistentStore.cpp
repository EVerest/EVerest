// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PersistentStore.hpp"

#include <everest/logging.hpp>
#include <utils/date.hpp>

namespace module {

PersistentStore::PersistentStore(const std::vector<std::unique_ptr<kvsIntf>>& _r_store, const std::string module_id,
                                 std::optional<std::string> _boot_id, Writes writes) :
    r_store(_r_store), boot_id(std::move(_boot_id)) {

    if (r_store.size() > 0) {
        active = true;
    }

    session_key = module_id + "_session";

    if (active and writes == Writes::Background) {
        writer = std::thread([this]() { run_writer(); });
    }
}

PersistentStore::~PersistentStore() {
    if (writer.joinable()) {
        pending.handle()->stop = true;
        pending.notify_all();
        writer.join();
    }
}

void PersistentStore::flush() {
    if (not writer.joinable()) {
        return;
    }
    auto handle = pending.handle();
    handle.wait([&handle]() { return not handle->value.has_value() and not handle->writing; });
}

void PersistentStore::run_writer() {
    while (true) {
        KvsValue value;
        {
            auto handle = pending.handle();
            handle.wait([&handle]() { return handle->value.has_value() or handle->stop; });
            if (not handle->value.has_value()) {
                return;
            }
            value = std::move(handle->value.value());
            handle->value.reset();
            handle->writing = true;
        }
        store_in_kvs(value);
        pending.handle()->writing = false;
        pending.notify_all();
    }
}

void PersistentStore::store_transaction(StoredTransaction transaction) {
    if (not active) {
        return;
    }
    transaction.boot_id = boot_id;
    transaction.clean_shutdown = false;
    transaction.resume_after_reset = false;
    transaction.energy_transfer_paused = false;
    transaction.last_alive = Everest::Date::to_rfc3339(date::utc_clock::now());
    auto handle = record.handle();
    write(transaction);
    *handle = std::move(transaction);
}

void PersistentStore::clear_session() {
    if (not active) {
        return;
    }
    auto handle = record.handle();
    (*handle).reset();
    write_value(std::string{});
}

std::optional<StoredTransaction> PersistentStore::get_transaction() {
    if (not active) {
        return std::nullopt;
    }
    flush();
    std::optional<StoredTransaction> loaded;
    try {
        const auto value = r_store[0]->call_load(session_key);
        if (std::holds_alternative<std::string>(value)) {
            const auto& session_uuid = std::get<std::string>(value);
            if (not session_uuid.empty()) {
                loaded = StoredTransaction{};
                loaded->session_uuid = session_uuid;
            }
        } else if (std::holds_alternative<Object>(value)) {
            loaded = nlohmann::json(std::get<Object>(value)).get<StoredTransaction>();
        }
    } catch (const std::exception& e) {
        EVLOG_warning << "Could not load the stored transaction: " << e.what();
        return std::nullopt;
    }
    *record.handle() = loaded;
    return loaded;
}

void PersistentStore::touch_last_alive() {
    auto handle = record.handle();
    if (not active or not(*handle).has_value()) {
        return;
    }
    (*handle).value().last_alive = Everest::Date::to_rfc3339(date::utc_clock::now());
    write((*handle).value());
}

void PersistentStore::set_start_meter_value(const types::powermeter::Powermeter& meter_value) {
    auto handle = record.handle();
    if (not active or not(*handle).has_value()) {
        return;
    }
    (*handle).value().start_meter_value = meter_value;
    write((*handle).value());
}

void PersistentStore::mark_clean_shutdown() {
    auto handle = record.handle();
    if (not active or not(*handle).has_value()) {
        return;
    }
    (*handle).value().clean_shutdown = true;
    write((*handle).value());
}

bool PersistentStore::mark_resume_after_reset(bool energy_transfer_paused) {
    auto handle = record.handle();
    if (not active or not(*handle).has_value()) {
        return false;
    }
    (*handle).value().resume_after_reset = true;
    (*handle).value().energy_transfer_paused = energy_transfer_paused;
    (*handle).value().last_alive = Everest::Date::to_rfc3339(date::utc_clock::now());
    write((*handle).value());
    return true;
}

void PersistentStore::clear_resume_after_reset() {
    auto handle = record.handle();
    if (not active or not(*handle).has_value() or not(*handle).value().resume_after_reset) {
        return;
    }
    (*handle).value().resume_after_reset = false;
    (*handle).value().energy_transfer_paused = false;
    write((*handle).value());
}

const std::optional<std::string>& PersistentStore::get_boot_id() const {
    return boot_id;
}

void PersistentStore::write(const StoredTransaction& transaction) {
    const nlohmann::json value = transaction;
    write_value(value.get<Object>());
}

// Called with the record locked, so that writes reach the queue in the order of the changes
void PersistentStore::write_value(KvsValue value) {
    if (not writer.joinable()) {
        store_in_kvs(value);
        return;
    }
    pending.handle()->value = std::move(value);
    pending.notify_all();
}

void PersistentStore::store_in_kvs(const KvsValue& value) {
    try {
        std::visit([this](const auto& v) { r_store[0]->call_store(session_key, v); }, value);
    } catch (const std::exception& e) {
        EVLOG_warning << "Could not store the transaction: " << e.what();
    }
}

} // namespace module
