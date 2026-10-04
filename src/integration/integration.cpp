#include "videotrust/integration.hpp"

#include "videotrust/render.hpp"

#include <type_traits>

namespace videotrust {

Expected<IntegrationResult> ExecuteIntegration(const IntegrationRequest& request) {
  return std::visit(
      [](const auto& typed) -> Expected<IntegrationResult> {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, IntegrationVerifyRequest>) {
          auto result = VerifyAnnexBFile(typed.options);
          if (!result.ok()) return result.error();
          return IntegrationResult{IntegrationOperation::VerifyFile,
                                   std::move(result.value())};
        } else if constexpr (std::is_same_v<T, IntegrationInspectRequest>) {
          auto result = InspectAnnexBFile(typed.options);
          if (!result.ok()) return result.error();
          return IntegrationResult{IntegrationOperation::InspectFile,
                                   std::move(result.value())};
        } else {
          auto result = ComparePreservation(typed.options);
          if (!result.ok()) return result.error();
          return IntegrationResult{IntegrationOperation::ComparePreservation,
                                   std::move(result.value())};
        }
      },
      request);
}

const char* ToString(IntegrationOperation operation) noexcept {
  switch (operation) {
    case IntegrationOperation::VerifyFile:
      return "verify_file";
    case IntegrationOperation::InspectFile:
      return "inspect_file";
    case IntegrationOperation::ComparePreservation:
      return "compare_preservation";
  }
  return "unknown";
}

const char* IntegrationDocumentType(IntegrationOperation operation) noexcept {
  switch (operation) {
    case IntegrationOperation::VerifyFile:
      return "media_signing_verification";
    case IntegrationOperation::InspectFile:
      return "media_signing_inspection";
    case IntegrationOperation::ComparePreservation:
      return "media_signing_preservation_assessment";
  }
  return "unknown";
}

std::string RenderIntegrationDomainJson(const IntegrationResult& result) {
  return std::visit(
      [](const auto& domain) -> std::string {
        using T = std::decay_t<decltype(domain)>;
        if constexpr (std::is_same_v<T, VerificationResult>) {
          return RenderJson(domain);
        } else if constexpr (std::is_same_v<T, InspectionResult>) {
          return RenderInspectionJson(domain);
        } else {
          return RenderPreservationJson(domain);
        }
      },
      result.domain);
}

}  // namespace videotrust
