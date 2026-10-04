#include "videotrust/integration.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr std::size_t kMaxRequestBytes = 64 * 1024;
constexpr std::size_t kMaxResponseBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxRequestIdBytes = 64;
constexpr std::size_t kMaxPathBytes = 4096;
constexpr std::size_t kMaxPipelineIdBytes = 128;
constexpr std::size_t kMaxRoots = 16;
constexpr std::string_view kRequestType = "nanexus_video_trust_integration_request";
constexpr std::string_view kResponseType = "nanexus_video_trust_integration_response";
constexpr std::string_view kSchemaVersion = "0.1";

struct Failure {
  std::string code;
  std::string message;
};

struct AuthorizedPath {
  std::string canonical;
  std::string reference;
  std::string sha256;
};

struct EvidenceInput {
  std::string role;
  std::string reference;
  std::string sha256;
};

struct ParsedRequest {
  std::string request_id;
  videotrust::IntegrationRequest request;
  std::vector<EvidenceInput> references;
};

std::optional<std::string> Sha256(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  EVP_MD_CTX* raw = EVP_MD_CTX_new();
  if (raw == nullptr) return std::nullopt;
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(raw, EVP_MD_CTX_free);
  if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) return std::nullopt;
  char buffer[64 * 1024];
  while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
    if (EVP_DigestUpdate(context.get(), buffer,
                         static_cast<std::size_t>(input.gcount())) != 1) {
      return std::nullopt;
    }
  }
  if (!input.eof()) return std::nullopt;
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned length = 0;
  if (EVP_DigestFinal_ex(context.get(), digest, &length) != 1 || length != 32) {
    return std::nullopt;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned i = 0; i < length; ++i) out << std::setw(2) << unsigned(digest[i]);
  return out.str();
}

bool Token(std::string_view value) {
  if (value.empty() || value.size() > kMaxRequestIdBytes) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
  });
}

bool ClosedObject(const json& value, const std::set<std::string>& allowed) {
  if (!value.is_object()) return false;
  for (auto it = value.begin(); it != value.end(); ++it) {
    if (!allowed.contains(it.key())) return false;
  }
  return true;
}

