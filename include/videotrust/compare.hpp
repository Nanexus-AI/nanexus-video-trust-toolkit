#pragma once

#include "videotrust/correlation.hpp"
#include "videotrust/error.hpp"
#include "videotrust/preservation.hpp"

#include <string>

namespace videotrust {

struct PreservationCompareOptions {
  Codec codec{Codec::H264};
  std::string before_path;
  std::string after_path;
  std::string before_ca_pem_path;
  std::string after_ca_pem_path;
  TransformationContext transformation;
  CorrelationLimits correlation_limits{};
};

/// Inspect each artifact once, correlate them once, and derive the existing
/// preservation domain assessment. Paths are inputs only and are not retained
/// in the result or its renderers.
Expected<PreservationAssessment> ComparePreservation(
    const PreservationCompareOptions& options);

}  // namespace videotrust
