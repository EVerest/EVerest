// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Checks that an AsyncAPI spec and the codecs of its API describe the same wire format.
//
// A test lists, per channel address, how the codec round-trips a payload and payloads the codec
// serialized from fully populated values, and per enum schema every value the codec produces.
// check_api() then requires:
// - the spec's channels and the listed channels are the same set, each channel has one message
//   and its operation sends on m2e and receives on e2m;
// - every spec example validates against its schema and survives the codec round trip unchanged;
// - every codec payload validates, so the codec writes no field, value or type the spec lacks;
// - every property the spec declares occurs in some codec payload, so the spec has no field the
//   codec lacks;
// - the codec rejects a codec payload without a key exactly when the spec requires that key;
// - every enum schema lists exactly the codec's values.
// Properties are treated as closed: a key the schema does not declare is an error.
// A channel listed without a round trip has no codec: its examples and payloads are only validated.

#pragma once

#include <everest/utils/yaml_loader.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace spec_drift {

using json = nlohmann::json;

struct ChannelCodec {
    // Deserializes a payload with the codec and serializes the result again. Empty when the module
    // reads or writes the payload without a codec.
    std::function<json(json const&)> round_trip;
    // Payloads the codec serialized from fully populated values.
    std::vector<json> samples;
};

using Channels = std::map<std::string, ChannelCodec>;
// Schema name in components/schemas -> every value the codec writes.
using Enums = std::map<std::string, std::vector<json>>;

// Every value of an enum the codec can serialize, found by serializing each underlying value until
// the serializer refuses one.
template <class E, class Serialize> std::vector<json> enum_values(Serialize serialize) {
    std::vector<json> out;
    for (int i = 0; i < 256; ++i) {
        try {
            json value = json::parse(serialize(static_cast<E>(i)));
            if (value.is_string() and value.get<std::string>().rfind("INVALID_VALUE", 0) == 0) {
                break;
            }
            out.push_back(std::move(value));
        } catch (std::exception const&) {
            break;
        }
    }
    return out;
}

inline bool same_payload(json const& a, json const& b) {
    if (a.is_number() and b.is_number()) {
        const double x = a.get<double>();
        const double y = b.get<double>();
        return std::fabs(x - y) <= 1e-6 * std::fmax(1.0, std::fabs(x));
    }
    if (a.type() != b.type() or a.size() != b.size()) {
        return false;
    }
    if (a.is_object()) {
        for (auto const& [key, value] : a.items()) {
            if (not b.contains(key) or not same_payload(value, b.at(key))) {
                return false;
            }
        }
        return true;
    }
    if (a.is_array()) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (not same_payload(a.at(i), b.at(i))) {
                return false;
            }
        }
        return true;
    }
    return a == b;
}

class Spec {
public:
    explicit Spec(std::filesystem::path const& path) : doc(json::parse(Everest::load_yaml(path).dump())) {
        // A file that starts with --- loads as a stream of documents.
        if (doc.is_array() and doc.size() == 1) {
            doc = json(doc.at(0));
        }
    }

    // Channel address -> pointer to its message.
    std::map<std::string, std::string> channels(std::vector<std::string>& errors) const {
        std::map<std::string, std::string> out;
        for (auto const& [id, channel] : doc.at("channels").items()) {
            auto const& address = channel.at("address").get<std::string>();
            auto const& messages = channel.at("messages");
            if (messages.size() != 1) {
                errors.push_back(address + ": has " + std::to_string(messages.size()) + " messages, not one");
                continue;
            }
            if (not out.emplace(address, ref_pointer(messages.begin().value())).second) {
                errors.push_back(address + ": used by more than one channel");
            }
        }
        std::map<std::string, std::string> actions;
        for (auto const& [id, operation] : doc.at("operations").items()) {
            auto const& address = at(ref_pointer(operation.at("channel"))).at("address").get<std::string>();
            if (not actions.emplace(address, operation.at("action").get<std::string>()).second) {
                errors.push_back(address + ": used by more than one operation");
            }
        }
        for (auto const& [address, message] : out) {
            const auto expected = address.rfind("m2e/", 0) == 0 ? "send" : "receive";
            const auto action = actions.find(address);
            if (action == actions.end()) {
                errors.push_back(address + ": no operation uses it");
            } else if (action->second != expected) {
                errors.push_back(address + ": its operation is " + action->second + ", not " + expected);
            }
        }
        return out;
    }

    json const& at(std::string const& pointer) const {
        return doc.at(json::json_pointer(pointer));
    }

    // A key of a validated value, and whether its schema requires it.
    struct Key {
        json::json_pointer at;
        bool required;
    };

    // Validates value against the schema at pointer and returns its object keys. With record, the
    // properties it uses count as covered.
    std::vector<Key> validate(std::string const& pointer, json const& value, std::string const& where,
                              std::vector<std::string>& errors, bool record) {
        Walk walk;
        validate(pointer, value, json::json_pointer{}, where, errors, walk);
        if (record) {
            covered.insert(walk.seen.begin(), walk.seen.end());
        }
        return walk.keys;
    }

