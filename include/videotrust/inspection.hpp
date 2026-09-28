#pragma once

#include "videotrust/result.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace videotrust {

/// Accumulated session statistics from onvif_media_signing_accumulated_validation_t.
/// Counts are unsigned totals copied from upstream; zero means zero, not "unknown".
/// Timestamps are FILETIME units: 100-nanosecond intervals since 1601-01-01 UTC.
/// Upstream does not separately signal "timestamp unavailable" vs a zero FILETIME.
struct AccumulatedValidationObservations {
  unsigned received_nalus{0};
  unsigned validated_nalus{0};
  unsigned pending_nalus{0};
  unsigned received_frames{0};
  unsigned validated_frames{0};
  unsigned pending_frames{0};
  std::int64_t first_timestamp{0};
  std::int64_t last_timestamp{0};
};

/// Latest validation-span observations from onvif_media_signing_latest_validation_t.
/// Hashable NAL counts use nullopt when upstream reports a negative sentinel
/// (expected: missing/tampered SEI; received: signing disabled or validation error;
/// pending: treated the same if negative, though upstream docs do not require it).
/// Timestamps use the same FILETIME unit as accumulated observations.
struct LatestValidationObservations {
  std::optional<int> expected_hashable_nalus;
  std::optional<int> received_hashable_nalus;
  std::optional<int> pending_hashable_nalus;
  std::int64_t start_timestamp{0};
  std::int64_t end_timestamp{0};
};

/// Vendor product strings from onvif_media_signing_vendor_info_t.
/// Empty upstream C strings map to nullopt. These are metadata observations only;
/// they must not alter trust axes or source authenticity.
struct VendorObservations {
  std::optional<std::string> manufacturer;
  std::optional<std::string> firmware_version;
  std::optional<std::string> serial_number;
};

/// M2 inspection domain result. Embeds a copy of M1 VerificationResult for context.
/// Inspection fields never feed DeriveOverallState or ExitCodeForVerification.
struct InspectionResult {
  VerificationResult verification{};
  AccumulatedValidationObservations accumulated{};
  LatestValidationObservations latest{};
  VendorObservations vendor{};
};

}  // namespace videotrust
