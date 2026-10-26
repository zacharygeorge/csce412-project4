#include "LoadBalancer.h"
#include "NotImplementedError.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Function>
void requireThrows(Function function, const std::string& message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& error) {
        throw std::runtime_error(message + "; wrong exception: " + error.what());
    }
    throw std::runtime_error(message + "; no exception was thrown");
}

SimulationConfig makeConfig() {
    SimulationConfig config;
    config.minimumProcessingTime = 3;
    config.maximumProcessingTime = 3;
    config.scaleUpRequestsPerServer = 80;
    config.scaleDownRequestsPerServer = 50;
    config.scalingCooldownCycles = 3;
    config.arrivalProbability = 0.0;
    config.maximumRequestsPerArrival = 3;
    config.requestsPerStartingServer = 100;
    config.randomSeed = 412;
    config.blockedSourceRanges.clear();
    return config;
}

Request makeRequest(std::size_t identifier, int processingTime = 10) {
    const std::size_t secondOctet = (identifier / 256U) % 256U;
    const std::size_t thirdOctet = identifier % 256U;
    const std::string sourceIp =
        "10." + std::to_string(secondOctet) + "." +
        std::to_string(thirdOctet) + ".1";
    return Request(sourceIp, "172.16.0.1", processingTime,
                   identifier % 2U == 0U ? 'P' : 'S');
}

void submitRequests(LoadBalancer& loadBalancer,
                    std::size_t count,
                    int processingTime,
                    std::ostream& output) {
    for (std::size_t index = 0; index < count; ++index) {
        require(loadBalancer.submitRequest(makeRequest(index, processingTime), output),
                "request was unexpectedly rejected");
    }
}

void requireAccountingInvariants(const LoadBalancer& loadBalancer) {
    const SimulationStatistics& statistics = loadBalancer.statistics();
    require(statistics.requestsGenerated ==
                statistics.requestsAccepted + statistics.requestsRejected,
            "generated request accounting is inconsistent");
    require(statistics.requestsAccepted ==
                statistics.requestsCompleted + loadBalancer.queueSize() +
                    loadBalancer.busyServerCount(),
            "accepted request accounting is inconsistent");
    require(loadBalancer.serverCount() ==
                statistics.startingServerCount + statistics.serversAdded -
                    statistics.serversRemoved,
            "server add/remove accounting is inconsistent");
    require(loadBalancer.serverCount() ==
                loadBalancer.busyServerCount() + loadBalancer.idleServerCount(),
            "busy/idle server accounting is inconsistent");
    require(statistics.currentQueueSize == loadBalancer.queueSize(),
            "current queue statistic is stale");
    require(statistics.currentProcessingRequests == loadBalancer.busyServerCount(),
            "current processing statistic is stale");
    require(statistics.currentServerCount == loadBalancer.serverCount(),
            "current server statistic is stale");
    require(statistics.currentIdleServerCount == loadBalancer.idleServerCount(),
            "current idle statistic is stale");
}

void requireEquivalentState(const LoadBalancer& first,
                            const LoadBalancer& second) {
    const SimulationStatistics& firstStatistics = first.statistics();
    const SimulationStatistics& secondStatistics = second.statistics();
    require(first.currentClockCycle() == second.currentClockCycle(),
            "deterministic clocks differ");
    require(first.queueSize() == second.queueSize(),
            "deterministic queue sizes differ");
    require(first.serverCount() == second.serverCount(),
            "deterministic server counts differ");
    require(firstStatistics.requestsGenerated == secondStatistics.requestsGenerated,
            "deterministic generated counts differ");
    require(firstStatistics.requestsAccepted == secondStatistics.requestsAccepted,
            "deterministic accepted counts differ");
    require(firstStatistics.requestsCompleted == secondStatistics.requestsCompleted,
            "deterministic completed counts differ");
    require(firstStatistics.serversAdded == secondStatistics.serversAdded,
            "deterministic scale-up counts differ");
    require(firstStatistics.serversRemoved == secondStatistics.serversRemoved,
            "deterministic scale-down counts differ");

    require(first.servers().size() == second.servers().size(),
            "deterministic server collections differ");
    for (std::size_t index = 0; index < first.servers().size(); ++index) {
        const WebServer& firstServer = first.servers()[index];
        const WebServer& secondServer = second.servers()[index];
        require(firstServer.isBusy() == secondServer.isBusy(),
                "deterministic busy states differ");
        require(firstServer.remainingProcessingTime() ==
                    secondServer.remainingProcessingTime(),
                "deterministic remaining times differ");
        if (firstServer.isBusy()) {
            require(firstServer.currentRequest()->sourceIp() ==
                        secondServer.currentRequest()->sourceIp(),
                    "deterministic request sequences differ");
        }
    }
}

