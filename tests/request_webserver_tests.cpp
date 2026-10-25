#include "Request.h"
#include "WebServer.h"

#include <cctype>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
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

bool isValidIpv4(const std::string& address) {
    std::istringstream stream(address);
    std::string octet;
    int count = 0;

    while (std::getline(stream, octet, '.')) {
        if (octet.empty() || octet.size() > 3) {
            return false;
        }
        for (const unsigned char character : octet) {
            if (!std::isdigit(character)) {
                return false;
            }
        }
        const int value = std::stoi(octet);
        if (value < 0 || value > 255) {
            return false;
        }
        ++count;
    }
    return count == 4 && !address.empty() && address.back() != '.';
}

void testRequestConstructionAndAccessors() {
    const Request request("10.20.30.40", "172.16.1.5", 42, 'S');

    require(request.sourceIp() == "10.20.30.40", "source IP was not preserved");
    require(request.destinationIp() == "172.16.1.5",
            "destination IP was not preserved");
    require(request.processingTime() == 42, "processing time was not preserved");
    require(request.jobType() == 'S', "job type was not preserved");
}

void testRequestRejectsInvalidFields() {
    requireThrows<std::invalid_argument>(
        [] { Request("", "1.2.3.4", 1, 'P'); }, "empty source IP was accepted");
    requireThrows<std::invalid_argument>(
        [] { Request("1.2.3.4", "", 1, 'P'); }, "empty destination IP was accepted");
    requireThrows<std::invalid_argument>(
        [] { Request("1.2.3", "4.5.6.7", 1, 'P'); }, "short IPv4 address was accepted");
    requireThrows<std::invalid_argument>(
        [] { Request("1.2.3.256", "4.5.6.7", 1, 'P'); },
        "out-of-range IPv4 address was accepted");
    requireThrows<std::invalid_argument>(
        [] { Request("1.2.3.4", "4.5.6.7", 0, 'P'); },
        "zero processing time was accepted");
    requireThrows<std::invalid_argument>(
        [] { Request("1.2.3.4", "4.5.6.7", -1, 'P'); },
        "negative processing time was accepted");
    requireThrows<std::invalid_argument>(
        [] { Request("1.2.3.4", "4.5.6.7", 1, 'X'); },
        "unsupported job type was accepted");
}

void testRandomRequestsRespectBoundsAndFormats() {
    std::mt19937 randomEngine(412);

    for (int index = 0; index < 500; ++index) {
        const Request request = Request::generateRandom(randomEngine, 5, 25);
        require(isValidIpv4(request.sourceIp()), "generated source IP is invalid");
        require(isValidIpv4(request.destinationIp()),
                "generated destination IP is invalid");
        require(request.processingTime() >= 5 && request.processingTime() <= 25,
                "generated processing time is outside configured bounds");
        require(Request::isSupportedJobType(request.jobType()),
                "generated job type is not P or S");
    }
}

void testRandomRequestRejectsInvalidBounds() {
    std::mt19937 randomEngine(412);
    requireThrows<std::invalid_argument>(
        [&] { Request::generateRandom(randomEngine, 0, 10); },
        "non-positive minimum processing time was accepted");
    requireThrows<std::invalid_argument>(
        [&] { Request::generateRandom(randomEngine, 10, 9); },
        "reversed processing-time bounds were accepted");
}

void testRandomRequestSequenceIsReproducible() {
    std::mt19937 firstEngine(12345);
    std::mt19937 secondEngine(12345);

    for (int index = 0; index < 50; ++index) {
        const Request first = Request::generateRandom(firstEngine, 1, 100);
        const Request second = Request::generateRandom(secondEngine, 1, 100);
        require(first.sourceIp() == second.sourceIp(), "source IP sequence differs");
        require(first.destinationIp() == second.destinationIp(),
                "destination IP sequence differs");
        require(first.processingTime() == second.processingTime(),
                "processing-time sequence differs");
        require(first.jobType() == second.jobType(), "job-type sequence differs");
    }
}

void testNewServerIsIdle() {
    const WebServer server(7);
    require(server.identifier() == 7, "server identifier was not preserved");
    require(server.isIdle(), "new server is not idle");
    require(!server.isBusy(), "new server is unexpectedly busy");
    require(!server.currentRequest().has_value(), "new server has a request");
    require(server.remainingProcessingTime() == 0,
            "new server has nonzero remaining time");
}

