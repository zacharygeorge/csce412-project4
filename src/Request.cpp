#include "Request.h"
#include "NotImplementedError.h"

#include <stdexcept>
#include <utility>

Request::Request(std::string sourceIp,
                 std::string destinationIp,
                 int processingTime,
                 char jobType)
    : sourceIp_(std::move(sourceIp)),
      destinationIp_(std::move(destinationIp)),
      processingTime_(processingTime),
      jobType_(jobType) {
    if (processingTime_ <= 0) {
        throw std::invalid_argument("request processing time must be positive");
    }
    if (!isSupportedJobType(jobType_)) {
        throw std::invalid_argument("request job type must be P or S");
    }
}

Request Request::generateRandom(std::mt19937&, int, int) {
    throw NotImplementedError("Request::generateRandom");
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
