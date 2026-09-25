// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <everest/io/event/unique_fd.hpp>
#include <everest/utils/yaml_loader.hpp>
#include <framework/telemetry.hpp>
#include <utils/telemetry/catalog.hpp>
#include <utils/telemetry/module_telemetry.hpp>
#include <utils/telemetry/transport.hpp>
#include <utils/telemetry/wire.hpp>

#include <tests/helpers.hpp>

using namespace everest::telemetry;
using nlohmann::json;

namespace test_types {
enum class Mode {
    Idle,
    Running,
};

inline void to_json(json& j, const Mode& mode) {
    j = mode == Mode::Idle ? "Idle" : "Running";
}

struct Diagnostics {
    int code;
    std::string text;
};

inline void to_json(json& j, const Diagnostics& diagnostics) {
    j = json{{"code", diagnostics.code}, {"text", diagnostics.text}};
}

std::atomic<int> expensive_serializations{0};

struct Expensive {};

inline void to_json(json& j, const Expensive&) {
    ++expensive_serializations;
    j = json::object();
}
} // namespace test_types

namespace {

class RecordingSender : public DatagramSender {
public:
    SendResult send(const std::uint8_t* data, std::size_t size) override {
        const std::lock_guard lock(mutex);
        datagrams.emplace_back(data, data + size);
        return result;
    }

    std::vector<wire::Sample> samples() {
        const std::lock_guard lock(mutex);
        std::vector<wire::Sample> result_samples;
        for (const auto& datagram : datagrams) {
            auto decoded = wire::decode(datagram.data(), datagram.size());
            REQUIRE(decoded.message.has_value());
            REQUIRE(std::holds_alternative<wire::Sample>(decoded.message.value()));
            result_samples.push_back(std::get<wire::Sample>(decoded.message.value()));
        }
        return result_samples;
    }

    std::mutex mutex;
    std::vector<std::vector<std::uint8_t>> datagrams;
    SendResult result{SendResult::Ok};
};

const json TELEMETRY_MANIFEST = json::parse(R"({
    "temperature": {"kind": "gauge", "type": "number", "unit": "Celsius", "description": "Temperature"},
    "voltage_raw": {"kind": "gauge", "type": "integer", "description": "Raw voltage"},
    "plug_ins": {"kind": "counter", "type": "integer", "description": "Plug-ins"},
    "energy": {"kind": "counter", "type": "number", "unit": "Wh", "description": "Energy"},
    "mode": {"kind": "state", "type": "string", "$ref": "/test_telemetry_types#/Mode", "description": "Mode"},
    "relay_closed": {"kind": "state", "type": "boolean", "description": "Relay"},
    "diagnostics": {"kind": "event", "type": "object", "$ref": "/test_telemetry_types#/Diagnostics",
                    "description": "Diagnostics"},
    "expensive": {"kind": "event", "type": "object", "$ref": "/x#/Expensive", "description": "Expensive"}
})");

ElementDeclarations telemetry_elements() {
    std::vector<std::string> errors;
    auto elements = parse_element_declarations(TELEMETRY_MANIFEST, errors);
    REQUIRE(errors.empty());
    return elements;
}

struct Fixture {
    Fixture() : Fixture(std::make_unique<RecordingSender>()) {
    }

    RecordingSender* sender;
    ModuleTelemetry telemetry;

private:
    explicit Fixture(std::unique_ptr<RecordingSender> owned_sender) :
        sender(owned_sender.get()),
        telemetry(make_module_telemetry("producer_1", telemetry_elements(), std::move(owned_sender))) {
    }
};

std::string make_temp_dir() {
    std::array<char, 40> dir_template{"/tmp/everest_telemetry_test_XXXXXX"};
    REQUIRE(::mkdtemp(dir_template.data()) != nullptr);
    return dir_template.data();
}

} // namespace

