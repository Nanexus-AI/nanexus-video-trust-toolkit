#include "videotrust/error.hpp"

namespace videotrust {

const char* ToString(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:
      return "ok";
    case ErrorCode::InvalidArgument:
      return "invalid_argument";
    case ErrorCode::NotSupported:
      return "not_supported";
    case ErrorCode::IoFailure:
      return "io_failure";
    case ErrorCode::ParseError:
      return "parse_error";
    case ErrorCode::UpstreamFailure:
      return "upstream_failure";
    case ErrorCode::InternalError:
      return "internal_error";
  }
  return "unknown";
}

}  // namespace videotrust
