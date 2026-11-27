// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ModuleAdapterStub.hpp"

namespace module::stub {

// In-memory kvs, and a power meter whose transaction commands are recorded and answered with OK
struct StoreAdapterStub : public QuietModuleAdapterStub {
    std::map<std::string, json> values;
    std::vector<std::string> stored_keys;
    std::vector<std::string> meter_started;
    std::vector<std::string> meter_stopped;

    Result call_fn(const Requirement&, const std::string& cmd, Parameters p) override {
        if (cmd == "store") {
            values[p.at("key")] = p.at("value");
            stored_keys.push_back(p.at("key"));
            return json(nullptr);
        }
        if (cmd == "load") {
            const auto it = values.find(p.at("key"));
            return it == values.end() ? json(nullptr) : it->second;
        }
        if (cmd == "delete") {
            values.erase(p.at("key").get<std::string>());
            return json(nullptr);
        }
        if (cmd == "exists") {
            return json(values.count(p.at("key")) > 0);
        }
        if (cmd == "start_transaction") {
            meter_started.push_back(p.at("value").at("transaction_id"));
            return json{{"status", "OK"}};
        }
        if (cmd == "stop_transaction") {
            meter_stopped.push_back(p.at("transaction_id"));
            return json{{"status", "OK"}};
        }
        return {};
    }
};

// Store commands block until release(), as a slow kvs over MQTT would
struct BlockingStoreAdapterStub : public StoreAdapterStub {
    std::mutex mutex;
    std::condition_variable released_cv;
    bool released{false};

    void release() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            released = true;
        }
        released_cv.notify_all();
    }

    Result call_fn(const Requirement& req, const std::string& cmd, Parameters p) override {
        if (cmd == "store") {
            std::unique_lock<std::mutex> lock(mutex);
            released_cv.wait(lock, [this]() { return released; });
        }
        return StoreAdapterStub::call_fn(req, cmd, std::move(p));
    }
};

} // namespace module::stub