void testInitialServerPoolAndStatistics() {
    LoadBalancer loadBalancer(3, makeConfig(), OutputMode::Quiet);

    require(loadBalancer.currentClockCycle() == 0, "initial clock is not zero");
    require(loadBalancer.serverCount() == 3, "wrong initial server count");
    require(loadBalancer.busyServerCount() == 0, "new servers are busy");
    require(loadBalancer.idleServerCount() == 3, "new servers are not idle");
    require(loadBalancer.statistics().minimumServerCount == 3,
            "initial minimum server count is wrong");
    require(loadBalancer.statistics().maximumServerCount == 3,
            "initial maximum server count is wrong");
    requireAccountingInvariants(loadBalancer);
}

void testConfiguredFirewallRemainsExplicitlyUnimplemented() {
    std::ostringstream output;
    SimulationConfig config = makeConfig();
    config.blockedSourceRanges.push_back({"10.0.0.0", "10.255.255.255"});
    LoadBalancer loadBalancer(1, config, OutputMode::Quiet);

    requireThrows<NotImplementedError>(
        [&] { loadBalancer.submitRequest(makeRequest(1), output); },
        "configured firewall was silently treated as implemented");
    require(loadBalancer.statistics().requestsGenerated == 0,
            "unfinished filtering partially changed request statistics");
    require(loadBalancer.queueSize() == 0,
            "unfinished filtering partially changed the request queue");
}

void testInitialQueueHasExactAcceptedTarget() {
    std::ostringstream output;
    LoadBalancer loadBalancer(2, makeConfig(), OutputMode::Quiet);
    loadBalancer.initializeRequestQueue(200, output);

    require(loadBalancer.queueSize() == 200, "initial queue target was not reached");
    require(loadBalancer.statistics().startingQueueSize == 200,
            "starting queue statistic is wrong");
    require(loadBalancer.statistics().requestsGenerated == 200,
            "initial generated count is wrong");
    require(loadBalancer.statistics().requestsAccepted == 200,
            "initial accepted count is wrong");
    requireAccountingInvariants(loadBalancer);
}

void testFifoAssignmentAcrossServers() {
    std::ostringstream output;
    LoadBalancer loadBalancer(2, makeConfig(), OutputMode::Quiet);
    loadBalancer.submitRequest(makeRequest(1, 1), output);
    loadBalancer.submitRequest(makeRequest(2, 1), output);
    loadBalancer.submitRequest(makeRequest(3, 5), output);

    loadBalancer.runCycle(output);
    require(loadBalancer.servers()[0].currentRequest()->sourceIp() ==
                makeRequest(1).sourceIp(),
            "first server did not receive the queue front");
    require(loadBalancer.servers()[1].currentRequest()->sourceIp() ==
                makeRequest(2).sourceIp(),
            "second server did not receive the second request");
    require(loadBalancer.queueSize() == 1, "wrong queue size after distribution");

    loadBalancer.runCycle(output);
    require(loadBalancer.statistics().requestsCompleted == 2,
            "first two requests did not complete together");
    require(loadBalancer.servers()[0].currentRequest()->sourceIp() ==
                makeRequest(3).sourceIp(),
            "third FIFO request was not assigned next");
    requireAccountingInvariants(loadBalancer);
}

void testCycleOrderAndExactProcessingDuration() {
    std::ostringstream output;
    LoadBalancer loadBalancer(1, makeConfig(), OutputMode::Quiet);
    loadBalancer.submitRequest(makeRequest(1, 2), output);

    loadBalancer.runCycle(output);
    require(loadBalancer.currentClockCycle() == 1, "clock did not advance");
    require(loadBalancer.statistics().requestsCompleted == 0,
            "newly assigned request processed on its assignment cycle");
    require(loadBalancer.servers()[0].remainingProcessingTime() == 2,
            "assignment cycle changed processing time");

    loadBalancer.runCycle(output);
    require(loadBalancer.statistics().requestsCompleted == 0,
            "two-cycle request completed after one processing cycle");
    require(loadBalancer.servers()[0].remainingProcessingTime() == 1,
            "processing did not advance exactly once");

    loadBalancer.runCycle(output);
    require(loadBalancer.statistics().requestsCompleted == 1,
            "two-cycle request did not complete on its second processing cycle");
    require(loadBalancer.servers()[0].isIdle(),
            "server did not become idle after completion");
    requireAccountingInvariants(loadBalancer);
}

