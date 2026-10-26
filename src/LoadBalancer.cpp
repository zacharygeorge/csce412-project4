#include "LoadBalancer.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
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

std::string describeRequest(const Request& request) {
    std::ostringstream description;
    description << request.sourceIp() << " -> " << request.destinationIp()
                << ", type " << request.jobType() << ", "
                << request.processingTime() << " cycles";
    return description.str();
}

std::string_view scalingActionName(ScalingAction action) noexcept {
    return action == ScalingAction::AddedServer ? "added" : "removed";
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

    const std::uint64_t generatedBefore = statistics_.requestsGenerated;
    const std::uint64_t acceptedBefore = statistics_.requestsAccepted;
    const std::uint64_t rejectedBefore = statistics_.requestsRejected;
    std::size_t attempts = 0;
    initializingRequestQueue_ = true;
    try {
        while (requestQueue_.size() < acceptedRequestTarget &&
               attempts < maximumAttempts) {
            ++attempts;
            submitRequest(Request::generateRandom(randomEngine_,
                                                  config_.minimumProcessingTime,
                                                  config_.maximumProcessingTime),
                          eventOutput);
        }
    } catch (...) {
        initializingRequestQueue_ = false;
        throw;
    }
    initializingRequestQueue_ = false;

    if (requestQueue_.size() != acceptedRequestTarget) {
        writeVerboseEvent(
            eventOutput,
            "initial queue generation failed after " +
                std::to_string(statistics_.requestsGenerated - generatedBefore) +
                " candidates; accepted " +
                std::to_string(statistics_.requestsAccepted - acceptedBefore) +
                ", rejected " +
                std::to_string(statistics_.requestsRejected - rejectedBefore));
        throw std::runtime_error(
            "unable to generate the requested number of accepted initial requests");
    }

    statistics_.startingQueueSize = requestQueue_.size();
    refreshCurrentStatistics();
    writeVerboseEvent(
        eventOutput,
        "initialized queue with " + std::to_string(requestQueue_.size()) +
            " accepted requests from " +
            std::to_string(statistics_.requestsGenerated - generatedBefore) +
            " candidates; rejected " +
            std::to_string(statistics_.requestsRejected - rejectedBefore));
}

