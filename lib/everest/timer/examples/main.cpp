// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>
#include <date/date.h>
#include <date/tz.h>

#include <everest/timer.hpp>

namespace po = boost::program_options;

using date::operator<<;
using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
    po::options_description desc("EVerest::time example");
    desc.add_options()("help,h", "produce help message");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help") != 0) {
        std::cout << desc << "\n";
        return 1;
    }

    boost::asio::io_context io_context;

    std::cout << "start time: " << date::utc_clock::now() << std::endl;

    int count_timer1 = 0;
    int count_timer2 = 0;
    auto timer0 = std::make_unique<Everest::SteadyTimer>(&io_context);
    timer0->timeout([]() { std::cout << "Goodbye after 25s" << std::endl; }, 25s);
    auto timer1 = std::make_unique<Everest::SteadyTimer>(&io_context);
    timer1->at(
        [&]() {
            std::cout << "timer1 (asio) after 5s: " << date::utc_clock::now() << std::endl;
            timer1->interval(
                [&]() {
                    std::cout << "timer1 (asio) interval (1s): " << date::utc_clock::now() << std::endl;
                    count_timer1++;
                    if (count_timer1 > 3) {
                        timer1->timeout(
                            []() {
                                std::cout << "timer1 (asio) timeout (3s): " << date::utc_clock::now() << std::endl;
                            },
                            3s);
                    }
                },
                1s);
        },
        date::utc_clock::now() + 5s);

    auto timer2 = std::make_unique<Everest::SteadyTimer>();
    timer2->at(
        [&]() {
            std::cout << "timer2 (thread) after 12s: " << date::utc_clock::now() << std::endl;
            timer2->interval(
                [&]() {
                    std::cout << "timer2 (thread) interval (1s): " << date::utc_clock::now() << std::endl;
                    count_timer2++;
                    if (count_timer2 > 3) {
                        timer2->timeout(
                            []() {
                                std::cout << "timer2 (thread) timeout (3s): " << date::utc_clock::now() << std::endl;
                            },
                            3s);
                    }
                },
                1s);
        },
        date::utc_clock::now() + 12s);

    io_context.run();

    return 0;
}
