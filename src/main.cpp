#include "Config.h"
#include "LoadBalancer.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::uint64_t readPositiveInteger(std::istream& input,
                                  std::ostream& output,
                                  std::string_view prompt) {
    for (;;) {
        output << prompt;
        std::string line;
        if (!std::getline(input, line)) {
            throw std::runtime_error("input ended before a positive integer was provided");
        }

        line = trim(std::move(line));
        std::uint64_t value = 0;
        const auto result = std::from_chars(line.data(), line.data() + line.size(), value);
        if (!line.empty() && result.ec == std::errc{} &&
            result.ptr == line.data() + line.size() && value > 0) {
            return value;
        }
        output << "Please enter a positive whole number.\n";
    }
}

OutputMode readOutputMode(std::istream& input,
                          std::ostream& output,
                          OutputMode defaultMode) {
    for (;;) {
        output << "Output mode [quiet/verbose] (default " << toString(defaultMode)
               << "): ";
        std::string line;
        if (!std::getline(input, line)) {
            throw std::runtime_error("input ended before an output mode was provided");
        }

        line = trim(std::move(line));
        if (line.empty()) {
            return defaultMode;
        }

        try {
            return parseOutputMode(line);
        } catch (const std::invalid_argument&) {
            output << "Please enter quiet (q) or verbose (v).\n";
        }
    }
}

std::size_t checkedServerCount(std::uint64_t value) {
    if (value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("server count is too large for this platform");
    }
    return static_cast<std::size_t>(value);
}

std::size_t checkedInitialRequestCount(std::size_t serverCount,
                                       std::size_t requestsPerServer) {
    if (serverCount > std::numeric_limits<std::size_t>::max() / requestsPerServer) {
        throw std::invalid_argument("initial request count would overflow");
    }
    return serverCount * requestsPerServer;
}
}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [config-file]\n";
        return 2;
    }

    const std::string configPath = argc == 2 ? argv[1] : "config.txt";

    try {
        SimulationConfig config = SimulationConfig::loadFromFile(configPath);
        config.validate();

        std::cout << "CSCE 412 Project 4 - Load Balancer Simulation\n";
        const std::size_t initialServerCount = checkedServerCount(
            readPositiveInteger(std::cin, std::cout, "Starting web servers: "));
        const std::uint64_t clockCycles = readPositiveInteger(
            std::cin, std::cout, "Clock cycles to simulate: ");
        const OutputMode outputMode =
            readOutputMode(std::cin, std::cout, config.defaultOutputMode);

        LoadBalancer loadBalancer(initialServerCount, config, outputMode);
        const std::size_t initialRequestTarget = checkedInitialRequestCount(
            initialServerCount, config.requestsPerStartingServer);

        std::cout << "\n=== Simulation Start ===\n"
                  << "Configuration file: " << configPath << '\n'
                  << "Output mode: " << toString(outputMode) << '\n'
                  << "Starting servers: " << initialServerCount << '\n'
                  << "Starting queue size: " << initialRequestTarget << '\n'
                  << "Clock cycles requested: " << clockCycles << '\n'
                  << "Processing-time range: " << config.minimumProcessingTime
                  << '-' << config.maximumProcessingTime << " cycles\n"
                  << "Random seed: " << config.randomSeed << '\n'
                  << "Blocked IP ranges: " << config.blockedSourceRanges.size()
                  << "\n\n";

        loadBalancer.initializeRequestQueue(initialRequestTarget, std::cout);

        for (std::uint64_t cycle = 0; cycle < clockCycles; ++cycle) {
            loadBalancer.runCycle(std::cout);
        }

        loadBalancer.printFinalSummary(std::cout);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Simulation error: " << error.what() << '\n';
        return 1;
    }
}
