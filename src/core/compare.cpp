#include "videotrust/compare.hpp"

#include "videotrust/verify.hpp"

namespace videotrust {

Expected<PreservationAssessment> ComparePreservation(
    const PreservationCompareOptions& options) {
  VerifyOptions before_options;
  before_options.codec = options.codec;
  before_options.input_path = options.before_path;
  before_options.ca_pem_path = options.before_ca_pem_path;
  auto before = InspectAnnexBFile(before_options);
  if (!before.ok()) return before.error();

  VerifyOptions after_options;
  after_options.codec = options.codec;
  after_options.input_path = options.after_path;
  after_options.ca_pem_path = options.after_ca_pem_path;
  auto after = InspectAnnexBFile(after_options);
  if (!after.ok()) return after.error();

  CorrelationOptions correlation_options;
  correlation_options.codec = options.codec;
  correlation_options.before_path = options.before_path;
  correlation_options.after_path = options.after_path;
  correlation_options.limits = options.correlation_limits;
  auto correlation = CorrelateAnnexBFiles(correlation_options);
  if (!correlation.ok()) return correlation.error();

  return DerivePreservationAssessment(before.value(), after.value(),
                                      correlation.value(),
                                      options.transformation);
}

}  // namespace videotrust
