#include "LoadBalancer.h"
#include "NotImplementedError.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace {
bool isGreaterThanProduct(std::size_t value,
                          std::size_t factor,
                          std::size_t count) noexcept {
    if (count != 0 && factor > std::numeric_limits<std::size_t>::max() / count) {
        return false;
    }
    return value > factor * count;
}

bool isLessThanProduct(std::size_t value,
                       std::size_t factor,
                       std::size_t count) noexcept {
    if (count != 0 && factor > std::numeric_limits<std::size_t>::max() / count) {
        return true;
    }
    return value < factor * count;
}
}  // namespace

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

void LoadBalancer::initializeRequestQueue(std::size_t acceptedRequestTarget,
                                          std::ostream& eventOutput) {
    if (currentClockCycle_ != 0 || !requestQueue_.empty() ||
        statistics_.requestsGenerated != 0 || busyServerCount() != 0) {
        throw std::logic_error(
            "initial request queue must be created before other simulation work");
    }

    const std::size_t maximumSize = std::numeric_limits<std::size_t>::max();
    const std::size_t maximumAttempts =
        acceptedRequestTarget > maximumSize / 100
            ? maximumSize
            : acceptedRequestTarget * 100;

    std::size_t attempts = 0;
    while (requestQueue_.size() < acceptedRequestTarget &&
           attempts < maximumAttempts) {
        ++attempts;
        submitRequest(Request::generateRandom(randomEngine_,
                                              config_.minimumProcessingTime,
                                              config_.maximumProcessingTime),
                      eventOutput);
    }

    if (requestQueue_.size() != acceptedRequestTarget) {
        throw std::runtime_error(
            "unable to generate the requested number of accepted initial requests");
    }

    statistics_.startingQueueSize = requestQueue_.size();
    refreshCurrentStatistics();
}

bool LoadBalancer::submitRequest(Request request, std::ostream&) {
    const bool blocked = isBlockedSourceIp(request.sourceIp());
    ++statistics_.requestsGenerated;
    if (blocked) {
        ++statistics_.requestsRejected;
        refreshCurrentStatistics();
        return false;
    }

    requestQueue_.push(std::move(request));
    ++statistics_.requestsAccepted;
    refreshCurrentStatistics();
    return true;
}

void LoadBalancer::runCycle(std::ostream& eventOutput) {
    ++currentClockCycle_;
    generateRandomArrivals(eventOutput);
    advanceServers(eventOutput);
    assignQueuedRequests(eventOutput);
    evaluateScaling(eventOutput);
    refreshCurrentStatistics();
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

const std::vector<WebServer>& LoadBalancer::servers() const noexcept {
    return servers_;
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

void LoadBalancer::generateRandomArrivals(std::ostream& eventOutput) {
    std::bernoulli_distribution arrivalOccurs(config_.arrivalProbability);
    if (!arrivalOccurs(randomEngine_)) {
        return;
    }

    std::uniform_int_distribution<std::size_t> batchSizeDistribution(
        1, config_.maximumRequestsPerArrival);
    const std::size_t batchSize = batchSizeDistribution(randomEngine_);
    for (std::size_t index = 0; index < batchSize; ++index) {
        submitRequest(Request::generateRandom(randomEngine_,
                                              config_.minimumProcessingTime,
                                              config_.maximumProcessingTime),
                      eventOutput);
    }
}

void LoadBalancer::advanceServers(std::ostream&) {
    for (WebServer& server : servers_) {
        if (server.advanceOneCycle().has_value()) {
            ++statistics_.requestsCompleted;
        }
    }
}

void LoadBalancer::assignQueuedRequests(std::ostream&) {
    for (WebServer& server : servers_) {
        if (requestQueue_.empty()) {
            break;
        }
        if (server.isIdle()) {
            server.acceptRequest(std::move(requestQueue_.front()));
            requestQueue_.pop();
        }
    }
}

void LoadBalancer::evaluateScaling(std::ostream& eventOutput) {
    if (!scalingCooldownElapsed()) {
        return;
    }

    const std::size_t currentServerCount = serverCount();
    if (isGreaterThanProduct(queueSize(),
                             config_.scaleUpRequestsPerServer,
                             currentServerCount)) {
        addServer(eventOutput);
    } else if (isLessThanProduct(queueSize(),
                                 config_.scaleDownRequestsPerServer,
                                 currentServerCount)) {
        removeIdleServer(eventOutput);
    }
}

void LoadBalancer::addServer(std::ostream&) {
    const std::size_t serverCountBefore = serverCount();
    servers_.emplace_back(nextServerIdentifier_++);
    ++statistics_.serversAdded;
    statistics_.maximumServerCount =
        std::max(statistics_.maximumServerCount, serverCount());
    statistics_.scalingEvents.push_back(
        ScalingEvent{currentClockCycle_, ScalingAction::AddedServer, queueSize(),
                     serverCountBefore, serverCount()});
    lastSuccessfulScalingCycle_ = currentClockCycle_;
}

bool LoadBalancer::removeIdleServer(std::ostream&) {
    if (serverCount() <= 1) {
        return false;
    }

    const auto idleServer = std::find_if(
        servers_.rbegin(), servers_.rend(),
        [](const WebServer& server) { return server.isIdle(); });
    if (idleServer == servers_.rend()) {
        return false;
    }

    const std::size_t serverCountBefore = serverCount();
    servers_.erase(std::prev(idleServer.base()));
    ++statistics_.serversRemoved;
    statistics_.minimumServerCount =
        std::min(statistics_.minimumServerCount, serverCount());
    statistics_.scalingEvents.push_back(
        ScalingEvent{currentClockCycle_, ScalingAction::RemovedServer, queueSize(),
                     serverCountBefore, serverCount()});
    lastSuccessfulScalingCycle_ = currentClockCycle_;
    return true;
}

bool LoadBalancer::isBlockedSourceIp(std::string_view) const {
    if (config_.blockedSourceRanges.empty()) {
        return false;
    }
    throw NotImplementedError(
        "LoadBalancer::isBlockedSourceIp with configured firewall ranges");
}

bool LoadBalancer::scalingCooldownElapsed() const {
    return !lastSuccessfulScalingCycle_.has_value() ||
           currentClockCycle_ - *lastSuccessfulScalingCycle_ >=
               config_.scalingCooldownCycles;
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
