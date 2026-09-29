#pragma once

#include "videotrust/inspection.hpp"
#include "videotrust/preservation.hpp"
#include "videotrust/result.hpp"

#include <string>

namespace videotrust {

std::string RenderText(const VerificationResult& result);
std::string RenderJson(const VerificationResult& result);
std::string RenderInspectionText(const InspectionResult& result);
std::string RenderInspectionJson(const InspectionResult& result);
std::string RenderPreservationJson(const PreservationAssessment& assessment);

}  // namespace videotrust