    // Pointers of every property reachable from the schema at pointer.
    void properties(std::string const& pointer, std::set<std::string>& out, std::set<std::string>& enums,
                    std::set<std::string>& visited) const {
        if (not visited.insert(pointer).second) {
            return;
        }
        auto const& schema = at(pointer);
        if (schema.contains("$ref")) {
            properties(ref_pointer(schema), out, enums, visited);
            return;
        }
        if (schema.contains("enum")) {
            enums.insert(pointer);
        }
        if (schema.contains("properties")) {
            for (auto const& [key, sub] : schema.at("properties").items()) {
                out.insert(pointer + "/properties/" + key);
                properties(pointer + "/properties/" + key, out, enums, visited);
            }
        }
        if (schema.contains("items")) {
            properties(pointer + "/items", out, enums, visited);
        }
        if (schema.contains("oneOf")) {
            for (std::size_t i = 0; i < schema.at("oneOf").size(); ++i) {
                properties(pointer + "/oneOf/" + std::to_string(i), out, enums, visited);
            }
        }
    }

    std::set<std::string> covered;

    static std::string ref_pointer(json const& node) {
        const auto ref = node.at("$ref").get<std::string>();
        if (ref.rfind("#/", 0) != 0) {
            throw std::invalid_argument("only local $ref is supported: " + ref);
        }
        return ref.substr(1);
    }

private:
    json doc;

    struct Walk {
        std::set<std::string> seen;
        std::vector<Key> keys;
    };

    void validate(std::string const& pointer, json const& value, json::json_pointer const& at_value,
                  std::string const& where, std::vector<std::string>& errors, Walk& walk) const {
        auto const& schema = at(pointer);
        if (schema.contains("$ref")) {
            validate(ref_pointer(schema), value, at_value, where, errors, walk);
            return;
        }
        if (schema.contains("oneOf")) {
            std::size_t matches = 0;
            Walk match;
            for (std::size_t i = 0; i < schema.at("oneOf").size(); ++i) {
                std::vector<std::string> branch_errors;
                Walk branch;
                validate(pointer + "/oneOf/" + std::to_string(i), value, at_value, where, branch_errors, branch);
                if (branch_errors.empty()) {
                    ++matches;
                    match = std::move(branch);
                }
            }
            if (matches != 1) {
                errors.push_back(where + ": matches " + std::to_string(matches) + " oneOf branches of " + pointer);
                return;
            }
            walk.seen.insert(match.seen.begin(), match.seen.end());
            walk.keys.insert(walk.keys.end(), match.keys.begin(), match.keys.end());
        }
        if (schema.contains("type") and not has_type(value, schema.at("type").get<std::string>())) {
            errors.push_back(where + ": " + value.dump() + " is not of type " + schema.at("type").get<std::string>());
            return;
        }
        if (schema.contains("const") and schema.at("const") != value) {
            errors.push_back(where + ": " + value.dump() + " is not " + schema.at("const").dump());
        }
        if (schema.contains("enum")) {
            auto const& values = schema.at("enum");
            if (std::find(values.begin(), values.end(), value) == values.end()) {
                errors.push_back(where + ": " + value.dump() + " is not in the enum of " + pointer);
            }
        }
        if (value.is_number()) {
            if (schema.contains("minimum") and value.get<double>() < schema.at("minimum").get<double>()) {
                errors.push_back(where + ": " + value.dump() + " is below the minimum of " + pointer);
            }
            if (schema.contains("maximum") and value.get<double>() > schema.at("maximum").get<double>()) {
                errors.push_back(where + ": " + value.dump() + " is above the maximum of " + pointer);
            }
        }
        if (value.is_array()) {
            if (schema.contains("minItems") and value.size() < schema.at("minItems").get<std::size_t>()) {
                errors.push_back(where + ": fewer items than the minimum of " + pointer);
            }
            if (schema.contains("items")) {
                for (std::size_t i = 0; i < value.size(); ++i) {
                    validate(pointer + "/items", value.at(i), at_value / i, where + "/" + std::to_string(i), errors,
                             walk);
                }
            }
        }
        if (value.is_object() and not schema.contains("oneOf")) {
            const auto declared = schema.value("properties", json::object());
            const auto required = schema.value("required", json::array());
            for (auto const& key : required) {
                if (not value.contains(key.get<std::string>())) {
                    errors.push_back(where + ": required " + key.get<std::string>() + " is missing");
                }
            }
            for (auto const& [key, sub] : value.items()) {
                if (not declared.contains(key)) {
                    errors.push_back(where + ": " + key + " is not declared in " + pointer);
                    continue;
                }
                walk.seen.insert(pointer + "/properties/" + key);
                walk.keys.push_back(
                    {at_value / key, std::find(required.begin(), required.end(), key) != required.end()});
                validate(pointer + "/properties/" + key, sub, at_value / key, where + "/" + key, errors, walk);
            }
        }
    }

