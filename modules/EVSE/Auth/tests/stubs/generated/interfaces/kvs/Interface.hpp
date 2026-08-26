#ifndef KVS_INTERFACE_HPP
#define KVS_INTERFACE_HPP

#include <map>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>
// #include <utils/types.hpp>

using nlohmann::json;

using Array = nlohmann::json::array_t;
using Object = nlohmann::json::object_t;

class kvsIntf {
public:
    using Value = std::variant<std::nullptr_t, Array, Object, bool, double, int, std::string>;

    kvsIntf() {
    }

    void call_store(std::string key, Value value) {
        this->values[key] = std::move(value);
    }

    Value call_load(std::string key) {
        const auto it = this->values.find(key);
        if (it == this->values.end()) {
            // the kvs interface returns null for keys that were never stored
            return nullptr;
        }
        return it->second;
    }

    void call_delete(std::string key) {
        this->values.erase(key);
    }

    bool call_exists(std::string key) {
        return this->values.find(key) != this->values.end();
    }

private:
    std::map<std::string, Value> values;
};

#endif // KVS_INTERFACE_HPP
