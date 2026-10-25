#include "LoadBalancer.h"
#include "NotImplementedError.h"

#include <algorithm>
#include <ostream>
#include <stdexcept>
#include <utility>

LoadBalancer::LoadBalancer(std::size_t initialServerCount,
                           SimulationConfig config,
                           OutputMode outputMode)
    : config_(std::move(config)),
      outputMode_(outputMode),
      randomEngine_(config_.randomSeed) {
    if (initialServerCount == 0) {
        throw std::invalid_argument("initial server count must be positive");
    }
    config_.validate();

    servers_.reserve(initialServerCount);
    for (std::size_t index = 0; index < initialServerCount; ++index) {
        servers_.emplace_back(nextServerIdentifier_++);
    }

    statistics_.startingServerCount = initialServerCount;
    statistics_.currentServerCount = initialServerCount;
    statistics_.currentIdleServerCount = initialServerCount;
    statistics_.minimumServerCount = initialServerCount;
    statistics_.maximumServerCount = initialServerCount;
}

void LoadBalancer::initializeRequestQueue(std::size_t, std::ostream&) {
    throw NotImplementedError("LoadBalancer::initializeRequestQueue");
}

bool LoadBalancer::submitRequest(Request, std::ostream&) {
    throw NotImplementedError("LoadBalancer::submitRequest");
}

void LoadBalancer::runCycle(std::ostream&) {
    throw NotImplementedError("LoadBalancer::runCycle");
}

void LoadBalancer::printFinalSummary(std::ostream&) const {
    throw NotImplementedError("LoadBalancer::printFinalSummary");
}

std::uint64_t LoadBalancer::currentClockCycle() const noexcept {
    return currentClockCycle_;
}

std::size_t LoadBalancer::queueSize() const noexcept {
    return requestQueue_.size();
}

std::size_t LoadBalancer::serverCount() const noexcept {
    return servers_.size();
}

std::size_t LoadBalancer::busyServerCount() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(servers_.begin(), servers_.end(),
                      [](const WebServer& server) { return server.isBusy(); }));
}

std::size_t LoadBalancer::idleServerCount() const noexcept {
    return serverCount() - busyServerCount();
}

const SimulationStatistics& LoadBalancer::statistics() const noexcept {
    return statistics_;
}

OutputMode LoadBalancer::outputMode() const noexcept {
    return outputMode_;
}

void LoadBalancer::generateRandomArrivals(std::ostream&) {
    throw NotImplementedError("LoadBalancer::generateRandomArrivals");
}

void LoadBalancer::advanceServers(std::ostream&) {
    throw NotImplementedError("LoadBalancer::advanceServers");
}

void LoadBalancer::assignQueuedRequests(std::ostream&) {
    throw NotImplementedError("LoadBalancer::assignQueuedRequests");
}

void LoadBalancer::evaluateScaling(std::ostream&) {
    throw NotImplementedError("LoadBalancer::evaluateScaling");
}

void LoadBalancer::addServer(std::ostream&) {
    throw NotImplementedError("LoadBalancer::addServer");
}

bool LoadBalancer::removeIdleServer(std::ostream&) {
    throw NotImplementedError("LoadBalancer::removeIdleServer");
}

bool LoadBalancer::isBlockedSourceIp(std::string_view) const {
    throw NotImplementedError("LoadBalancer::isBlockedSourceIp");
}

bool LoadBalancer::scalingCooldownElapsed() const {
    throw NotImplementedError("LoadBalancer::scalingCooldownElapsed");
}

void LoadBalancer::refreshCurrentStatistics() noexcept {
    statistics_.currentQueueSize = queueSize();
    statistics_.currentProcessingRequests = busyServerCount();
    statistics_.currentServerCount = serverCount();
    statistics_.currentIdleServerCount = idleServerCount();
}

void LoadBalancer::writeVerboseEvent(std::ostream& output,
                                     std::string_view message) const {
    if (outputMode_ == OutputMode::Verbose) {
        output << "[cycle " << currentClockCycle_ << "] " << message << '\n';
    }
}
