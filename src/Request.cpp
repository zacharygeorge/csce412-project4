#include "Request.h"

#include <string_view>
#include <stdexcept>
#include <utility>

namespace {
bool isValidIpv4Address(std::string_view address) {
    if (address.empty()) {
        return false;
    }

    std::size_t octetStart = 0;
    int octetCount = 0;
    while (octetStart < address.size()) {
        const std::size_t dot = address.find('.', octetStart);
        const std::size_t octetEnd =
            dot == std::string_view::npos ? address.size() : dot;
        const std::size_t octetLength = octetEnd - octetStart;
        if (octetLength == 0 || octetLength > 3) {
            return false;
        }

        int octetValue = 0;
        for (std::size_t index = octetStart; index < octetEnd; ++index) {
            const char character = address[index];
            if (character < '0' || character > '9') {
                return false;
            }
            octetValue = octetValue * 10 + (character - '0');
        }
        if (octetValue > 255) {
            return false;
        }

        ++octetCount;
        if (dot == std::string_view::npos) {
            break;
        }
        octetStart = dot + 1;
    }

    return octetCount == 4 && address.back() != '.';
}

std::string generateRandomIpv4Address(std::mt19937& randomEngine) {
    std::uniform_int_distribution<int> octetDistribution(0, 255);
    std::string address;
    for (int octet = 0; octet < 4; ++octet) {
        if (octet > 0) {
            address += '.';
        }
        address += std::to_string(octetDistribution(randomEngine));
    }
    return address;
}
}  // namespace

Request::Request(std::string sourceIp,
                 std::string destinationIp,
                 int processingTime,
                 char jobType)
    : sourceIp_(std::move(sourceIp)),
      destinationIp_(std::move(destinationIp)),
      processingTime_(processingTime),
      jobType_(jobType) {
    if (!isValidIpv4Address(sourceIp_)) {
        throw std::invalid_argument("request source IP must be a valid IPv4 address");
    }
    if (!isValidIpv4Address(destinationIp_)) {
        throw std::invalid_argument(
            "request destination IP must be a valid IPv4 address");
    }
    if (processingTime_ <= 0) {
        throw std::invalid_argument("request processing time must be positive");
    }
    if (!isSupportedJobType(jobType_)) {
        throw std::invalid_argument("request job type must be P or S");
    }
}

Request Request::generateRandom(std::mt19937& randomEngine,
                                int minimumProcessingTime,
                                int maximumProcessingTime) {
    if (minimumProcessingTime <= 0 ||
        maximumProcessingTime < minimumProcessingTime) {
        throw std::invalid_argument(
            "random request processing times must define a positive, ordered range");
    }

    std::uniform_int_distribution<int> processingTimeDistribution(
        minimumProcessingTime, maximumProcessingTime);
    std::uniform_int_distribution<int> jobTypeDistribution(0, 1);

    const std::string sourceIp = generateRandomIpv4Address(randomEngine);
    const std::string destinationIp = generateRandomIpv4Address(randomEngine);
    const int processingTime = processingTimeDistribution(randomEngine);
    const char jobType = jobTypeDistribution(randomEngine) == 0 ? 'P' : 'S';
    return Request(sourceIp, destinationIp, processingTime, jobType);
}

const std::string& Request::sourceIp() const noexcept {
    return sourceIp_;
}

const std::string& Request::destinationIp() const noexcept {
    return destinationIp_;
}

int Request::processingTime() const noexcept {
    return processingTime_;
}

char Request::jobType() const noexcept {
    return jobType_;
}

bool Request::isSupportedJobType(char jobType) noexcept {
    return jobType == 'P' || jobType == 'S';
}
