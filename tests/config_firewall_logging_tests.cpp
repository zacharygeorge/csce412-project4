#include "Config.h"
#include "LoadBalancer.h"
#include "Request.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
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

void requireContains(const std::string& text,
                     const std::string& expected,
                     const std::string& message) {
    require(text.find(expected) != std::string::npos,
            message + "; missing: " + expected);
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

class TemporaryConfigFile {
public:
    explicit TemporaryConfigFile(const std::string& contents) {
        static std::size_t nextIdentifier = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("csce412_project4_config_" +
                 std::to_string(nextIdentifier++) + ".txt");
        std::ofstream output(path_);
        if (!output) {
            throw std::runtime_error("unable to create temporary configuration file");
        }
        output << contents;
        if (!output) {
            throw std::runtime_error("unable to write temporary configuration file");
        }
    }

    ~TemporaryConfigFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::string string() const {
        return path_.string();
    }

private:
    std::filesystem::path path_;
};

std::string validConfigText() {
    return R"CONFIG(
# Complete test configuration
minimum_processing_time=2
maximum_processing_time=9
scale_up_requests_per_server=80
scale_down_requests_per_server=50
scaling_cooldown_cycles=7
arrival_probability=0.4
maximum_requests_per_arrival=4
requests_per_starting_server=6
random_seed=12345
default_output_mode=verbose
)CONFIG";
}

std::string replaceOnce(std::string text,
                        const std::string& original,
                        const std::string& replacement) {
    const std::size_t position = text.find(original);
    if (position == std::string::npos) {
        throw std::runtime_error("test configuration replacement target is missing");
    }
    text.replace(position, original.size(), replacement);
    return text;
}

SimulationConfig makeRuntimeConfig() {
    SimulationConfig config;
    config.minimumProcessingTime = 1;
    config.maximumProcessingTime = 1;
    config.scaleUpRequestsPerServer = 80;
    config.scaleDownRequestsPerServer = 50;
    config.scalingCooldownCycles = 2;
    config.arrivalProbability = 0.0;
    config.maximumRequestsPerArrival = 2;
    config.requestsPerStartingServer = 2;
    config.randomSeed = 412;
    config.defaultOutputMode = OutputMode::Quiet;
    config.blockedSourceRanges.clear();
    return config;
}

void requireAccountingInvariants(const LoadBalancer& loadBalancer) {
    const SimulationStatistics& statistics = loadBalancer.statistics();
    require(statistics.requestsGenerated ==
                statistics.requestsAccepted + statistics.requestsRejected,
            "generated request invariant failed");
    require(statistics.requestsAccepted ==
                statistics.requestsCompleted + statistics.currentQueueSize +
                    statistics.currentProcessingRequests,
            "accepted request invariant failed");
    require(statistics.currentServerCount ==
                statistics.startingServerCount + statistics.serversAdded -
                    statistics.serversRemoved,
            "server history invariant failed");
    require(statistics.currentServerCount ==
                statistics.currentProcessingRequests +
                    statistics.currentIdleServerCount,
            "busy/idle server invariant failed");
}

void testValidConfigurationParsing() {
    TemporaryConfigFile file(
        "  # whitespace and comments are allowed\n"
        " minimum_processing_time = 2\n"
        "maximum_processing_time=9 # inline comment\n"
        "scale_up_requests_per_server = 80\n"
        "scale_down_requests_per_server = 50\n"
        "scaling_cooldown_cycles = 7\n"
        "arrival_probability = 0.4\n"
        "maximum_requests_per_arrival = 4\n"
        "requests_per_starting_server = 6\n"
        "random_seed = 12345\n"
        "default_output_mode = VeRbOsE\n"
        "blocked_source_range = 10.0.0.0 - 10.255.255.255\n"
        "blocked_source_range=192.168.1.10-192.168.1.20\n");

    const SimulationConfig config = SimulationConfig::loadFromFile(file.string());
    require(config.minimumProcessingTime == 2, "minimum time parsed incorrectly");
    require(config.maximumProcessingTime == 9, "maximum time parsed incorrectly");
    require(config.scaleUpRequestsPerServer == 80, "scale-up parsed incorrectly");
    require(config.scaleDownRequestsPerServer == 50,
            "scale-down parsed incorrectly");
    require(config.scalingCooldownCycles == 7, "cooldown parsed incorrectly");
    require(config.arrivalProbability == 0.4, "probability parsed incorrectly");
    require(config.maximumRequestsPerArrival == 4, "batch size parsed incorrectly");
    require(config.requestsPerStartingServer == 6,
            "initial request ratio parsed incorrectly");
    require(config.randomSeed == 12345, "seed parsed incorrectly");
    require(config.defaultOutputMode == OutputMode::Verbose,
            "output mode parsed incorrectly");
    require(config.blockedSourceRanges.size() == 2,
            "repeated blocked ranges were not preserved");
}

