#pragma once

#include "videotrust/result.hpp"

#include <string>

namespace videotrust {

std::string RenderText(const VerificationResult& result);
std::string RenderJson(const VerificationResult& result);

}  // namespace videotrust