    static bool has_type(json const& value, std::string const& type) {
        if (type == "object") {
            return value.is_object();
        }
        if (type == "array") {
            return value.is_array();
        }
        if (type == "string") {
            return value.is_string();
        }
        if (type == "boolean") {
            return value.is_boolean();
        }
        if (type == "integer") {
            return value.is_number_integer();
        }
        if (type == "number") {
            return value.is_number();
        }
        return false;
    }
};

// Returns every disagreement between the spec and the codec, empty when they match.
inline std::vector<std::string> check_api(std::filesystem::path const& spec_path, Channels const& codecs,
                                          Enums const& enums) {
    std::vector<std::string> errors;
    Spec spec{spec_path};
    const auto channels = spec.channels(errors);

    for (auto const& [address, message] : channels) {
        if (codecs.count(address) == 0) {
            errors.push_back(address + ": in the spec, but no codec handles it");
        }
    }
    for (auto const& [address, codec] : codecs) {
        if (channels.count(address) == 0) {
            errors.push_back(address + ": handled by the codec, but missing from the spec");
        }
    }

    std::set<std::string> declared;
    std::set<std::string> enum_schemas;
    std::set<std::string> visited;
    for (auto const& [address, message] : channels) {
        const auto payload = message + "/payload";
        spec.properties(payload, declared, enum_schemas, visited);
        auto const codec = codecs.find(address);
        if (codec == codecs.end()) {
            continue;
        }

        auto const& examples = spec.at(message).value("examples", json::array());
        if (examples.empty()) {
            errors.push_back(address + ": the spec has no example");
        }
        for (std::size_t i = 0; i < examples.size(); ++i) {
            const auto where = address + " example " + std::to_string(i);
            auto const& example = examples.at(i).at("payload");
            // An example proves nothing about the codec's fields, so it covers nothing.
            spec.validate(payload, example, where, errors, false);
            if (not codec->second.round_trip) {
                continue;
            }
            try {
                const auto back = codec->second.round_trip(example);
                if (not same_payload(example, back)) {
                    errors.push_back(where + ": the codec turns " + example.dump() + " into " + back.dump());
                }
            } catch (std::exception const& e) {
                errors.push_back(where + ": the codec rejects " + example.dump() + ": " + e.what());
            }
        }
        for (std::size_t i = 0; i < codec->second.samples.size(); ++i) {
            const auto where = address + " codec payload " + std::to_string(i);
            auto const& sample = codec->second.samples.at(i);
            const auto keys = spec.validate(payload, sample, where, errors, true);
            if (not codec->second.round_trip) {
                continue;
            }
            try {
                const auto back = codec->second.round_trip(sample);
                if (not same_payload(sample, back)) {
                    errors.push_back(where + ": the codec turns " + sample.dump() + " into " + back.dump());
                }
            } catch (std::exception const& e) {
                errors.push_back(where + ": the codec rejects " + sample.dump() + ": " + e.what());
                continue;
            }
            for (auto const& key : keys) {
                auto without = sample;
                without.at(key.at.parent_pointer()).erase(key.at.back());
                bool rejected = false;
                try {
                    codec->second.round_trip(without);
                } catch (std::exception const&) {
                    rejected = true;
                }
                if (key.required and not rejected) {
                    errors.push_back(where + ": the spec requires " + key.at.to_string() +
                                     ", but the codec accepts the payload without it");
                } else if (not key.required and rejected) {
                    errors.push_back(where + ": the codec requires " + key.at.to_string() +
                                     ", but the spec leaves it optional");
                }
            }
        }
    }

    for (auto const& property : declared) {
        if (spec.covered.count(property) == 0) {
            errors.push_back(property + ": declared, but no codec payload has it");
        }
    }

    const std::string schemas = "/components/schemas/";
    for (auto const& pointer : enum_schemas) {
        const auto name = pointer.rfind(schemas, 0) == 0 ? pointer.substr(schemas.size()) : pointer;
        auto const codec = enums.find(name);
        if (codec == enums.end()) {
            errors.push_back(pointer + ": an enum with no codec values listed");
            continue;
        }
        std::set<std::string> in_spec;
        for (auto const& value : spec.at(pointer).at("enum")) {
            in_spec.insert(value.dump());
        }
        std::set<std::string> in_codec;
        for (auto const& value : codec->second) {
            in_codec.insert(value.dump());
        }
        for (auto const& value : in_spec) {
            if (in_codec.count(value) == 0) {
                errors.push_back(pointer + ": " + value + " is not a codec value");
            }
        }
        for (auto const& value : in_codec) {
            if (in_spec.count(value) == 0) {
                errors.push_back(pointer + ": codec value " + value + " is missing");
            }
        }
    }
    for (auto const& [name, values] : enums) {
        if (enum_schemas.count(schemas + name) == 0) {
            errors.push_back(name + ": codec enum with no schema of that name in the spec");
        }
    }
    return errors;
}

} // namespace spec_drift