void testConfigurationErrorsAreReported() {
    const std::filesystem::path missingPath =
        std::filesystem::temp_directory_path() /
        "csce412_project4_missing_configuration.txt";
    std::error_code error;
    std::filesystem::remove(missingPath, error);
    requireThrows<std::runtime_error>(
        [&] { SimulationConfig::loadFromFile(missingPath.string()); },
        "missing configuration file was accepted");

    TemporaryConfigFile missingRequired(
        "minimum_processing_time=1\nmaximum_processing_time=2\n");
    requireThrows<std::runtime_error>(
        [&] { SimulationConfig::loadFromFile(missingRequired.string()); },
        "missing required settings were accepted");

    TemporaryConfigFile invalidProbability(replaceOnce(
        validConfigText(), "arrival_probability=0.4",
        "arrival_probability=1.5"));
    requireThrows<std::runtime_error>(
        [&] { SimulationConfig::loadFromFile(invalidProbability.string()); },
        "out-of-range probability was accepted");

    TemporaryConfigFile nonFiniteProbability(replaceOnce(
        validConfigText(), "arrival_probability=0.4",
        "arrival_probability=nan"));
    requireThrows<std::runtime_error>(
        [&] { SimulationConfig::loadFromFile(nonFiniteProbability.string()); },
        "non-finite probability was accepted");

    TemporaryConfigFile duplicateKey(
        validConfigText() + "minimum_processing_time=3\n");
    requireThrows<std::runtime_error>(
        [&] { SimulationConfig::loadFromFile(duplicateKey.string()); },
        "duplicate scalar setting was accepted");

    TemporaryConfigFile unknownKey(validConfigText() + "mystery_setting=1\n");
    requireThrows<std::runtime_error>(
        [&] { SimulationConfig::loadFromFile(unknownKey.string()); },
        "unknown configuration key was ignored");

    TemporaryConfigFile invalidRange(
        validConfigText() +
        "blocked_source_range=10.0.0.20-10.0.0.10\n");
    requireThrows<std::runtime_error>(
        [&] { SimulationConfig::loadFromFile(invalidRange.string()); },
        "reversed blocked range was accepted");
}

void testIpv4RangesAreNumericAndInclusive() {
    const IpRange range{"10.0.0.2", "10.0.0.20"};
    require(range.contains("10.0.0.2"), "first range boundary was excluded");
    require(range.contains("10.0.0.10"), "numeric address ordering is incorrect");
    require(range.contains("10.0.0.20"), "last range boundary was excluded");
    require(!range.contains("10.0.0.1"), "address below range was included");
    require(!range.contains("10.0.0.21"), "address above range was included");

    requireThrows<std::invalid_argument>(
        [&] { range.contains("10.0.0.256"); },
        "malformed candidate IP was accepted");
    requireThrows<std::invalid_argument>(
        [] { IpRange{"10.0.0.20", "10.0.0.2"}.contains("10.0.0.10"); },
        "reversed range was accepted");
}

void testBlockedAndAllowedRequestAccounting() {
    SimulationConfig config = makeRuntimeConfig();
    config.blockedSourceRanges.push_back({"10.0.0.0", "10.255.255.255"});
    LoadBalancer loadBalancer(1, config, OutputMode::Quiet);
    std::ostringstream output;

    require(!loadBalancer.submitRequest(
                Request("10.1.2.3", "8.8.8.8", 1, 'P'), output),
            "blocked request was accepted");
    require(loadBalancer.submitRequest(
                Request("11.1.2.3", "8.8.8.8", 1, 'S'), output),
            "allowed request was rejected");
    require(loadBalancer.statistics().requestsGenerated == 2,
            "generated count is wrong after filtering");
    require(loadBalancer.statistics().requestsAccepted == 1,
            "accepted count is wrong after filtering");
    require(loadBalancer.statistics().requestsRejected == 1,
            "rejected count is wrong after filtering");
    require(loadBalancer.queueSize() == 1, "allowed request was not queued");
    requireAccountingInvariants(loadBalancer);
}