std::optional<std::string> RequiredString(const json& object,
                                          const char* key,
                                          std::size_t limit) {
  auto it = object.find(key);
  if (it == object.end() || !it->is_string()) return std::nullopt;
  auto value = it->get<std::string>();
  if (value.empty() || value.size() > limit || value.find('\0') != std::string::npos ||
      value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> OptionalString(const json& object,
                                          const char* key,
                                          std::size_t limit,
                                          bool& valid) {
  auto it = object.find(key);
  if (it == object.end()) return std::nullopt;
  auto value = RequiredString(object, key, limit);
  if (!value) valid = false;
  return value;
}

class AllowedRoots {
 public:
  static std::variant<AllowedRoots, Failure> FromEnvironment() {
    const char* raw = std::getenv("NANEXUS_INTEGRATION_ALLOWED_ROOTS");
    if (raw == nullptr || *raw == '\0') {
      return Failure{"dependency_unavailable", "Allowed roots are not configured."};
    }
    std::vector<fs::path> roots;
    std::string text(raw);
    std::size_t start = 0;
    while (start <= text.size()) {
      auto end = text.find(':', start);
      if (end == std::string::npos) end = text.size();
      if (end > start) {
        std::error_code ec;
        auto root = fs::canonical(text.substr(start, end - start), ec);
        if (ec || !fs::is_directory(root, ec)) {
          return Failure{"dependency_unavailable", "An allowed root is unavailable."};
        }
        roots.push_back(std::move(root));
        if (roots.size() > kMaxRoots) {
          return Failure{"resource_limit", "Too many allowed roots are configured."};
        }
      }
      if (end == text.size()) break;
      start = end + 1;
    }
    if (roots.empty()) {
      return Failure{"dependency_unavailable", "Allowed roots are not configured."};
    }
    return AllowedRoots(std::move(roots));
  }

  std::variant<AuthorizedPath, Failure> Authorize(const std::string& raw) const {
    if (raw.size() > kMaxPathBytes) {
      return Failure{"resource_limit", "A path exceeds the allowed size."};
    }
    std::error_code ec;
    fs::path candidate(raw);
    if (candidate.is_relative()) candidate = fs::current_path(ec) / candidate;
    if (ec) return Failure{"path_not_allowed", "The path is not allowed."};
    auto resolved = fs::canonical(candidate, ec);
    if (ec) {
      return Failure{"input_not_found", "An input file was not found."};
    }
    if (!fs::is_regular_file(resolved, ec) || ec) {
      return Failure{"input_rejected", "An input is not a regular file."};
    }
    for (std::size_t i = 0; i < roots_.size(); ++i) {
      auto relative = resolved.lexically_relative(roots_[i]);
      if (!relative.empty() && *relative.begin() != "..") {
        std::string reference;
        if (roots_.size() > 1) reference = "root-" + std::to_string(i) + "/";
        reference += relative.generic_string();
        auto sha256 = Sha256(resolved);
        if (!sha256) {
          return Failure{"internal_failure", "An authorized input could not be identified."};
        }
        return AuthorizedPath{resolved.string(), std::move(reference), std::move(*sha256)};
      }
    }
    return Failure{"path_not_allowed", "The path is outside the allowed roots."};
  }

 private:
  explicit AllowedRoots(std::vector<fs::path> roots) : roots_(std::move(roots)) {}
  std::vector<fs::path> roots_;
};

std::variant<videotrust::Codec, Failure> ParseCodec(const json& input) {
  auto codec = RequiredString(input, "codec", 4);
  if (!codec) return Failure{"malformed_request", "The request shape is invalid."};
  if (*codec == "h264") return videotrust::Codec::H264;
  if (*codec == "h265") return videotrust::Codec::H265;
  return Failure{"input_rejected", "Codec must be h264 or h265."};
}

std::variant<AuthorizedPath, Failure> Path(const json& input,
                                           const char* key,
                                           const AllowedRoots& roots) {
  auto value = RequiredString(input, key, kMaxPathBytes);
  if (!value) return Failure{"malformed_request", "The request shape is invalid."};
  return roots.Authorize(*value);
}

std::variant<std::optional<AuthorizedPath>, Failure> OptionalPath(
    const json& input, const char* key, const AllowedRoots& roots) {
  auto it = input.find(key);
  if (it == input.end()) return std::optional<AuthorizedPath>{};
  auto authorized = Path(input, key, roots);
  if (auto* failure = std::get_if<Failure>(&authorized)) return *failure;
  return std::optional<AuthorizedPath>{std::get<AuthorizedPath>(std::move(authorized))};
}

std::optional<videotrust::TransformationKind> Transformation(std::string_view value) {
  using K = videotrust::TransformationKind;
  if (value == "transparent") return K::Transparent;
  if (value == "remux") return K::Remux;
  if (value == "clip") return K::Clip;
  if (value == "segment") return K::Segment;
  if (value == "concatenate") return K::Concatenate;
  if (value == "transcode") return K::Transcode;
  if (value == "metadata-change") return K::MetadataChange;
  if (value == "timestamp-rewrite") return K::TimestampRewrite;
  if (value == "proprietary-or-unknown") return K::ProprietaryOrUnknown;
  return std::nullopt;
}

std::variant<ParsedRequest, Failure> ParseRequest(const json& root,
                                                  const AllowedRoots& roots) {
  static const std::set<std::string> top_keys{
      "document_type", "schema_version", "request_id", "operation", "input"};
  if (!ClosedObject(root, top_keys)) {
    return Failure{"malformed_request", "The request shape is invalid."};
  }
  auto type = RequiredString(root, "document_type", 64);
  auto version = RequiredString(root, "schema_version", 16);
  auto request_id = RequiredString(root, "request_id", kMaxRequestIdBytes);
  auto operation = RequiredString(root, "operation", 32);
  auto input_it = root.find("input");
  if (!type || !version || !request_id || !operation || input_it == root.end() ||
      !input_it->is_object() || *type != kRequestType || !Token(*request_id)) {
    return Failure{"malformed_request", "The request shape is invalid."};
  }
  if (*version != kSchemaVersion) {
    return Failure{"unsupported_contract_version", "The contract version is unsupported."};
  }
  const auto& input = *input_it;
  auto codec_value = ParseCodec(input);
  if (auto* failure = std::get_if<Failure>(&codec_value)) return *failure;
  auto codec = std::get<videotrust::Codec>(codec_value);
  ParsedRequest parsed;
  parsed.request_id = *request_id;

  if (*operation == "verify_file" || *operation == "inspect_file") {
    static const std::set<std::string> keys{"codec", "input_file", "trust_anchor"};
    if (!ClosedObject(input, keys)) {
      return Failure{"malformed_request", "The request shape is invalid."};
    }
    auto media = Path(input, "input_file", roots);
    if (auto* failure = std::get_if<Failure>(&media)) return *failure;
    auto trust = OptionalPath(input, "trust_anchor", roots);
    if (auto* failure = std::get_if<Failure>(&trust)) return *failure;
    auto authorized_media = std::get<AuthorizedPath>(std::move(media));
    auto authorized_trust =
        std::get<std::optional<AuthorizedPath>>(std::move(trust));
    videotrust::VerifyOptions options{codec, authorized_media.canonical, {}};
    parsed.references.push_back(
        {"input_file", authorized_media.reference, authorized_media.sha256});
    if (authorized_trust) {
      options.ca_pem_path = authorized_trust->canonical;
      parsed.references.push_back(
          {"trust_anchor", authorized_trust->reference, authorized_trust->sha256});
    }
    if (*operation == "verify_file") {
      parsed.request = videotrust::IntegrationVerifyRequest{std::move(options)};
    } else {
      parsed.request = videotrust::IntegrationInspectRequest{std::move(options)};
    }
    return parsed;
  }

  if (*operation == "compare_preservation") {
    static const std::set<std::string> keys{
        "codec", "before_file", "after_file", "before_trust_anchor",
        "after_trust_anchor", "transformation", "pipeline_id"};
    if (!ClosedObject(input, keys)) {
      return Failure{"malformed_request", "The request shape is invalid."};
    }
    auto before = Path(input, "before_file", roots);
    if (auto* failure = std::get_if<Failure>(&before)) return *failure;
    auto after = Path(input, "after_file", roots);
    if (auto* failure = std::get_if<Failure>(&after)) return *failure;
    auto before_ca = OptionalPath(input, "before_trust_anchor", roots);
    if (auto* failure = std::get_if<Failure>(&before_ca)) return *failure;
    auto after_ca = OptionalPath(input, "after_trust_anchor", roots);
    if (auto* failure = std::get_if<Failure>(&after_ca)) return *failure;
    bool valid = true;
    auto transformation = OptionalString(input, "transformation", 32, valid);
    auto pipeline = OptionalString(input, "pipeline_id", kMaxPipelineIdBytes, valid);
    if (!valid) return Failure{"malformed_request", "The request shape is invalid."};
    videotrust::TransformationContext context;
    if (transformation) {
      auto kind = Transformation(*transformation);
      if (!kind) return Failure{"input_rejected", "Transformation is unsupported."};
      context.kind = *kind;
    }
    if (pipeline) context.pipeline_id = *pipeline;
    auto authorized_before = std::get<AuthorizedPath>(std::move(before));
    auto authorized_after = std::get<AuthorizedPath>(std::move(after));
    auto authorized_before_ca =
        std::get<std::optional<AuthorizedPath>>(std::move(before_ca));
    auto authorized_after_ca =
        std::get<std::optional<AuthorizedPath>>(std::move(after_ca));
    videotrust::PreservationCompareOptions options;
    options.codec = codec;
    options.before_path = authorized_before.canonical;
    options.after_path = authorized_after.canonical;
    options.transformation = std::move(context);
    parsed.references.push_back(
        {"before_file", authorized_before.reference, authorized_before.sha256});
    parsed.references.push_back(
        {"after_file", authorized_after.reference, authorized_after.sha256});
    if (authorized_before_ca) {
      options.before_ca_pem_path = authorized_before_ca->canonical;
      parsed.references.push_back({"before_trust_anchor", authorized_before_ca->reference,
                                   authorized_before_ca->sha256});
    }
    if (authorized_after_ca) {
      options.after_ca_pem_path = authorized_after_ca->canonical;
      parsed.references.push_back({"after_trust_anchor", authorized_after_ca->reference,
                                   authorized_after_ca->sha256});
    }
    parsed.request = videotrust::IntegrationCompareRequest{std::move(options)};
    return parsed;
  }
  return Failure{"unsupported_operation", "The operation is unsupported."};
}

Failure MapDomainError(const videotrust::Error& error) {
  using E = videotrust::ErrorCode;
  switch (error.code) {
    case E::InvalidArgument:
    case E::ParseError:
      return {"input_rejected", "The core rejected an input."};
    case E::NotSupported:
      return {"dependency_unavailable", "A required capability is unavailable."};
    case E::IoFailure:
      return {"internal_failure", "The core could not read an authorized input."};
    case E::UpstreamFailure:
    case E::InternalError:
      return {"internal_failure", "The core operation failed."};
    case E::Ok:
      break;
  }
  return {"internal_failure", "The core operation failed."};
}

json BaseResponse(const std::string& request_id, const std::string& operation) {
  return {{"document_type", kResponseType},
          {"schema_version", kSchemaVersion},
          {"request_id", request_id},
          {"operation", operation}};
}

std::string SafeMetadata(const json& root,
                         const char* key,
                         std::size_t limit,
                         bool require_token = false) {
  if (!root.is_object()) return "unavailable";
  auto it = root.find(key);
  if (it == root.end() || !it->is_string()) return "unavailable";
  const auto value = it->get<std::string>();
  if (value.empty() || value.size() > limit) return "unavailable";
  if (require_token && !Token(value)) return "unavailable";
  return value;
}

json Failed(const std::string& request_id,
            const std::string& operation,
            const Failure& failure) {
  auto response = BaseResponse(request_id, operation);
  response["execution"] = {{"status", "failed"},
                           {"error", {{"code", failure.code},
                                      {"message", failure.message}}}};
  response["result"] = nullptr;
  response["evidence"] = nullptr;
  response["limitations"] = json::array();
  return response;
}

json Completed(const ParsedRequest& request,
               const videotrust::IntegrationResult& result,
               json document) {
  auto response = BaseResponse(request.request_id, videotrust::ToString(result.operation));
  response["execution"] = {{"status", "completed"}, {"error", nullptr}};
  response["result"] = {{"document_type", videotrust::IntegrationDocumentType(result.operation)},
                        {"schema_version", "0.1"},
                        {"document", std::move(document)}};
  response["evidence"] = {{"core_name", "video-trust"},
                           {"core_version", VIDEO_TRUST_VERSION},
                           {"inputs", json::array()}};
  for (const auto& input : request.references) {
    response["evidence"]["inputs"].push_back(
        {{"role", input.role}, {"reference", input.reference}, {"sha256", input.sha256}});
  }
  response["limitations"] = json::array(
      {"source_authenticity_not_established", "depicted_event_authenticity_not_established"});
  return response;
}

int Emit(const json& response) {
  auto text = response.dump();
  if (text.size() > kMaxResponseBytes) {
    auto bounded = Failed(response.value("request_id", "unavailable"),
                          response.value("operation", "unavailable"),
                          {"resource_limit", "The response exceeds the allowed size."})
                       .dump();
    std::cout << bounded << '\n';
    return 1;
  }
  std::cout << text << '\n';
  return response["execution"]["status"] == "completed" ? 0 : 1;
}

}  // namespace

int main() {
  std::string input;
  input.reserve(kMaxRequestBytes + 1);
  char buffer[4096];
  while (std::cin.read(buffer, sizeof(buffer)) || std::cin.gcount() > 0) {
    input.append(buffer, static_cast<std::size_t>(std::cin.gcount()));
    if (input.size() > kMaxRequestBytes) {
      return Emit(Failed("unavailable", "unavailable",
                         {"resource_limit", "The request exceeds the allowed size."}));
    }
  }
  auto root = json::parse(input, nullptr, false, true);
  if (root.is_discarded()) {
    return Emit(Failed("unavailable", "unavailable",
                       {"malformed_request", "The request is not valid JSON."}));
  }
  auto roots_value = AllowedRoots::FromEnvironment();
  if (auto* failure = std::get_if<Failure>(&roots_value)) {
    return Emit(Failed(SafeMetadata(root, "request_id", kMaxRequestIdBytes, true),
                       SafeMetadata(root, "operation", 32), *failure));
  }
  auto parsed_value = ParseRequest(root, std::get<AllowedRoots>(std::move(roots_value)));
  if (auto* failure = std::get_if<Failure>(&parsed_value)) {
    return Emit(Failed(SafeMetadata(root, "request_id", kMaxRequestIdBytes, true),
                       SafeMetadata(root, "operation", 32), *failure));
  }
  auto parsed = std::get<ParsedRequest>(std::move(parsed_value));
  auto result = videotrust::ExecuteIntegration(parsed.request);
  if (!result.ok()) {
    return Emit(Failed(parsed.request_id,
                       std::visit([](const auto& value) {
                         using T = std::decay_t<decltype(value)>;
                         if constexpr (std::is_same_v<T, videotrust::IntegrationVerifyRequest>)
                           return std::string("verify_file");
                         if constexpr (std::is_same_v<T, videotrust::IntegrationInspectRequest>)
                           return std::string("inspect_file");
                         return std::string("compare_preservation");
                       }, parsed.request),
                       MapDomainError(result.error())));
  }
  auto domain_text = videotrust::RenderIntegrationDomainJson(result.value());
  auto document = json::parse(domain_text, nullptr, false, true);
  if (document.is_discarded()) {
    return Emit(Failed(parsed.request_id, videotrust::ToString(result.value().operation),
                       {"contract_mismatch", "The core result did not match its contract."}));
  }
  return Emit(Completed(parsed, result.value(), std::move(document)));
}
