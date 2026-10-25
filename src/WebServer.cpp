#include "WebServer.h"
#include "NotImplementedError.h"

#include <stdexcept>

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

void WebServer::acceptRequest(Request) {
    throw NotImplementedError("WebServer::acceptRequest");
}

std::optional<Request> WebServer::advanceOneCycle() {
    throw NotImplementedError("WebServer::advanceOneCycle");
}