bool LoadBalancer::submitRequest(Request request, std::ostream& eventOutput) {
    const bool blocked = isBlockedSourceIp(request.sourceIp());
    ++statistics_.requestsGenerated;
    if (blocked) {
        ++statistics_.requestsRejected;
        if (!initializingRequestQueue_) {
            writeVerboseEvent(eventOutput,
                              "rejected request from blocked source " +
                                  request.sourceIp());
        }
        refreshCurrentStatistics();
        return false;
    }

    const std::string description = describeRequest(request);
    requestQueue_.push(std::move(request));
    ++statistics_.requestsAccepted;
    if (!initializingRequestQueue_) {
        writeVerboseEvent(eventOutput,
                          "accepted request " + description +
                              "; queue size " + std::to_string(queueSize()));
    }
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

void LoadBalancer::printFinalSummary(std::ostream& output) const {
    const bool requestAccountingIsConsistent =
        statistics_.requestsGenerated ==
            statistics_.requestsAccepted + statistics_.requestsRejected &&
        statistics_.requestsAccepted ==
            statistics_.requestsCompleted + queueSize() + busyServerCount();
    const bool serverAccountingIsConsistent =
        serverCount() == statistics_.startingServerCount +
                             statistics_.serversAdded -
                             statistics_.serversRemoved &&
        serverCount() == busyServerCount() + idleServerCount();

    output << "\n=== Simulation Summary ===\n"
           << "Output mode: " << toString(outputMode_) << '\n'
           << "Clock cycles completed: " << currentClockCycle_ << '\n'
           << "Processing-time range: " << config_.minimumProcessingTime << '-'
           << config_.maximumProcessingTime << " cycles\n"
           << "Starting servers: " << statistics_.startingServerCount << '\n'
           << "Starting queue size: " << statistics_.startingQueueSize << '\n'
           << "Requests generated: " << statistics_.requestsGenerated << '\n'
           << "Requests accepted: " << statistics_.requestsAccepted << '\n'
           << "Requests rejected: " << statistics_.requestsRejected << '\n'
           << "Requests completed: " << statistics_.requestsCompleted << '\n'
           << "Requests discarded: " << statistics_.requestsDiscarded << '\n'
           << "Random arrival batches: " << statistics_.randomArrivalBatches << '\n'
           << "Random arrival requests: "
           << statistics_.randomArrivalRequestsGenerated << '\n'
           << "Ending queue size: " << queueSize() << '\n'
           << "Requests processing: " << busyServerCount() << '\n'
           << "Ending servers: " << serverCount() << '\n'
           << "Busy servers: " << busyServerCount() << '\n'
           << "Idle servers: " << idleServerCount() << '\n'
           << "Servers added: " << statistics_.serversAdded << '\n'
           << "Servers removed: " << statistics_.serversRemoved << '\n'
           << "Minimum servers: " << statistics_.minimumServerCount << '\n'
           << "Maximum servers: " << statistics_.maximumServerCount << '\n'
           << "Scaling events: " << statistics_.scalingEvents.size() << '\n';

    if (!statistics_.scalingEvents.empty()) {
        const auto printEvent = [&output](std::string_view label,
                                          const ScalingEvent& event) {
            output << label << ": cycle " << event.clockCycle << ", "
                   << scalingActionName(event.action) << " server, queue "
                   << event.queueSize << ", servers " << event.serverCountBefore
                   << " -> " << event.serverCountAfter << '\n';
        };
        printEvent("First scaling event", statistics_.scalingEvents.front());
        if (statistics_.scalingEvents.size() > 1) {
            printEvent("Last scaling event", statistics_.scalingEvents.back());
        }
    }

    output << "Request accounting: "
           << (requestAccountingIsConsistent ? "consistent" : "INCONSISTENT")
           << '\n'
           << "Server accounting: "
           << (serverAccountingIsConsistent ? "consistent" : "INCONSISTENT")
           << '\n';
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
    ++statistics_.randomArrivalBatches;
    statistics_.randomArrivalRequestsGenerated += batchSize;
    writeVerboseEvent(eventOutput,
                      "generated arrival batch of " +
                          std::to_string(batchSize) + " request(s)");
    for (std::size_t index = 0; index < batchSize; ++index) {
        submitRequest(Request::generateRandom(randomEngine_,
                                              config_.minimumProcessingTime,
                                              config_.maximumProcessingTime),
                      eventOutput);
    }
}

void LoadBalancer::advanceServers(std::ostream& eventOutput) {
    for (WebServer& server : servers_) {
        std::optional<Request> completedRequest = server.advanceOneCycle();
        if (completedRequest.has_value()) {
            ++statistics_.requestsCompleted;
            writeVerboseEvent(
                eventOutput,
                "server " + std::to_string(server.identifier()) +
                    " completed request " + describeRequest(*completedRequest));
        }
    }
}

void LoadBalancer::assignQueuedRequests(std::ostream& eventOutput) {
    for (WebServer& server : servers_) {
        if (requestQueue_.empty()) {
            break;
        }
        if (server.isIdle()) {
            server.acceptRequest(std::move(requestQueue_.front()));
            requestQueue_.pop();
            writeVerboseEvent(
                eventOutput,
                "assigned request " + describeRequest(*server.currentRequest()) +
                    " to server " + std::to_string(server.identifier()) +
                    "; queue size " + std::to_string(queueSize()));
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

void LoadBalancer::addServer(std::ostream& eventOutput) {
    const std::size_t serverCountBefore = serverCount();
    servers_.emplace_back(nextServerIdentifier_++);
    ++statistics_.serversAdded;
    statistics_.maximumServerCount =
        std::max(statistics_.maximumServerCount, serverCount());
    statistics_.scalingEvents.push_back(
        ScalingEvent{currentClockCycle_, ScalingAction::AddedServer, queueSize(),
                     serverCountBefore, serverCount()});
    lastSuccessfulScalingCycle_ = currentClockCycle_;
    writeVerboseEvent(eventOutput,
                      "scaled up: added server " +
                          std::to_string(servers_.back().identifier()) +
                          "; servers " + std::to_string(serverCountBefore) +
                          " -> " + std::to_string(serverCount()) +
                          ", queue size " + std::to_string(queueSize()));
}

bool LoadBalancer::removeIdleServer(std::ostream& eventOutput) {
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
    const std::size_t removedIdentifier = idleServer->identifier();
    servers_.erase(std::prev(idleServer.base()));
    ++statistics_.serversRemoved;
    statistics_.minimumServerCount =
        std::min(statistics_.minimumServerCount, serverCount());
    statistics_.scalingEvents.push_back(
        ScalingEvent{currentClockCycle_, ScalingAction::RemovedServer, queueSize(),
                     serverCountBefore, serverCount()});
    lastSuccessfulScalingCycle_ = currentClockCycle_;
    writeVerboseEvent(eventOutput,
                      "scaled down: removed idle server " +
                          std::to_string(removedIdentifier) + "; servers " +
                          std::to_string(serverCountBefore) + " -> " +
                          std::to_string(serverCount()) + ", queue size " +
                          std::to_string(queueSize()));
    return true;
}

bool LoadBalancer::isBlockedSourceIp(std::string_view sourceIp) const {
    return std::any_of(
        config_.blockedSourceRanges.begin(), config_.blockedSourceRanges.end(),
        [sourceIp](const IpRange& range) { return range.contains(sourceIp); });
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
