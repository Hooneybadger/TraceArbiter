#pragma once

#include <optional>
#include <string>
#include <utility>

namespace tracearbiter {

struct Error {
    std::string code;
    std::string message;
};

inline Error make_error(std::string code, std::string message) {
    return Error{std::move(code), std::move(message)};
}

template <typename T>
class Result {
public:
    static Result ok(T value) {
        Result result;
        result.value_ = std::move(value);
        return result;
    }

    static Result err(Error error) {
        Result result;
        result.error_ = std::move(error);
        return result;
    }

    explicit operator bool() const { return value_.has_value(); }
    bool has_value() const { return value_.has_value(); }

    T& value() & { return *value_; }
    const T& value() const& { return *value_; }
    T&& value() && { return std::move(*value_); }

    const Error& error() const { return *error_; }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

} // namespace tracearbiter