void testIncomingArrivalsAreDeterministic() {
    std::ostringstream firstOutput;
    std::ostringstream secondOutput;
    SimulationConfig config = makeConfig();
    config.arrivalProbability = 1.0;
    config.maximumRequestsPerArrival = 4;
    config.randomSeed = 98765;

    LoadBalancer first(1, config, OutputMode::Quiet);
    LoadBalancer second(1, config, OutputMode::Quiet);
    for (int cycle = 0; cycle < 12; ++cycle) {
        first.runCycle(firstOutput);
        second.runCycle(secondOutput);
        requireEquivalentState(first, second);
        requireAccountingInvariants(first);
        requireAccountingInvariants(second);
    }
    require(first.statistics().requestsGenerated > 0,
            "arrival generator did not create requests");
}

void testScaleUpUsesStrictThreshold() {
    std::ostringstream output;
    LoadBalancer atThreshold(1, makeConfig(), OutputMode::Quiet);
    submitRequests(atThreshold, 81, 100, output);
    atThreshold.runCycle(output);
    require(atThreshold.queueSize() == 80, "scale-up equality setup failed");
    require(atThreshold.serverCount() == 1,
            "server was added at threshold equality");

    LoadBalancer aboveThreshold(1, makeConfig(), OutputMode::Quiet);
    submitRequests(aboveThreshold, 82, 100, output);
    aboveThreshold.runCycle(output);
    require(aboveThreshold.queueSize() == 81, "scale-up strict setup failed");
    require(aboveThreshold.serverCount() == 2,
            "server was not added above the scale-up threshold");
    require(aboveThreshold.statistics().serversAdded == 1,
            "scale-up statistic was not recorded");
    require(aboveThreshold.statistics().scalingEvents.size() == 1,
            "scale-up event was not recorded exactly once");
    const ScalingEvent& event = aboveThreshold.statistics().scalingEvents.front();
    require(event.clockCycle == 1 && event.action == ScalingAction::AddedServer &&
                event.serverCountBefore == 1 && event.serverCountAfter == 2 &&
                event.queueSize == 81,
            "scale-up event details are incorrect");
    requireAccountingInvariants(aboveThreshold);
}

void testScaleDownAndMinimumServerCount() {
    std::ostringstream output;
    LoadBalancer loadBalancer(2, makeConfig(), OutputMode::Quiet);
    loadBalancer.runCycle(output);
    require(loadBalancer.serverCount() == 1,
            "idle server was not removed below the scale-down threshold");
    require(loadBalancer.statistics().serversRemoved == 1,
            "scale-down statistic was not recorded");
    require(loadBalancer.statistics().scalingEvents.size() == 1,
            "scale-down event was not recorded");

    for (int cycle = 0; cycle < 10; ++cycle) {
        loadBalancer.runCycle(output);
    }
    require(loadBalancer.serverCount() == 1,
            "load balancer removed its final server");
    require(loadBalancer.statistics().minimumServerCount == 1,
            "minimum server statistic is incorrect");
    requireAccountingInvariants(loadBalancer);
}

void testNoScaleDownAtThresholdEquality() {
    std::ostringstream output;
    LoadBalancer loadBalancer(2, makeConfig(), OutputMode::Quiet);
    submitRequests(loadBalancer, 102, 100, output);
    loadBalancer.runCycle(output);

    require(loadBalancer.queueSize() == 100, "scale-down equality setup failed");
    require(loadBalancer.serverCount() == 2,
            "server count changed at scale-down threshold equality");
    require(loadBalancer.statistics().scalingEvents.empty(),
            "scaling event was recorded at threshold equality");
    requireAccountingInvariants(loadBalancer);
}

