#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <utility>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Typed error semantics. Important failures are never reduced to a bare
// string; code() carries the classification and message()/context() carry
// enough inspection context without exposing backend handles or pointers.
//---------------------------------------------------------------------------
enum class ErrorCode : std::uint32_t {
    OK = 0u,
    NOT_FOUND,
    UNSUPPORTED,
    INVALID_ARGUMENT,
    INVALID_TOPOLOGY,
    STALE_GENERATION,
    STALE_BOOT,
    STALE_EPOCH,
    STALE_MEASUREMENT,
    REVALIDATION_REQUIRED,
    BACKEND_UNAVAILABLE,
    BACKEND_ERROR,
    LINK_DOWN,
    NO_PATH,
    INSUFFICIENT_EVIDENCE,
    INTEGRITY_FAILURE,
    PROTOCOL_ERROR,
    RESOURCE_LIMIT,
    CANCELLED
};

inline const char* error_code_name(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::OK:                   return "OK";
        case ErrorCode::NOT_FOUND:            return "NOT_FOUND";
        case ErrorCode::UNSUPPORTED:          return "UNSUPPORTED";
        case ErrorCode::INVALID_ARGUMENT:     return "INVALID_ARGUMENT";
        case ErrorCode::INVALID_TOPOLOGY:     return "INVALID_TOPOLOGY";
        case ErrorCode::STALE_GENERATION:     return "STALE_GENERATION";
        case ErrorCode::STALE_BOOT:           return "STALE_BOOT";
        case ErrorCode::STALE_EPOCH:          return "STALE_EPOCH";
        case ErrorCode::STALE_MEASUREMENT:    return "STALE_MEASUREMENT";
        case ErrorCode::REVALIDATION_REQUIRED:return "REVALIDATION_REQUIRED";
        case ErrorCode::BACKEND_UNAVAILABLE:  return "BACKEND_UNAVAILABLE";
        case ErrorCode::BACKEND_ERROR:        return "BACKEND_ERROR";
        case ErrorCode::LINK_DOWN:            return "LINK_DOWN";
        case ErrorCode::NO_PATH:              return "NO_PATH";
        case ErrorCode::INSUFFICIENT_EVIDENCE:return "INSUFFICIENT_EVIDENCE";
        case ErrorCode::INTEGRITY_FAILURE:    return "INTEGRITY_FAILURE";
        case ErrorCode::PROTOCOL_ERROR:       return "PROTOCOL_ERROR";
        case ErrorCode::RESOURCE_LIMIT:       return "RESOURCE_LIMIT";
        case ErrorCode::CANCELLED:            return "CANCELLED";
    }
    return "UNKNOWN";
}

class Error {
public:
    Error() = default;
    Error(ErrorCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    ErrorCode code() const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }
    bool ok() const noexcept { return code_ == ErrorCode::OK; }
    explicit operator bool() const noexcept { return !ok(); }

    Error& set_context(std::string name, std::string value) {
        context_[std::move(name)] = std::move(value);
        return *this;
    }
    const std::map<std::string, std::string>& context() const noexcept { return context_; }

    std::string to_string() const {
        std::string out = std::string(error_code_name(code_));
        if (!message_.empty()) {
            out += ": ";
            out += message_;
        }
        if (!context_.empty()) {
            out += " {";
            for (const auto& kv : context_) {
                out += " ";
                out += kv.first;
                out += "=";
                out += kv.second;
            }
            out += " }";
        }
        return out;
    }

private:
    ErrorCode code_{ErrorCode::OK};
    std::string message_;
    std::map<std::string, std::string> context_;
};

inline Error make_error(ErrorCode c, std::string m) {
    return Error(c, std::move(m));
}

}  // namespace nvlinkfabric
