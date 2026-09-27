#pragma once

#include "videotrust/error.hpp"
#include "videotrust/result.hpp"

#include <cstddef>
#include <string>

namespace videotrust {

enum class TamperOperation {
  CorruptVcl,
  StripSigningSei,
  Truncate,
};

struct TamperOptions {
  Codec codec{Codec::H264};
  TamperOperation operation{TamperOperation::CorruptVcl};
  std::string input_path;
  std::string output_path;
  bool force{false};
  /// truncate only: number of trailing NAL units to remove (default 1).
  std::size_t truncate_nal_count{1};
};

/// Apply a deterministic Annex-B tamper transformation to a new output file.
Error TamperAnnexBFile(const TamperOptions& options);

ExitCode ExitCodeForTamper(const Error& error) noexcept;

const char* ToString(TamperOperation op) noexcept;

}  // namespace videotrust
