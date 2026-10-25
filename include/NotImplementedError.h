#ifndef NOT_IMPLEMENTED_ERROR_H
#define NOT_IMPLEMENTED_ERROR_H

#include <stdexcept>
#include <string>

/** Distinguishes deliberate Milestone A stubs from invalid user/configuration data. */
class NotImplementedError : public std::logic_error {
public:
    /** @param operation Name of the unfinished operation that was reached. */
    explicit NotImplementedError(const std::string& operation)
        : std::logic_error(operation +
                           " is intentionally not implemented in Milestone A") {}
};

#endif
