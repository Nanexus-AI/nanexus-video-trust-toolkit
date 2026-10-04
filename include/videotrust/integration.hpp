#pragma once

#include "videotrust/compare.hpp"
#include "videotrust/error.hpp"
#include "videotrust/inspection.hpp"
#include "videotrust/preservation.hpp"
#include "videotrust/result.hpp"
#include "videotrust/verify.hpp"

#include <string>
#include <variant>

namespace videotrust {

enum class IntegrationOperation { VerifyFile, InspectFile, ComparePreservation };

struct IntegrationVerifyRequest {
  VerifyOptions options;
};

struct IntegrationInspectRequest {
  VerifyOptions options;
};

struct IntegrationCompareRequest {
  PreservationCompareOptions options;
};

using IntegrationRequest =
    std::variant<IntegrationVerifyRequest, IntegrationInspectRequest,
                 IntegrationCompareRequest>;

using IntegrationDomainResult =
    std::variant<VerificationResult, InspectionResult, PreservationAssessment>;

struct IntegrationResult {
  IntegrationOperation operation;
  IntegrationDomainResult domain;
};

/// Execute one already-validated finite integration request through the
/// existing domain entry points. This is a source-level facade, not a stable
/// binary ABI and not a second trust implementation.
Expected<IntegrationResult> ExecuteIntegration(const IntegrationRequest& request);

const char* ToString(IntegrationOperation operation) noexcept;
const char* IntegrationDocumentType(IntegrationOperation operation) noexcept;
std::string RenderIntegrationDomainJson(const IntegrationResult& result);

}  // namespace videotrust
