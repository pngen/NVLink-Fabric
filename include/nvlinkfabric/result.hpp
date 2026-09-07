#pragma once
#include "nvlinkfabric/error.hpp"
#include <variant>
#include <utility>
#include <stdexcept>

namespace nvlinkfabric {

//---------------------------------------------------------------------------
// Result<T>: a typed value-or-error transport. Use has_value()/value() for the
// value and error() for the typed error. It is never a bare string result.
//---------------------------------------------------------------------------
template <class T>
class Result {
public:
    Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}
    Result(const Error& e) : data_(std::in_place_index<1>, e) {}
    Result(Error&& e) : data_(std::in_place_index<1>, std::move(e)) {}

    bool has_value() const noexcept { return data_.index() == 0u; }
    explicit operator bool() const noexcept { return has_value(); }

    T& value() & {
        if (!has_value()) throw_no_value();
        return std::get<0>(data_);
    }
    const T& value() const & {
        if (!has_value()) throw_no_value();
        return std::get<0>(data_);
    }
    T&& value() && {
        if (!has_value()) throw_no_value();
        return std::move(std::get<0>(data_));
    }

    T& operator*() & { return value(); }
    const T& operator*() const & { return value(); }
    T* operator->() & { return &value(); }
    const T* operator->() const & { return &value(); }

    const Error& error() const noexcept {
        return std::get<1>(data_);
    }

    T value_or(T def) const & {
        return has_value() ? std::get<0>(data_) : std::move(def);
    }

    static Result ok(T v) { return Result(std::move(v)); }
    static Result err(Error e) { return Result(std::move(e)); }
    static Result err(ErrorCode c, std::string m) { return Result(Error(c, std::move(m))); }

private:
    [[noreturn]] void throw_no_value() const {
        throw std::runtime_error("Result has no value: " + std::get<1>(data_).to_string());
    }
    std::variant<T, Error> data_;
};

//---------------------------------------------------------------------------
// Result<void> specialization.
//---------------------------------------------------------------------------
template <>
class Result<void> {
public:
    Result() : ok_(true) {}
    Result(Error e) : ok_(false), err_(std::move(e)) {}
    Result(ErrorCode c, std::string m) : ok_(false), err_(c, std::move(m)) {}

    bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    const Error& error() const noexcept { return err_; }

    static Result ok() { return Result(); }
    static Result err(Error e) { return Result(std::move(e)); }
    static Result err(ErrorCode c, std::string m) { return Result(c, std::move(m)); }

private:
    bool ok_{false};
    Error err_;
};

//---------------------------------------------------------------------------
// Convenience for building an Error.
//---------------------------------------------------------------------------
inline Error error_of(ErrorCode c, std::string m) {
    return Error(c, std::move(m));
}

}  // namespace nvlinkfabric
