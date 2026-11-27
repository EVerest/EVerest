// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/*
 The Persistent Store class is an abstraction layer to store any persistent information
 (such as sessions) for the EvseManager.
*/

#ifndef EVSE_MANAGER_PERSISTENT_STORE_H_
#define EVSE_MANAGER_PERSISTENT_STORE_H_

#include <optional>
#include <string>
#include <thread>
#include <variant>

#include <everest/util/async/monitor.hpp>

#include <generated/interfaces/kvs/Interface.hpp>

#include "resume_transaction.hpp"

namespace module {

class PersistentStore {
public:
    /// \brief Background writes keep kvs round trips off the callers, which hold the charger state machine lock
    enum class Writes {
        Background,
        Immediate
    };
    explicit PersistentStore(const std::vector<std::unique_ptr<kvsIntf>>& r_store, const std::string module_id,
                             std::optional<std::string> boot_id = read_boot_id(), Writes writes = Writes::Background);
    ~PersistentStore();
    PersistentStore(const PersistentStore&) = delete;
    PersistentStore& operator=(const PersistentStore&) = delete;

    /// \brief Wait until all changes are in the kvs
    void flush();

    /// \brief Store the record of the active transaction, stamped with the current boot ID and time
    void store_transaction(StoredTransaction transaction);
    /// \brief Transaction left over in storage. A legacy plain session ID yields a record with the ID only.
    std::optional<StoredTransaction> get_transaction();
    void clear_session();

    void touch_last_alive();
    /// \brief Add the meter value at the start of the stored transaction
    void set_start_meter_value(const types::powermeter::Powermeter& meter_value);
    void mark_clean_shutdown();
    /// \brief Keep the stored transaction for resumption after an OCPP reset
    /// \param energy_transfer_paused the resumed transaction stays paused by the EVSE
    /// \returns false if there is no stored transaction to keep
    bool mark_resume_after_reset(bool energy_transfer_paused);
    /// \brief Undo mark_resume_after_reset() for a reset that did not take place
    void clear_resume_after_reset();

    const std::optional<std::string>& get_boot_id() const;

private:
    // an Object is the record, an empty string clears it
    using KvsValue = std::variant<Object, std::string>;
    struct PendingWrite {
        std::optional<KvsValue> value;
        bool writing{false};
        bool stop{false};
    };

    void write(const StoredTransaction& transaction);
    void write_value(KvsValue value);
    void store_in_kvs(const KvsValue& value);
    void run_writer();

    const std::vector<std::unique_ptr<kvsIntf>>& r_store;
    std::string session_key;
    bool active{false};
    std::optional<std::string> boot_id;
    everest::lib::util::monitor<std::optional<StoredTransaction>> record;
    // only the latest value is written, as every write replaces the whole record
    everest::lib::util::monitor<PendingWrite> pending;
    std::thread writer;
};

} // namespace module

#endif // EVSE_MANAGER_PERSISTENT_STORE_H_
