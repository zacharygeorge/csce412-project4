#ifndef LOAD_BALANCER_H
#define LOAD_BALANCER_H

#include "Config.h"
#include "Request.h"
#include "WebServer.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <queue>
#include <random>
#include <string_view>
#include <vector>

/** Identifies a successful dynamic server-count change. */
enum class ScalingAction {
    AddedServer,
    RemovedServer
};

/** Immutable historical evidence for one successful scaling action. */
struct ScalingEvent {
    std::uint64_t clockCycle = 0;
    ScalingAction action = ScalingAction::AddedServer;
    std::size_t queueSize = 0;
    std::size_t serverCountBefore = 0;
    std::size_t serverCountAfter = 0;
};

/** Cumulative and current counters used by required summaries and demonstrations. */
struct SimulationStatistics {
    std::uint64_t requestsGenerated = 0;
    std::uint64_t requestsAccepted = 0;
    std::uint64_t requestsRejected = 0;
    std::uint64_t requestsCompleted = 0;
    std::uint64_t requestsDiscarded = 0;

    std::size_t startingQueueSize = 0;
    std::size_t currentQueueSize = 0;
    std::size_t currentProcessingRequests = 0;

    std::size_t startingServerCount = 0;
    std::size_t currentServerCount = 0;
    std::size_t currentIdleServerCount = 0;
    std::size_t minimumServerCount = 0;
    std::size_t maximumServerCount = 0;
    std::uint64_t serversAdded = 0;
    std::uint64_t serversRemoved = 0;

    std::vector<ScalingEvent> scalingEvents;
};

/** Owns the request queue, server pool, simulation clock, and execution statistics. */
class LoadBalancer {
public:
    /**
     * Creates the initial server pool and reproducible random engine.
     *
     * @param initialServerCount Positive number of servers requested by the user.
     * @param config Validated runtime settings retained by value.
     * @param outputMode Quiet or verbose event behavior for this run.
     * @throws std::invalid_argument when initialServerCount is zero.
     * @post The balancer clock is zero and every starting server is idle.
     */
    LoadBalancer(std::size_t initialServerCount,
                 SimulationConfig config,
                 OutputMode outputMode);

    /**
     * Fills the initial queue with accepted random requests.
     *
     * @param acceptedRequestTarget Desired queue size, normally 100 per starting server.
     * @param eventOutput Destination for verbose rejection/generation events.
     * @post startingQueueSize equals the number of accepted queued requests.
     * @throws std::runtime_error if the target cannot be reached within 100
     *         generated candidates per requested queue entry. This finite limit
     *         prevents an all-blocked configuration from hanging at startup.
     * @throws NotImplementedError while configured firewall ranges remain reserved
     *         for Milestone D.
     */
    void initializeRequestQueue(std::size_t acceptedRequestTarget,
                                std::ostream& eventOutput);

    /**
     * Submits one request through the firewall before it can enter the queue.
     *
     * @param request Incoming request whose source address will be checked.
     * @param eventOutput Destination for a verbose acceptance or rejection event.
     * @return true if accepted into the queue; false if rejected by an IP range.
     * @post requestsGenerated and exactly one of requestsAccepted or
     *       requestsRejected are incremented, preserving generated = accepted + rejected.
     * @note Milestone C accepts requests when no blocked ranges are configured.
     *       Configured firewall matching remains an explicit Milestone D operation.
     */
    bool submitRequest(Request request, std::ostream& eventOutput);

    /**
     * Executes exactly one complete simulation clock cycle.
     *
     * @param eventOutput Destination for verbose generation, assignment, completion,
     *        rejection, and scaling events.
     * @post The clock advances by one and current statistics describe the final state.
     *
     * The fixed event order is: increment clock; generate/filter arrivals; advance
     * existing server work; assign queued work to newly idle servers; perform at most
     * one eligible scaling action; refresh current counters. A newly added server first
     * receives work during the next cycle.
     */
    void runCycle(std::ostream& eventOutput);

    /**
     * Prints the required final state and cumulative statistics.
     *
     * @param output Stream that receives the summary.
     * @post No simulation state is changed.
     * @note Complete summary output is intentionally unimplemented in Milestone A.
     */
    void printFinalSummary(std::ostream& output) const;

    /** @return Number of completed clock cycles. */
    std::uint64_t currentClockCycle() const noexcept;

    /** @return Number of requests waiting for assignment. */
    std::size_t queueSize() const noexcept;

    /** @return Current number of managed web servers. */
    std::size_t serverCount() const noexcept;

    /**
     * Exposes read-only server state for demonstrations and deterministic tests.
     *
     * @return The managed servers in assignment order.
     */
    const std::vector<WebServer>& servers() const noexcept;

    /** @return Number of servers currently processing requests. */
    std::size_t busyServerCount() const noexcept;

    /** @return Number of servers currently able to accept work. */
    std::size_t idleServerCount() const noexcept;

    /** @return Read-only cumulative and current statistics. */
    const SimulationStatistics& statistics() const noexcept;

    /** @return Quiet or verbose mode selected for this run. */
    OutputMode outputMode() const noexcept;

private:
    /**
     * Performs one Bernoulli trial using arrivalProbability. On success, chooses
     * one uniform batch size in [1, maximumRequestsPerArrival], generates that many
     * independent requests, and submits each through the single accounting boundary.
     * On failure, generates no requests during this cycle.
     */
    void generateRandomArrivals(std::ostream& eventOutput);

    /** Advances all busy servers and records requests that finish this cycle. */
    void advanceServers(std::ostream& eventOutput);

    /** Moves FIFO requests into every currently idle server. */
    void assignQueuedRequests(std::ostream& eventOutput);

    /** Applies strict >up or <down thresholds when the cooldown has elapsed. */
    void evaluateScaling(std::ostream& eventOutput);

    /** Adds exactly one idle server and records a successful scale-up event. */
    void addServer(std::ostream& eventOutput);

    /**
     * Removes exactly one idle server while preserving a minimum of one server.
     *
     * @return true when a server was removed; false when none can be removed safely.
     * Active servers are never removed, so accepted requests cannot be lost.
     */
    bool removeIdleServer(std::ostream& eventOutput);

    /**
     * @return false when no firewall ranges are configured.
     * @throws NotImplementedError when range matching is requested before Milestone D.
     */
    bool isBlockedSourceIp(std::string_view sourceIp) const;

    /**
     * @return true before the first scale or after the configured cycles have elapsed.
     * Only a successful add/remove restarts the cooldown; a skipped removal does not.
     */
    bool scalingCooldownElapsed() const;

    /** Updates derived queue, busy, idle, and server-count fields without double-counting. */
    void refreshCurrentStatistics() noexcept;

    /** Writes an event only when outputMode_ is OutputMode::Verbose. */
    void writeVerboseEvent(std::ostream& output, std::string_view message) const;

    SimulationConfig config_;
    OutputMode outputMode_;
    std::queue<Request> requestQueue_;
    std::vector<WebServer> servers_;
    SimulationStatistics statistics_;
    std::mt19937 randomEngine_;
    std::uint64_t currentClockCycle_ = 0;
    std::optional<std::uint64_t> lastSuccessfulScalingCycle_;
    std::size_t nextServerIdentifier_ = 1;
};

#endif