void testServerRejectsInvalidIdentifier() {
    requireThrows<std::invalid_argument>([] { WebServer(0); },
                                         "zero server identifier was accepted");
}

void testServerAcceptsRequestAndBecomesBusy() {
    WebServer server(1);
    server.acceptRequest(Request("1.1.1.1", "2.2.2.2", 3, 'P'));

    require(server.isBusy(), "assigned server is not busy");
    require(!server.isIdle(), "assigned server is still idle");
    require(server.remainingProcessingTime() == 3,
            "remaining time does not match assigned request");
    require(server.currentRequest().has_value(), "assigned request is missing");
    require(server.currentRequest()->sourceIp() == "1.1.1.1",
            "server stored the wrong request");
}

void testRequestCompletesAfterExactNumberOfCycles() {
    WebServer server(1);
    server.acceptRequest(Request("1.1.1.1", "2.2.2.2", 3, 'P'));

    require(!server.advanceOneCycle().has_value(), "request completed after one cycle");
    require(server.remainingProcessingTime() == 2, "remaining time should be two");
    require(!server.advanceOneCycle().has_value(), "request completed after two cycles");
    require(server.remainingProcessingTime() == 1, "remaining time should be one");

    const std::optional<Request> completed = server.advanceOneCycle();
    require(completed.has_value(), "request did not complete after three cycles");
    require(completed->sourceIp() == "1.1.1.1", "wrong completed request returned");
    require(server.isIdle(), "server did not become idle after completion");
    require(server.remainingProcessingTime() == 0,
            "remaining time was not reset after completion");
}

void testCompletedRequestIsReturnedOnlyOnce() {
    WebServer server(1);
    server.acceptRequest(Request("1.1.1.1", "2.2.2.2", 1, 'S'));

    require(server.advanceOneCycle().has_value(), "one-cycle request did not complete");
    require(!server.advanceOneCycle().has_value(),
            "completed request was returned more than once");
    require(!server.currentRequest().has_value(),
            "completed request remained assigned to server");
}

void testServerCanAcceptAnotherRequestAfterCompletion() {
    WebServer server(1);
    server.acceptRequest(Request("1.1.1.1", "2.2.2.2", 1, 'P'));
    require(server.advanceOneCycle().has_value(), "first request did not complete");

    server.acceptRequest(Request("3.3.3.3", "4.4.4.4", 2, 'S'));
    require(server.isBusy(), "server did not accept a second request");
    require(server.currentRequest()->sourceIp() == "3.3.3.3",
            "server retained the first request");
    require(server.remainingProcessingTime() == 2,
            "second request remaining time is incorrect");
}

void testBusyServerCannotOverwriteRequest() {
    WebServer server(1);
    server.acceptRequest(Request("1.1.1.1", "2.2.2.2", 3, 'P'));

    requireThrows<std::logic_error>(
        [&] { server.acceptRequest(Request("9.9.9.9", "8.8.8.8", 1, 'S')); },
        "busy server accepted a replacement request");
    require(server.currentRequest()->sourceIp() == "1.1.1.1",
            "busy assignment overwrote the original request");
    require(server.remainingProcessingTime() == 3,
            "busy assignment changed remaining processing time");
}
}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"Request construction and accessors", testRequestConstructionAndAccessors},
        {"Request invalid fields", testRequestRejectsInvalidFields},
        {"Request random bounds and formats", testRandomRequestsRespectBoundsAndFormats},
        {"Request invalid random bounds", testRandomRequestRejectsInvalidBounds},
        {"Request reproducible sequence", testRandomRequestSequenceIsReproducible},
        {"WebServer starts idle", testNewServerIsIdle},
        {"WebServer invalid identifier", testServerRejectsInvalidIdentifier},
        {"WebServer accepts work", testServerAcceptsRequestAndBecomesBusy},
        {"WebServer exact completion cycle", testRequestCompletesAfterExactNumberOfCycles},
        {"WebServer returns completion once", testCompletedRequestIsReturnedOnlyOnce},
        {"WebServer accepts later work", testServerCanAcceptAnotherRequestAfterCompletion},
        {"WebServer rejects overwrite", testBusyServerCannotOverwriteRequest},
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