TEST_CASE("Telemetry wire format round-trips and rejects invalid datagrams", "[telemetry]") {
    const auto datagram = wire::encode(wire::Sample{"evse_manager_1", "temperature", 1758800000123, 41.2});

    REQUIRE(datagram.size() > wire::HEADER_SIZE);
    CHECK(datagram[0] == 'E');
    CHECK(datagram[1] == 'T');
    CHECK(datagram[2] == wire::VERSION);
    CHECK(datagram[3] == static_cast<std::uint8_t>(wire::Codec::Json));

    CHECK(json::parse(datagram.begin() + wire::HEADER_SIZE, datagram.end()) ==
          json::parse(R"({"t":"s","m":"evse_manager_1","e":"temperature","ts":1758800000123,"v":41.2})"));
    const auto decoded = wire::decode(datagram.data(), datagram.size());
    REQUIRE(decoded.message.has_value());
    CHECK_FALSE(decoded.error.has_value());
    const auto& sample = std::get<wire::Sample>(decoded.message.value());
    CHECK(sample.module_id == "evse_manager_1");
    CHECK(sample.element == "temperature");
    CHECK(sample.timestamp_ms == 1758800000123);
    CHECK(sample.value == 41.2);

    auto broken = datagram;
    CHECK(wire::decode(broken.data(), 3).error == wire::DecodeError::TooShort);
    broken[0] = 'X';
    CHECK(wire::decode(broken.data(), broken.size()).error == wire::DecodeError::BadMagic);
    broken = datagram;
    broken[2] = 2;
    CHECK(wire::decode(broken.data(), broken.size()).error == wire::DecodeError::UnsupportedVersion);
    broken = datagram;
    broken[3] = 2;
    CHECK(wire::decode(broken.data(), broken.size()).error == wire::DecodeError::UnsupportedCodec);
    broken = datagram;
    broken.back() = 'x';
    CHECK(wire::decode(broken.data(), broken.size()).error == wire::DecodeError::MalformedPayload);

    const auto decode_error = [](const json& payload) {
        const auto datagram = wire::encode_payload(payload);
        return wire::decode(datagram.data(), datagram.size()).error;
    };
    CHECK(decode_error(json::array({1, 2})) == wire::DecodeError::MalformedPayload);
    CHECK(decode_error({{"t", "x"}}) == wire::DecodeError::UnknownMessageType);
    CHECK(decode_error({{"t", "s"}, {"m", "m"}, {"e", "e"}, {"ts", "late"}, {"v", 1}}) ==
          wire::DecodeError::MalformedPayload);
    CHECK(decode_error({{"t", "s"}, {"m", "m"}, {"e", "e"}, {"ts", 1}}) == wire::DecodeError::MalformedPayload);
    CHECK(decode_error({{"t", "d"}, {"elements", json::object()}}) == wire::DecodeError::MalformedPayload);
}

TEST_CASE("Telemetry declarations round-trip with the manifest keys", "[telemetry]") {
    wire::Declare declare{"ext:meter", "ExternalMeter", telemetry_elements(), {}};
    const auto datagram = wire::encode(declare);
    const auto payload = json::parse(datagram.begin() + wire::HEADER_SIZE, datagram.end());
    CHECK(payload.at("elements").at("temperature") ==
          json::parse(R"({"kind": "gauge", "type": "number", "unit": "Celsius", "description": "Temperature"})"));

    const auto decoded = wire::decode(datagram.data(), datagram.size());
    const auto& round_trip = std::get<wire::Declare>(decoded.message.value());
    CHECK(round_trip.module_id == "ext:meter");
    CHECK(round_trip.module_type == "ExternalMeter");
    CHECK(round_trip.elements.size() == TELEMETRY_MANIFEST.size());
    CHECK(round_trip.elements.at("mode").type_ref == std::optional<std::string>("/test_telemetry_types#/Mode"));
    CHECK(round_trip.invalid_elements.empty());

    const auto partly_valid = wire::encode_payload(
        {{"t", "d"},
         {"module", "ext:meter"},
         {"elements", {{"ok", TELEMETRY_MANIFEST.at("temperature")}, {"bad", {{"kind", "gauge"}}}}}});
    const auto partial =
        std::get<wire::Declare>(wire::decode(partly_valid.data(), partly_valid.size()).message.value());
    CHECK(partial.elements.size() == 1);
    REQUIRE(partial.invalid_elements.size() == 1);
    CHECK_THAT(partial.invalid_elements.front(), Catch::Matchers::ContainsSubstring("'bad'"));
}

