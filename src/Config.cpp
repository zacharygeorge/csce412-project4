#include "Config.h"
#include "NotImplementedError.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

bool IpRange::contains(std::string_view) const {
    throw NotImplementedError("IpRange::contains");
}

SimulationConfig SimulationConfig::loadFromFile(const std::string&) {
    throw NotImplementedError("SimulationConfig::loadFromFile");
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
    if (arrivalProbability < 0.0 || arrivalProbability > 1.0) {
        throw std::invalid_argument("arrival probability must be between 0 and 1");
    }
    if (maximumRequestsPerArrival == 0) {
        throw std::invalid_argument("maximum requests per arrival must be positive");
    }
    if (requestsPerStartingServer == 0) {
        throw std::invalid_argument("initial requests per server must be positive");
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