void testScalingCooldownAllowsAtMostOneTimedChange() {
    std::ostringstream output;
    SimulationConfig config = makeConfig();
    config.scalingCooldownCycles = 3;
    LoadBalancer loadBalancer(1, config, OutputMode::Quiet);
    submitRequests(loadBalancer, 202, 100, output);

    loadBalancer.runCycle(output);
    require(loadBalancer.serverCount() == 2, "first scale-up was not immediate");
    loadBalancer.runCycle(output);
    require(loadBalancer.serverCount() == 2, "cooldown allowed scaling at cycle two");
    loadBalancer.runCycle(output);
    require(loadBalancer.serverCount() == 2,
            "cooldown allowed scaling before three cycles elapsed");
    loadBalancer.runCycle(output);
    require(loadBalancer.serverCount() == 3,
            "scaling did not resume when cooldown elapsed");
    require(loadBalancer.statistics().scalingEvents.size() == 2,
            "cooldown produced the wrong number of scaling events");
    require(loadBalancer.statistics().scalingEvents[0].clockCycle == 1 &&
                loadBalancer.statistics().scalingEvents[1].clockCycle == 4,
            "scaling occurred on the wrong cycles");
    requireAccountingInvariants(loadBalancer);
}

void testBusyServersPreventScaleDown() {
    std::ostringstream output;
    LoadBalancer loadBalancer(2, makeConfig(), OutputMode::Quiet);
    submitRequests(loadBalancer, 2, 100, output);

    loadBalancer.runCycle(output);
    require(loadBalancer.queueSize() == 0, "busy scale-down setup left queued work");
    require(loadBalancer.busyServerCount() == 2, "both servers are not busy");
    require(loadBalancer.serverCount() == 2,
            "an active server was removed during scale-down");
    require(loadBalancer.statistics().serversRemoved == 0,
            "failed scale-down changed removal statistics");

    loadBalancer.runCycle(output);
    require(loadBalancer.serverCount() == 2,
            "active server was removed on a later cycle");
    requireAccountingInvariants(loadBalancer);
}

void testScaleDownPreservesActiveRequest() {
    std::ostringstream output;
    LoadBalancer loadBalancer(2, makeConfig(), OutputMode::Quiet);
    const Request request = makeRequest(42, 5);
    loadBalancer.submitRequest(request, output);

    loadBalancer.runCycle(output);
    require(loadBalancer.serverCount() == 1, "idle server was not removed");
    require(loadBalancer.busyServerCount() == 1, "active request was lost");
    require(loadBalancer.servers()[0].currentRequest()->sourceIp() ==
                request.sourceIp(),
            "scale-down changed or discarded the active request");
    requireAccountingInvariants(loadBalancer);
}

void testAllAcceptedRequestsCompleteExactlyOnce() {
    std::ostringstream output;
    LoadBalancer loadBalancer(2, makeConfig(), OutputMode::Quiet);
    submitRequests(loadBalancer, 7, 1, output);

    for (int cycle = 0; cycle < 20 &&
                        loadBalancer.statistics().requestsCompleted < 7;
         ++cycle) {
        loadBalancer.runCycle(output);
        requireAccountingInvariants(loadBalancer);
    }

    require(loadBalancer.statistics().requestsAccepted == 7,
            "accepted request count changed unexpectedly");
    require(loadBalancer.statistics().requestsCompleted == 7,
            "not every request completed exactly once");
    require(loadBalancer.queueSize() == 0, "queue was not drained");
    require(loadBalancer.busyServerCount() == 0,
            "processing requests remain after completion");
    require(loadBalancer.statistics().requestsDiscarded == 0,
            "requests were reported as discarded");
    requireAccountingInvariants(loadBalancer);
}
}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"initial server pool and statistics", testInitialServerPoolAndStatistics},
        {"configured firewall remains explicit",
         testConfiguredFirewallRemainsExplicitlyUnimplemented},
        {"initial queue target", testInitialQueueHasExactAcceptedTarget},
        {"FIFO assignment across servers", testFifoAssignmentAcrossServers},
        {"cycle order and processing duration", testCycleOrderAndExactProcessingDuration},
        {"deterministic incoming arrivals", testIncomingArrivalsAreDeterministic},
        {"strict scale-up threshold", testScaleUpUsesStrictThreshold},
        {"scale-down and minimum server count", testScaleDownAndMinimumServerCount},
        {"no scale-down at equality", testNoScaleDownAtThresholdEquality},
        {"scaling cooldown", testScalingCooldownAllowsAtMostOneTimedChange},
        {"busy servers prevent scale-down", testBusyServersPreventScaleDown},
        {"scale-down preserves active request", testScaleDownPreservesActiveRequest},
        {"all requests complete exactly once", testAllAcceptedRequestsCompleteExactlyOnce},
    };

    int passed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            ++passed;
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
        }
    }

    std::cout << passed << '/' << tests.size() << " tests passed\n";
    return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