TEST_CASE("Element declarations are validated", "[telemetry]") {
    const auto valid = [](const json& declaration) {
        return std::holds_alternative<ElementDeclaration>(parse_element_declaration("e", declaration));
    };
    CHECK(valid({{"kind", "gauge"}, {"type", "number"}}));
    CHECK(valid({{"kind", "counter"}, {"type", "integer"}}));
    CHECK(valid({{"kind", "state"}, {"type", "boolean"}}));
    CHECK(valid({{"kind", "state"}, {"type", "string"}, {"enum", {"A", "B"}}}));
    CHECK(valid({{"kind", "event"}, {"type", "object"}, {"$ref", "/a#/B"}}));

    CHECK_FALSE(valid({{"kind", "gauge"}, {"type", "string"}}));
    CHECK_FALSE(valid({{"kind", "state"}, {"type", "number"}}));
    CHECK_FALSE(valid({{"kind", "event"}, {"type", "string"}}));
    CHECK_FALSE(valid({{"kind", "histogram"}, {"type", "number"}}));
    CHECK_FALSE(valid({{"kind", "gauge"}}));
    CHECK_FALSE(valid({{"kind", "gauge"}, {"type", "number"}, {"enum", {"A"}}}));
    CHECK_FALSE(valid({{"kind", "state"}, {"type", "string"}, {"enum", json::array()}}));
    CHECK_FALSE(valid(json::array()));

    const auto element = std::get<ElementDeclaration>(parse_element_declaration(
        "temperature", {{"kind", "gauge"}, {"type", "number"}, {"unit", "Celsius"}, {"description", "Temp"}}));
    CHECK(element.name == "temperature");
    CHECK(element.kind == Kind::Gauge);
    CHECK(element.value_type == ValueType::Number);
    CHECK(element.unit == std::optional<std::string>("Celsius"));
    CHECK(element.description == "Temp");
}

TEST_CASE("Inactive telemetry handles do nothing and never serialize", "[telemetry]") {
    test_types::expensive_serializations = 0;
    ModuleTelemetry disabled;
    CHECK_FALSE(disabled.enabled());
    Gauge<double> gauge(disabled, "temperature");
    Counter<std::int64_t> counter(disabled, "plug_ins");
    State<bool> state(disabled, "relay_closed");
    Event<test_types::Expensive> event(disabled, "expensive");

    gauge.set(1.0);
    counter.increase();
    state.set(true);
    event.publish(test_types::Expensive{});
    CHECK(test_types::expensive_serializations == 0);

    Fixture fixture;
    Event<test_types::Expensive> mismatched(fixture.telemetry, "temperature");
    mismatched.publish(test_types::Expensive{});
    CHECK(test_types::expensive_serializations == 0);
    CHECK(fixture.sender->datagrams.empty());

    ModuleTelemetry moved_to(std::move(fixture.telemetry));
    CHECK(moved_to.enabled());
    CHECK_FALSE(fixture.telemetry.enabled()); // NOLINT(bugprone-use-after-move)
    Gauge<double> on_moved_from(fixture.telemetry, "temperature");
    on_moved_from.set(1.0);
    CHECK(fixture.sender->datagrams.empty());
    CHECK(fixture.telemetry.statistics().sent == 0);
}

