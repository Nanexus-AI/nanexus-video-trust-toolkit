#pragma once

#include <string>
#include <utility>
#include <variant>

namespace videotrust {

enum class ErrorCode {
  Ok = 0,
  InvalidArgument,
  NotSupported,
  IoFailure,
  ParseError,
  UpstreamFailure,
  InternalError,
};

struct Error {
  ErrorCode code{ErrorCode::InternalError};
  std::string message;
};

inline Error MakeError(ErrorCode code, std::string message) {
  return Error{code, std::move(message)};
}

/// Value-or-error without exceptions for expected operational failures.
template <typename T>
class Expected {
 public:
  Expected(T value) : data_(std::move(value)) {}
  Expected(Error error) : data_(std::move(error)) {}

  bool ok() const noexcept { return std::holds_alternative<T>(data_); }
  explicit operator bool() const noexcept { return ok(); }

  T& value() & { return std::get<T>(data_); }
  const T& value() const& { return std::get<T>(data_); }
  T&& value() && { return std::get<T>(std::move(data_)); }

  Error& error() & { return std::get<Error>(data_); }
  const Error& error() const& { return std::get<Error>(data_); }

 private:
  std::variant<T, Error> data_;
};

const char* ToString(ErrorCode code) noexcept;

}  // namespace videotrust
