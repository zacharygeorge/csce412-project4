#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "Request.h"

#include <cstddef>
#include <optional>

/**
 * @brief Executes at most one Request, one simulated clock cycle at a time.
 *
 * LoadBalancer assigns a queued Request only while the server is idle. Each
 * call to advanceOneCycle() consumes exactly one unit of the assigned request's
 * processing time. On the finishing call, the completed Request is returned to
 * LoadBalancer for accounting and the server becomes idle again.
 *
 * @invariant An idle server has no current request and zero remaining time.
 * @invariant A busy server has one current request and positive remaining time.
 */
class WebServer {
public:
    /**
     * @param identifier Positive stable identifier used in verbose event messages.
     * @throws std::invalid_argument when identifier is zero.
     * @post The new server is idle with zero remaining processing time.
     */
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
     */
    void acceptRequest(Request request);

    /**
     * Advances the current request by exactly one clock cycle.
     *
     * @return The completed request on its finishing cycle; otherwise std::nullopt.
     * Calling this method while idle is safe and returns std::nullopt.
     * @post A completed request is removed and the server becomes idle.
     */
    std::optional<Request> advanceOneCycle();

private:
    std::size_t identifier_;
    std::optional<Request> currentRequest_;
    int remainingProcessingTime_ = 0;
};

#endif