TEST_CASE("Telemetry handles bind only to matching manifest elements", "[telemetry]") {
    Fixture fixture;
    CHECK(fixture.telemetry.bind("temperature", Kind::Gauge, ValueType::Number) != nullptr);
    CHECK(fixture.telemetry.bind("temperature", Kind::Gauge, ValueType::Integer) == nullptr);
    CHECK(fixture.telemetry.bind("temperature", Kind::Counter, ValueType::Number) == nullptr);
    CHECK(fixture.telemetry.bind("unknown", Kind::Gauge, ValueType::Number) == nullptr);
    CHECK(fixture.telemetry.bind("mode", Kind::State, ValueType::String) != nullptr);
    CHECK(fixture.telemetry.bind("relay_closed", Kind::State, ValueType::Boolean) != nullptr);
    CHECK(fixture.telemetry.bind("diagnostics", Kind::Event, ValueType::Object) != nullptr);
}

TEST_CASE("Telemetry handles publish samples", "[telemetry]") {
    Fixture fixture;
    Gauge<double> temperature(fixture.telemetry, "temperature");
    Gauge<std::int64_t> voltage_raw(fixture.telemetry, "voltage_raw");
    State<test_types::Mode> mode(fixture.telemetry, "mode");
    State<bool> relay_closed(fixture.telemetry, "relay_closed");
    Event<test_types::Diagnostics> diagnostics(fixture.telemetry, "diagnostics");

    const auto before = std::chrono::system_clock::now();
    temperature.set(41.5);
    voltage_raw.set(4095);
    mode.set(test_types::Mode::Running);
    relay_closed.set(true);
    diagnostics.publish({7, "overtemperature"});
    const auto measured = std::chrono::system_clock::time_point(std::chrono::milliseconds(1700000000000));
    temperature.set(40.0, {measured});

    const auto samples = fixture.sender->samples();
    REQUIRE(samples.size() == 6);
    for (const auto& sample : samples) {
        CHECK(sample.module_id == "producer_1");
    }
    CHECK(samples[0].element == "temperature");
    CHECK(samples[0].value == 41.5);
    CHECK(samples[0].timestamp_ms >=
          std::chrono::duration_cast<std::chrono::milliseconds>(before.time_since_epoch()).count());
    CHECK(samples[1].value == 4095);
    CHECK(samples[2].value == "Running");
    CHECK(samples[3].value == true);
    CHECK(samples[4].value == json({{"code", 7}, {"text", "overtemperature"}}));
    CHECK(samples[5].timestamp_ms == 1700000000000);
    CHECK(fixture.telemetry.statistics().sent == 6);
}

TEST_CASE("Telemetry counters publish running totals", "[telemetry]") {
    Fixture fixture;
    Counter<std::int64_t> plug_ins(fixture.telemetry, "plug_ins");
    Counter<double> energy(fixture.telemetry, "energy");

    plug_ins.increase();
    plug_ins.increase(2);
    plug_ins.increase(-5);
    energy.increase(1.5);
    energy.increase(2.25);
    energy.increase(std::numeric_limits<double>::quiet_NaN());

    const auto samples = fixture.sender->samples();
    REQUIRE(samples.size() == 4);
    CHECK(samples[0].value == 1);
    CHECK(samples[1].value == 3);
    CHECK(samples[2].value == 1.5);
    CHECK(samples[3].value == 3.75);
}

