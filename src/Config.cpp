#include "Config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace {
std::string trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::invalid_argument invalidIpv4(std::string_view address) {
    return std::invalid_argument("invalid IPv4 address: " + std::string(address));
}

std::uint32_t ipv4ToInteger(std::string_view address) {
    if (address.empty()) {
        throw invalidIpv4(address);
    }

    std::uint32_t result = 0;
    std::size_t octetStart = 0;
    for (int octetIndex = 0; octetIndex < 4; ++octetIndex) {
        const std::size_t dot = address.find('.', octetStart);
        const bool finalOctet = octetIndex == 3;
        if ((!finalOctet && dot == std::string_view::npos) ||
            (finalOctet && dot != std::string_view::npos)) {
            throw invalidIpv4(address);
        }

        const std::size_t octetEnd = finalOctet ? address.size() : dot;
        const std::size_t octetLength = octetEnd - octetStart;
        if (octetLength == 0 || octetLength > 3) {
            throw invalidIpv4(address);
        }

        unsigned int octetValue = 0;
        for (std::size_t index = octetStart; index < octetEnd; ++index) {
            const char character = address[index];
            if (character < '0' || character > '9') {
                throw invalidIpv4(address);
            }
            octetValue = octetValue * 10U +
                         static_cast<unsigned int>(character - '0');
        }
        if (octetValue > 255U) {
            throw invalidIpv4(address);
        }

        result = (result << 8U) | octetValue;
        octetStart = octetEnd + 1;
    }
    return result;
}

[[noreturn]] void throwConfigError(const std::string& path,
                                   std::size_t lineNumber,
                                   const std::string& message) {
    throw std::runtime_error(path + ":" + std::to_string(lineNumber) +
                             ": " + message);
}

template <typename Integer>
Integer parseUnsigned(const std::string& value,
                      const std::string& path,
                      std::size_t lineNumber,
                      const std::string& key) {
    unsigned long long parsed = 0;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || result.ec != std::errc{} ||
        result.ptr != value.data() + value.size() ||
        parsed > static_cast<unsigned long long>(
                     std::numeric_limits<Integer>::max())) {
        throwConfigError(path, lineNumber,
                         "invalid nonnegative integer for " + key + ": " + value);
    }
    return static_cast<Integer>(parsed);
}

int parseInteger(const std::string& value,
                 const std::string& path,
                 std::size_t lineNumber,
                 const std::string& key) {
    int parsed = 0;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || result.ec != std::errc{} ||
        result.ptr != value.data() + value.size()) {
        throwConfigError(path, lineNumber,
                         "invalid integer for " + key + ": " + value);
    }
    return parsed;
}

double parseProbability(const std::string& value,
                        const std::string& path,
                        std::size_t lineNumber) {
    try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size()) {
            throwConfigError(path, lineNumber,
                             "invalid number for arrival_probability: " + value);
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throwConfigError(path, lineNumber,
                         "invalid number for arrival_probability: " + value);
    } catch (const std::out_of_range&) {
        throwConfigError(path, lineNumber,
                         "arrival_probability is out of range: " + value);
    }
}

IpRange parseIpRange(const std::string& value,
                     const std::string& path,
                     std::size_t lineNumber) {
    const std::size_t separator = value.find('-');
    if (separator == std::string::npos ||
        value.find('-', separator + 1) != std::string::npos) {
        throwConfigError(path, lineNumber,
                         "blocked_source_range must be first-ip-last-ip");
    }

    IpRange range{trim(std::string_view(value).substr(0, separator)),
                  trim(std::string_view(value).substr(separator + 1))};
    try {
        const std::uint32_t first = ipv4ToInteger(range.firstAddress);
        const std::uint32_t last = ipv4ToInteger(range.lastAddress);
        if (first > last) {
            throw std::invalid_argument("first address is greater than last address");
        }
    } catch (const std::invalid_argument& error) {
        throwConfigError(path, lineNumber,
                         "invalid blocked_source_range: " +
                             std::string(error.what()));
    }
    return range;
}
}  // namespace

bool IpRange::contains(std::string_view address) const {
    const std::uint32_t first = ipv4ToInteger(firstAddress);
    const std::uint32_t last = ipv4ToInteger(lastAddress);
    if (first > last) {
        throw std::invalid_argument(
            "IP range first address must not exceed its last address");
    }
    const std::uint32_t candidate = ipv4ToInteger(address);
    return candidate >= first && candidate <= last;
}

