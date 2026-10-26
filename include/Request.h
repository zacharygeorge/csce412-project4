#ifndef REQUEST_H
#define REQUEST_H

#include <random>
#include <string>

/**
 * @brief Immutable description of one unit of work submitted to the load balancer.
 *
 * A request stores the source and destination IPv4 addresses, the number of
 * clock cycles required to process it, and its processing (`P`) or streaming
 * (`S`) job type. LoadBalancer generates and queues Request values, then moves
 * them into idle WebServer objects. The object itself does not track execution
 * progress; WebServer owns that state.
 */
class Request {
public:
    /**
     * Creates a request with all assignment-required fields.
     *
     * @param sourceIp Requester's dotted-decimal IPv4 address.
     * @param destinationIp Destination dotted-decimal IPv4 address.
     * @param processingTime Required positive number of clock cycles.
     * @param jobType 'P' for processing or 'S' for streaming.
     * @throws std::invalid_argument for an invalid IPv4 address, non-positive time,
     *         or unsupported job type.
     */
    Request(std::string sourceIp,
            std::string destinationIp,
            int processingTime,
            char jobType);

    /**
     * Generates a request using the caller-owned reproducible random engine.
     *
     * @param randomEngine Engine shared by the LoadBalancer for deterministic runs.
     * @param minimumProcessingTime Inclusive lower processing-time bound.
     * @param maximumProcessingTime Inclusive upper processing-time bound.
     * @return A request with random IPs, processing time, and P/S job type.
     * @throws std::invalid_argument if the processing-time range is invalid.
     */
    static Request generateRandom(std::mt19937& randomEngine,
                                  int minimumProcessingTime,
                                  int maximumProcessingTime);

    /** @return The requester's source IPv4 address. */
    const std::string& sourceIp() const noexcept;

    /** @return The destination IPv4 address. */
    const std::string& destinationIp() const noexcept;

    /** @return The total number of processing cycles requested. */
    int processingTime() const noexcept;

    /** @return 'P' for processing or 'S' for streaming. */
    char jobType() const noexcept;

    /**
     * @param jobType Candidate job-type character.
     * @return true only for assignment-supported job types 'P' and 'S'.
     */
    static bool isSupportedJobType(char jobType) noexcept;

private:
    std::string sourceIp_;
    std::string destinationIp_;
    int processingTime_;
    char jobType_;
};

#endif
