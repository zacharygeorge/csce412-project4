#include "WebServer.h"

#include <stdexcept>
#include <utility>

WebServer::WebServer(std::size_t identifier) : identifier_(identifier) {
    if (identifier_ == 0) {
        throw std::invalid_argument("web server identifier must be positive");
    }
}

std::size_t WebServer::identifier() const noexcept {
    return identifier_;
}

bool WebServer::isBusy() const noexcept {
    return currentRequest_.has_value();
}

bool WebServer::isIdle() const noexcept {
    return !isBusy();
}

int WebServer::remainingProcessingTime() const noexcept {
    return remainingProcessingTime_;
}

const std::optional<Request>& WebServer::currentRequest() const noexcept {
    return currentRequest_;
}

void WebServer::acceptRequest(Request request) {
    if (isBusy()) {
        throw std::logic_error("cannot assign a request to a busy web server");
    }

    currentRequest_.emplace(std::move(request));
    remainingProcessingTime_ = currentRequest_->processingTime();
}

std::optional<Request> WebServer::advanceOneCycle() {
    if (isIdle()) {
        return std::nullopt;
    }

    --remainingProcessingTime_;
    if (remainingProcessingTime_ > 0) {
        return std::nullopt;
    }

    std::optional<Request> completedRequest = std::move(currentRequest_);
    currentRequest_.reset();
    remainingProcessingTime_ = 0;
    return completedRequest;
}