SimulationConfig SimulationConfig::loadFromFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("unable to open configuration file: " + path);
    }

    SimulationConfig config;
    std::set<std::string> seenKeys;
    const std::array<std::string, 10> requiredKeys = {
        "minimum_processing_time",
        "maximum_processing_time",
        "scale_up_requests_per_server",
        "scale_down_requests_per_server",
        "scaling_cooldown_cycles",
        "arrival_probability",
        "maximum_requests_per_arrival",
        "requests_per_starting_server",
        "random_seed",
        "default_output_mode",
    };

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos ||
            line.find('=', equals + 1) != std::string::npos) {
            throwConfigError(path, lineNumber,
                             "expected exactly one key=value assignment");
        }
        const std::string key = trim(std::string_view(line).substr(0, equals));
        const std::string value = trim(std::string_view(line).substr(equals + 1));
        if (key.empty() || value.empty()) {
            throwConfigError(path, lineNumber, "configuration key and value are required");
        }

        if (key == "blocked_source_range") {
            config.blockedSourceRanges.push_back(
                parseIpRange(value, path, lineNumber));
            continue;
        }
        if (std::find(requiredKeys.begin(), requiredKeys.end(), key) ==
            requiredKeys.end()) {
            throwConfigError(path, lineNumber,
                             "unknown configuration setting: " + key);
        }
        if (!seenKeys.insert(key).second) {
            throwConfigError(path, lineNumber,
                             "duplicate configuration setting: " + key);
        }

        if (key == "minimum_processing_time") {
            config.minimumProcessingTime =
                parseInteger(value, path, lineNumber, key);
        } else if (key == "maximum_processing_time") {
            config.maximumProcessingTime =
                parseInteger(value, path, lineNumber, key);
        } else if (key == "scale_up_requests_per_server") {
            config.scaleUpRequestsPerServer =
                parseUnsigned<std::size_t>(value, path, lineNumber, key);
        } else if (key == "scale_down_requests_per_server") {
            config.scaleDownRequestsPerServer =
                parseUnsigned<std::size_t>(value, path, lineNumber, key);
        } else if (key == "scaling_cooldown_cycles") {
            config.scalingCooldownCycles =
                parseUnsigned<std::uint64_t>(value, path, lineNumber, key);
        } else if (key == "arrival_probability") {
            config.arrivalProbability =
                parseProbability(value, path, lineNumber);
        } else if (key == "maximum_requests_per_arrival") {
            config.maximumRequestsPerArrival =
                parseUnsigned<std::size_t>(value, path, lineNumber, key);
        } else if (key == "requests_per_starting_server") {
            config.requestsPerStartingServer =
                parseUnsigned<std::size_t>(value, path, lineNumber, key);
        } else if (key == "random_seed") {
            config.randomSeed =
                parseUnsigned<std::uint32_t>(value, path, lineNumber, key);
        } else if (key == "default_output_mode") {
            try {
                config.defaultOutputMode = parseOutputMode(value);
            } catch (const std::invalid_argument& error) {
                throwConfigError(path, lineNumber, error.what());
            }
        }
    }

    for (const std::string& key : requiredKeys) {
        if (seenKeys.find(key) == seenKeys.end()) {
            throw std::runtime_error("configuration file is missing required setting: " +
                                     key);
        }
    }

    try {
        config.validate();
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error("invalid configuration in " + path + ": " +
                                 error.what());
    }
    return config;
}

void SimulationConfig::validate() const {
    if (minimumProcessingTime <= 0 ||
        maximumProcessingTime < minimumProcessingTime) {
        throw std::invalid_argument(
            "processing times must define a positive, ordered range");
    }
    if (scaleDownRequestsPerServer == 0 ||
        scaleDownRequestsPerServer >= scaleUpRequestsPerServer) {
        throw std::invalid_argument(
            "scaling thresholds must be positive and ordered");
    }
    if (scalingCooldownCycles == 0) {
        throw std::invalid_argument("scaling cooldown must be positive");
    }
    if (!std::isfinite(arrivalProbability) || arrivalProbability < 0.0 ||
        arrivalProbability > 1.0) {
        throw std::invalid_argument("arrival probability must be between 0 and 1");
    }
    if (maximumRequestsPerArrival == 0) {
        throw std::invalid_argument("maximum requests per arrival must be positive");
    }
    if (requestsPerStartingServer == 0) {
        throw std::invalid_argument("initial requests per server must be positive");
    }
    for (const IpRange& range : blockedSourceRanges) {
        const std::uint32_t first = ipv4ToInteger(range.firstAddress);
        const std::uint32_t last = ipv4ToInteger(range.lastAddress);
        if (first > last) {
            throw std::invalid_argument(
                "blocked IP range first address must not exceed its last address");
        }
    }
}

OutputMode parseOutputMode(std::string_view text) {
    std::string normalized(text);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });

    if (normalized == "q" || normalized == "quiet") {
        return OutputMode::Quiet;
    }
    if (normalized == "v" || normalized == "verbose") {
        return OutputMode::Verbose;
    }
    throw std::invalid_argument("mode must be quiet (q) or verbose (v)");
}

std::string_view toString(OutputMode mode) noexcept {
    return mode == OutputMode::Verbose ? "verbose" : "quiet";
}