void testInitialQueueContinuesAfterFilteredCandidate() {
    SimulationConfig config = makeRuntimeConfig();
    config.randomSeed = 91;
    std::mt19937 previewEngine(config.randomSeed);
    const Request firstCandidate = Request::generateRandom(
        previewEngine, config.minimumProcessingTime, config.maximumProcessingTime);
    config.blockedSourceRanges.push_back(
        {firstCandidate.sourceIp(), firstCandidate.sourceIp()});

    LoadBalancer loadBalancer(1, config, OutputMode::Quiet);
    std::ostringstream output;
    loadBalancer.initializeRequestQueue(1, output);

    require(loadBalancer.queueSize() == 1, "initial accepted target was not reached");
    require(loadBalancer.statistics().startingQueueSize == 1,
            "starting queue statistic is incorrect");
    require(loadBalancer.statistics().requestsAccepted == 1,
            "accepted initialization count is wrong");
    require(loadBalancer.statistics().requestsRejected >= 1,
            "filtered initialization candidate was not counted");
    requireAccountingInvariants(loadBalancer);
}

void testAllBlockedInitializationIsBounded() {
    SimulationConfig config = makeRuntimeConfig();
    config.blockedSourceRanges.push_back({"0.0.0.0", "255.255.255.255"});
    LoadBalancer loadBalancer(1, config, OutputMode::Quiet);
    std::ostringstream output;

    requireThrows<std::runtime_error>(
        [&] { loadBalancer.initializeRequestQueue(1, output); },
        "all-blocked initialization did not terminate with an error");
    require(loadBalancer.queueSize() == 0, "blocked requests entered initial queue");
    require(loadBalancer.statistics().requestsGenerated == 100,
            "initialization attempt limit was not enforced");
    require(loadBalancer.statistics().requestsRejected == 100,
            "blocked initialization requests were not counted");
    requireAccountingInvariants(loadBalancer);
}

void testQuietOutputContainsSummaryOnly() {
    LoadBalancer loadBalancer(1, makeRuntimeConfig(), OutputMode::Quiet);
    std::ostringstream output;
    loadBalancer.initializeRequestQueue(1, output);
    loadBalancer.runCycle(output);
    loadBalancer.runCycle(output);
    require(output.str().empty(), "quiet mode printed individual events");

    loadBalancer.printFinalSummary(output);
    const std::string summary = output.str();
    requireContains(summary, "Simulation Summary", "summary heading missing");
    requireContains(summary, "Starting servers: 1", "starting servers missing");
    requireContains(summary, "Starting queue size: 1", "starting queue missing");
    requireContains(summary, "Clock cycles completed: 2", "clock total missing");
    requireContains(summary, "Processing-time range: 1-1 cycles",
                    "processing range missing");
    requireContains(summary, "Requests generated: 1", "generated count missing");
    requireContains(summary, "Requests accepted: 1", "accepted count missing");
    requireContains(summary, "Requests rejected: 0", "rejected count missing");
    requireContains(summary, "Requests completed: 1", "completed count missing");
    require(summary.find("[cycle ") == std::string::npos,
            "quiet summary contains verbose event output");
}

void testVerboseOutputReportsMeaningfulEvents() {
    SimulationConfig config = makeRuntimeConfig();
    config.arrivalProbability = 1.0;
    config.maximumRequestsPerArrival = 1;
    config.blockedSourceRanges.push_back({"10.0.0.0", "10.255.255.255"});
    LoadBalancer loadBalancer(2, config, OutputMode::Verbose);
    std::ostringstream output;

    loadBalancer.submitRequest(Request("10.1.1.1", "8.8.8.8", 1, 'P'), output);
    loadBalancer.submitRequest(Request("11.1.1.1", "8.8.8.8", 1, 'S'), output);
    loadBalancer.runCycle(output);
    loadBalancer.runCycle(output);

    const std::string events = output.str();
    requireContains(events, "rejected request", "rejection event missing");
    requireContains(events, "accepted request", "acceptance event missing");
    requireContains(events, "arrival batch", "arrival event missing");
    requireContains(events, "assigned request", "assignment event missing");
    requireContains(events, "completed request", "completion event missing");
    require(events.find("[cycle 1]") != std::string::npos,
            "cycle prefix missing from verbose output");
}

