#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "Request.h"

#include <cstddef>
#include <optional>

/** A simulated web server that processes at most one request at a time. */
class WebServer {
public:
    /** @param identifier Stable identifier used in verbose event messages. */
    explicit WebServer(std::size_t identifier);

    /** @return This server's stable identifier. */
    std::size_t identifier() const noexcept;

    /** @return true when the server owns a request that has not completed. */
    bool isBusy() const noexcept;

    /** @return true when the server can accept a request. */
    bool isIdle() const noexcept;

    /** @return Clock cycles still required by the current request, or zero if idle. */
    int remainingProcessingTime() const noexcept;

    /** @return The current request, or std::nullopt when this server is idle. */
    const std::optional<Request>& currentRequest() const noexcept;

    /**
     * Transfers one queued request to this server.
     *
     * @param request Request value removed from the LoadBalancer queue.
     * @throws std::logic_error if this server is already busy.
     * @post The server is busy and remaining time equals request.processingTime().
     * @note Assignment behavior is intentionally unimplemented in Milestone A.
     */
    void acceptRequest(Request request);

    /**
     * Advances the current request by exactly one clock cycle.
     *
     * @return The completed request on its finishing cycle; otherwise std::nullopt.
     * @post A completed request is removed and the server becomes idle.
     * @note Processing behavior is intentionally unimplemented in Milestone A.
     */
    std::optional<Request> advanceOneCycle();

private:
    std::size_t identifier_;
    std::optional<Request> currentRequest_;
    int remainingProcessingTime_ = 0;
};

#endif
