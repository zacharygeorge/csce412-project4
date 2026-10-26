#ifndef CONFIG_H
#define CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/** Controls whether the simulation prints only summaries or individual events. */
enum class OutputMode {
    Quiet,
    Verbose
};

/** An inclusive IPv4 address range used to block request source addresses. */
struct IpRange {
    /** Inclusive first IPv4 address in dotted-decimal notation. */
    std::string firstAddress;
    /** Inclusive last IPv4 address in dotted-decimal notation. */
    std::string lastAddress;

    /**
     * Reports whether an IPv4 address is inside this inclusive range.
     *
     * @param address Dotted-decimal IPv4 address to test.
     * @return true when address is valid and between both endpoints.
     * @throws std::invalid_argument when this range or address is malformed.
     */
    bool contains(std::string_view address) const;
};

/**
 * @brief Validated runtime settings loaded before a LoadBalancer is constructed.
 *
 * Settings control request duration, arrival traffic, dynamic scaling, random
 * reproducibility, output mode, initial workload size, and blocked source-IP
 * ranges. Scalar settings occur once in the configuration file; blocked ranges
 * may be repeated.
 */
struct SimulationConfig {
    /** Inclusive minimum randomly generated processing duration. */
    int minimumProcessingTime = 1;
    /** Inclusive maximum randomly generated processing duration. */
    int maximumProcessingTime = 100;
    /** Strict scale-up queue multiplier. */
    std::size_t scaleUpRequestsPerServer = 80;
    /** Strict scale-down queue multiplier. */
    std::size_t scaleDownRequestsPerServer = 50;
    /** Minimum cycles between successful server-count changes. */
    std::uint64_t scalingCooldownCycles = 100;
    /** Per-cycle probability of generating one arrival batch. */
    double arrivalProbability = 0.25;
    /** Inclusive maximum uniformly chosen arrival-batch size. */
    std::size_t maximumRequestsPerArrival = 3;
    /** Accepted startup requests requested per initial server. */
    std::size_t requestsPerStartingServer = 100;
    /** Seed used by the load balancer's reproducible random engine. */
    std::uint32_t randomSeed = 412;
    /** Mode offered as the default at the interactive prompt. */
    OutputMode defaultOutputMode = OutputMode::Quiet;
    /** Inclusive source-IP ranges rejected before queue admission. */
    std::vector<IpRange> blockedSourceRanges;

    /**
     * Loads configuration values from a key-value text file.
     *
     * @param path Path to the external configuration file.
     * @return A complete, validated configuration.
     * @throws std::runtime_error when the file cannot be read or a value is invalid.
     */
    static SimulationConfig loadFromFile(const std::string& path);

    /**
     * Validates relationships and ranges among all configuration values.
     *
     * @throws std::invalid_argument if any setting would make the simulation unsafe
     *         or ambiguous. Successful return guarantees positive processing times,
     *         ordered scaling thresholds, and a valid arrival probability.
     */
    void validate() const;
};

/**
 * Converts a user-supplied mode name (q/quiet or v/verbose) to OutputMode.
 *
 * @param text User input; comparison is case-insensitive.
 * @return The selected output mode.
 * @throws std::invalid_argument when text does not identify a supported mode.
 */
OutputMode parseOutputMode(std::string_view text);

/** @return A stable lowercase name for use in summaries and prompts. */
std::string_view toString(OutputMode mode) noexcept;

#endif