void testVerboseOutputIdentifiesBothScalingDirections() {
    std::ostringstream scaleDownOutput;
    LoadBalancer scaleDown(2, makeRuntimeConfig(), OutputMode::Verbose);
    scaleDown.runCycle(scaleDownOutput);
    requireContains(scaleDownOutput.str(), "scaled down",
                    "scale-down event missing from verbose output");

    std::ostringstream scaleUpOutput;
    LoadBalancer scaleUp(1, makeRuntimeConfig(), OutputMode::Verbose);
    for (std::size_t index = 0; index < 82; ++index) {
        const std::string source =
            "200.0." + std::to_string(index / 256U) + "." +
            std::to_string(index % 256U);
        scaleUp.submitRequest(Request(source, "8.8.8.8", 100, 'P'),
                              scaleUpOutput);
    }
    scaleUp.runCycle(scaleUpOutput);
    requireContains(scaleUpOutput.str(), "scaled up",
                    "scale-up event missing from verbose output");
}

void testFinalSummaryMatchesRuntimeState() {
    SimulationConfig config = makeRuntimeConfig();
    LoadBalancer loadBalancer(2, config, OutputMode::Quiet);
    std::ostringstream output;
    loadBalancer.initializeRequestQueue(2, output);
    loadBalancer.runCycle(output);
    loadBalancer.runCycle(output);
    loadBalancer.printFinalSummary(output);

    const SimulationStatistics& statistics = loadBalancer.statistics();
    const std::string summary = output.str();
    requireContains(summary, "Ending queue size: 0", "ending queue is incorrect");
    requireContains(summary, "Ending servers: 1", "ending server count is incorrect");
    requireContains(summary, "Busy servers: 0", "busy server count is incorrect");
    requireContains(summary, "Idle servers: 1", "idle server count is incorrect");
    requireContains(summary, "Servers removed: 1", "removal count is incorrect");
    requireContains(summary, "Minimum servers: 1", "minimum count is incorrect");
    requireContains(summary, "Maximum servers: 2", "maximum count is incorrect");
    requireContains(summary, "Scaling events: 1", "scaling total is incorrect");
    require(statistics.requestsCompleted == 2, "completion setup is incorrect");
    requireAccountingInvariants(loadBalancer);
}

void testParsedConfigurationRunsEndToEnd() {
    TemporaryConfigFile file(validConfigText());
    SimulationConfig config = SimulationConfig::loadFromFile(file.string());
    config.defaultOutputMode = OutputMode::Quiet;
    LoadBalancer loadBalancer(3, config, OutputMode::Quiet);
    std::ostringstream output;

    loadBalancer.initializeRequestQueue(
        3 * config.requestsPerStartingServer, output);
    for (int cycle = 0; cycle < 100; ++cycle) {
        loadBalancer.runCycle(output);
        requireAccountingInvariants(loadBalancer);
    }
    loadBalancer.printFinalSummary(output);

    require(loadBalancer.currentClockCycle() == 100,
            "end-to-end run stopped at the wrong cycle");
    requireContains(output.str(), "Simulation Summary",
                    "end-to-end run did not produce a summary");
    requireAccountingInvariants(loadBalancer);
}
}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"valid configuration parsing", testValidConfigurationParsing},
        {"configuration errors", testConfigurationErrorsAreReported},
        {"IPv4 numeric inclusive ranges", testIpv4RangesAreNumericAndInclusive},
        {"blocked and allowed accounting", testBlockedAndAllowedRequestAccounting},
        {"initial queue filtering", testInitialQueueContinuesAfterFilteredCandidate},
        {"bounded all-blocked initialization", testAllBlockedInitializationIsBounded},
        {"quiet summary output", testQuietOutputContainsSummaryOnly},
        {"verbose event output", testVerboseOutputReportsMeaningfulEvents},
        {"verbose scaling output",
         testVerboseOutputIdentifiesBothScalingDirections},
        {"final summary state", testFinalSummaryMatchesRuntimeState},
        {"parsed configuration end-to-end run", testParsedConfigurationRunsEndToEnd},
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