TEST_CASE("Telemetry counter totals are exact under concurrency", "[telemetry]") {
    Fixture fixture;
    Counter<std::int64_t> plug_ins(fixture.telemetry, "plug_ins");
    constexpr int threads = 8;
    constexpr int increments = 10000;

    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i) {
        workers.emplace_back([&plug_ins] {
            for (int j = 0; j < increments; ++j) {
                plug_ins.increase();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    std::int64_t highest = 0;
    for (const auto& sample : fixture.sender->samples()) {
        highest = std::max(highest, sample.value.get<std::int64_t>());
    }
    CHECK(highest == threads * increments);
    CHECK(fixture.telemetry.statistics().sent == threads * increments);
}

TEST_CASE("Telemetry drops are counted, not retried", "[telemetry]") {
    Fixture fixture;
    Gauge<double> temperature(fixture.telemetry, "temperature");

    temperature.set(std::nan(""));
    temperature.set(std::numeric_limits<double>::infinity());
    fixture.sender->result = SendResult::WouldBlock;
    temperature.set(1.0);
    fixture.sender->result = SendResult::NoReceiver;
    temperature.set(2.0);
    fixture.sender->result = SendResult::Ok;

    Event<std::string> oversized(fixture.telemetry, "diagnostics");
    oversized.publish(std::string(wire::MAX_DATAGRAM_SIZE, 'x'));

    const auto statistics = fixture.telemetry.statistics();
    CHECK(statistics.dropped_invalid_value == 2);
    CHECK(statistics.dropped_would_block == 1);
    CHECK(statistics.dropped_no_receiver == 1);
    CHECK(statistics.dropped_too_big == 1);
    CHECK(statistics.sent == 0);
    CHECK(fixture.sender->datagrams.size() == 2);
}

TEST_CASE("Module telemetry is only created when enabled and declared", "[telemetry]") {
    const auto path = make_temp_dir() + "/telemetry.sock";
    CHECK_FALSE(make_module_telemetry("m", telemetry_elements(), false, path).enabled());
    CHECK_FALSE(make_module_telemetry("m", ElementDeclarations{}, true, path).enabled());
    CHECK(make_module_telemetry("m", telemetry_elements(), true, path).enabled());
    CHECK_FALSE(make_module_telemetry("m", telemetry_elements(), true, std::string(200, 'x')).enabled());
}

TEST_CASE("Unix datagram sender delivers without blocking", "[telemetry]") {
    const auto dir = make_temp_dir();
    const auto path = dir + "/telemetry.sock";

    SECTION("No receiver") {
        auto sender = make_uds_datagram_sender(path);
        const std::array<std::uint8_t, 4> data{'E', 'T', 1, 1};
        CHECK(sender->send(data.data(), data.size()) == SendResult::NoReceiver);
    }

    SECTION("Delivery, full queue and oversized datagrams") {
        const everest::lib::io::event::unique_fd receiver(bind_receiver_socket(path, 0600));
        auto sender = make_uds_datagram_sender(path);

        const auto datagram = wire::encode(wire::Sample{"m", "e", 1, 1});
        REQUIRE(sender->send(datagram.data(), datagram.size()) == SendResult::Ok);
        std::array<std::uint8_t, 256> buffer{};
        const auto received = ::recv(receiver, buffer.data(), buffer.size(), 0);
        REQUIRE(received == static_cast<ssize_t>(datagram.size()));
        CHECK(std::get<wire::Sample>(wire::decode(buffer.data(), received).message.value()).element == "e");

        auto result = SendResult::Ok;
        for (int i = 0; i < 100000 and result == SendResult::Ok; ++i) {
            const auto start = std::chrono::steady_clock::now();
            result = sender->send(datagram.data(), datagram.size());
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1));
        }
        CHECK(result == SendResult::WouldBlock);

        const std::vector<std::uint8_t> huge(4 * 1024 * 1024, 'x');
        CHECK(sender->send(huge.data(), huge.size()) == SendResult::TooBig);
    }

    ::unlink(path.c_str());
    ::rmdir(dir.c_str());
}

TEST_CASE("The receiver socket is bound with its mode and replaces only stale sockets", "[telemetry]") {
    const auto dir = make_temp_dir();
    const auto path = dir + "/telemetry.sock";

    { const everest::lib::io::event::unique_fd stale(bind_receiver_socket(path, 0600)); }
    const everest::lib::io::event::unique_fd receiver(bind_receiver_socket(path, 0660));
    struct stat st {};
    REQUIRE(::stat(path.c_str(), &st) == 0);
    CHECK(S_ISSOCK(st.st_mode));
    CHECK((st.st_mode & 0777) == 0660);
    CHECK((::fcntl(receiver, F_GETFD) & FD_CLOEXEC) != 0);
    CHECK(bound_receiver_socket_path(receiver) == std::optional<std::string>(path));

    const auto file = dir + "/file";
    ::close(::open(file.c_str(), O_CREAT | O_WRONLY, 0600));
    CHECK_THROWS_AS(bind_receiver_socket(file, 0600), std::runtime_error);
    REQUIRE(::stat(file.c_str(), &st) == 0);
    CHECK(S_ISREG(st.st_mode));

    CHECK_THROWS_AS(bind_receiver_socket("", 0600), std::runtime_error);
    CHECK_THROWS_AS(bind_receiver_socket(dir + "/" + std::string(200, 'x'), 0600), std::runtime_error);
    CHECK_THROWS_AS(make_uds_datagram_sender(std::string(200, 'x')), std::runtime_error);

    ::unlink(file.c_str());
    ::unlink(path.c_str());
    ::rmdir(dir.c_str());
}

TEST_CASE("Only bound unix datagram sockets count as receiver sockets", "[telemetry]") {
    const everest::lib::io::event::unique_fd unbound(::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    CHECK_FALSE(bound_receiver_socket_path(unbound).has_value());

    const everest::lib::io::event::unique_fd stream(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    CHECK_FALSE(bound_receiver_socket_path(stream).has_value());

    const int file = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(file >= 0);
    CHECK_FALSE(bound_receiver_socket_path(file).has_value());
    ::close(file);
    CHECK_FALSE(bound_receiver_socket_path(file).has_value());
    CHECK_FALSE(bound_receiver_socket_path(-1).has_value());
}

TEST_CASE("Telemetry catalog collects declarations, mappings and enum values", "[telemetry]") {
    const auto bin_dir = Everest::tests::get_bin_dir() / "valid_telemetry";
    const json manifests = {{"TESTValidManifestTelemetry",
                             Everest::load_yaml(bin_dir / "modules" / "TESTValidManifestTelemetry" / "manifest.yaml")},
                            {"TESTTelemetryReceiver", json::object()}};
    const std::map<std::string, std::string, std::less<>> module_names = {{"producer_1", "TESTValidManifestTelemetry"},
                                                                          {"producer_2", "TESTValidManifestTelemetry"},
                                                                          {"receiver", "TESTTelemetryReceiver"}};
    const MappingLookup mapping = [](const std::string& module_id) -> std::optional<Mapping> {
        if (module_id == "producer_1") {
            return Mapping(1, 2);
        }
        return std::nullopt;
    };

    const auto catalog =
        build_telemetry_catalog(manifests, module_names, mapping, make_types_dir_enum_resolver(bin_dir / "types"));

    REQUIRE(catalog.size() == 2);
    const auto& producer = catalog.at("producer_1");
    CHECK(producer.module_type == "TESTValidManifestTelemetry");
    REQUIRE(producer.mapping.has_value());
    CHECK(producer.mapping->evse == 1);
    CHECK(producer.mapping->connector == std::optional<int>(2));
    CHECK_FALSE(catalog.at("producer_2").mapping.has_value());
    CHECK(producer.elements.size() == 8);
    CHECK(producer.elements.at("temperature").unit == std::optional<std::string>("Celsius"));
    CHECK(producer.elements.at("fw_state").enum_values == std::vector<std::string>{"Idle", "Measuring", "Error"});
    CHECK(producer.elements.at("mode").enum_values == std::vector<std::string>{"Idle", "Running", "Fault"});
    CHECK(producer.elements.at("diagnostics").enum_values.empty());
    CHECK(producer.elements.at("diagnostics").kind == Kind::Event);

    const json serialized = catalog;
    const auto round_trip = serialized.get<TelemetryCatalog>();
    REQUIRE(round_trip.size() == 2);
    CHECK(round_trip.at("producer_1").mapping->connector == std::optional<int>(2));
    CHECK(round_trip.at("producer_1").elements.at("mode").enum_values ==
          std::vector<std::string>{"Idle", "Running", "Fault"});
    CHECK(round_trip.at("producer_1").elements.at("temperature").name == "temperature");
    CHECK_FALSE(round_trip.at("producer_2").mapping.has_value());

    auto broken = serialized;
    broken["producer_1"]["elements"]["temperature"]["type"] = "string";
    CHECK_THROWS(broken.get<TelemetryCatalog>());
}
