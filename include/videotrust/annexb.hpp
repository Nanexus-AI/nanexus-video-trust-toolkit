#pragma once

#include "videotrust/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace videotrust {

/// One Annex-B access unit NAL: start code bytes + NAL unit bytes.
/// Upstream ONVIF APIs require Start Code + NAL Unit (see validator.h).
struct NalUnit {
  std::vector<uint8_t> bytes;  ///< Includes 3- or 4-byte start code.
  std::size_t start_code_size{0};
};

/// Minimal Annex-B splitter. Does not decode video.
class AnnexBReader {
 public:
  /// Parse a complete in-memory Annex-B buffer into ordered NAL units.
  static Expected<std::vector<NalUnit>> Parse(std::span<const uint8_t> data);

  /// Read an entire file then Parse.
  static Expected<std::vector<NalUnit>> ParseFile(const std::string& path);
};

}  // namespace videotrust
