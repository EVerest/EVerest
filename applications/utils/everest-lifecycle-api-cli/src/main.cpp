// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <boost/program_options.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command_handlers.hpp"
#include "everest_api_types/utilities/Topics.hpp"
#include "mqtt_lifecycle_service_client.hpp"

namespace po = boost::program_options;
using namespace everest::lifecycle_cli;

int main(int argc, char** argv) {
    std::string host;
    int port;
    std::string api_name;

    po::options_description global("Global options");
    global.add_options()("help,h", "This help message")(
        "host", po::value<std::string>(&host)->default_value("localhost"),
        "MQTT broker host")("port", po::value<int>(&port)->default_value(1883), "MQTT broker port")(
        "api-name", po::value<std::string>(&api_name)->default_value("lifecycle"),
        "API name to form the MQTT topic (e.g., lifecycle)")("command", po::value<std::string>(), "Command to execute")(
        "cmd_args", po::value<std::vector<std::string>>(), "Arguments for command");

    po::positional_options_description pos;
    pos.add("command", 1).add("cmd_args", -1);

    po::variables_map vm;
    try {
        po::parsed_options parsed =
            po::command_line_parser(argc, argv).options(global).positional(pos).allow_unregistered().run();
        po::store(parsed, vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    if (vm.count("help") || !vm.count("command")) {
        std::cout << "Usage: everest-lifecycle-api-cli [options] <command> [args...]\n\n";
        std::cout << global << "\n";
        std::cout << "Commands:\n"
                  << "  stop_modules\n"
                  << "  start_modules\n"
                  << "  get_everest_version\n"
                  << "  monitor\n";
        return 0;
    }

    std::string command = vm["command"].as<std::string>();
    std::vector<std::string> cmd_args = po::collect_unrecognized(
        po::command_line_parser(argc, argv).options(global).positional(pos).allow_unregistered().run().options,
        po::include_positional);
    // Remove the command itself from cmd_args
    if (!cmd_args.empty()) {
        cmd_args.erase(cmd_args.begin());
    }

    everest::lib::API::Topics topics;
    topics.setup("_", api_name, 0);

    try {
        auto client = std::make_shared<MqttLifecycleServiceClient>(host, port, topics);
        CommandHandlers handlers(client);

        if (command == "stop_modules") {
            handlers.stop_modules();
        } else if (command == "start_modules") {
            handlers.start_modules();
        } else if (command == "get_everest_version") {
            handlers.get_everest_version();
        } else if (command == "monitor") {
            handlers.monitor();
        } else {
            std::cerr << "Unknown command: " << command << "\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
