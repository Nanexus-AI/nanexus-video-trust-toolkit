#include "gateway_sign.hpp"
#include "videotrust/epoch_artifacts.hpp"
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string_view>

#ifndef VIDEO_TRUST_HAS_GSTREAMER
#define VIDEO_TRUST_HAS_GSTREAMER 0
#endif
namespace {
#if VIDEO_TRUST_HAS_GSTREAMER
volatile std::sig_atomic_t cancelled = 0;
void Cancel(int) { cancelled = 1; }
#endif
int Error(int code, const char* message) {
  std::cerr << "error: " << message << '\n'; return code;
}
void Usage() {
  std::cout << "video-trust gateway-sign --codec h264|h265 --duration 1..300\n"
    "    --key KEY.pem --cert CHAIN.pem --output-root NEW_ABSOLUTE_DIRECTORY <rtsp-url>\n"
    "Foreground reference workflow. Credentials: NANEXUS_RTSP_USERNAME/PASSWORD.\n"
    "0 finalized observed prefix; 2 configuration/path; 3 runtime/abort/loss.\n"
    "Gateway signing does not establish camera identity or event authenticity.\n";
}
int Run(int argc, char** argv) {
  videotrust::LiveIngestOptions input;
  videotrust::LiveSigningOptions signing;
  std::string root;
  std::set<std::string_view> seen;
  for (int i = 2; i < argc; ++i) {
    const std::string_view option(argv[i]);
    if (option == "--help" || option == "-h") { Usage(); return 0; }
    if (option == "--codec" || option == "--duration" || option == "--key" ||
        option == "--cert" || option == "--output-root") {
      if (!seen.insert(option).second || i + 1 >= argc) return Error(2, "missing or duplicate gateway option");
      const std::string_view value(argv[++i]);
      if (value.empty() || value.size() > 4096) return Error(2, "gateway option exceeds policy");
      if (option == "--codec") {
        if (value != "h264" && value != "h265") return Error(2, "codec must be h264 or h265");
        input.codec = signing.signer.codec = value == "h264" ? videotrust::Codec::H264 : videotrust::Codec::H265;
      } else if (option == "--duration") {
        unsigned duration = 0;
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), duration);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || duration < 1 || duration > 300)
          return Error(2, "duration must be an integer between 1 and 300");
        input.duration_seconds = duration;
      } else if (option == "--key") signing.signer.key_pem_path = value;
      else if (option == "--cert") signing.signer.cert_pem_path = value;
      else root = value;
    } else if (option.starts_with('-') || !input.endpoint.empty()) return Error(2, "unknown gateway option or extra source");
    else input.endpoint = option;
  }
  if (seen.size() != 5 || input.endpoint.empty()) return Error(2, "codec, duration, key, cert, output-root and one RTSP source are required");
  if (!input.endpoint.starts_with("rtsp://") || input.endpoint.size() > 2048 ||
      input.endpoint.find_first_of("\r\n\t") != std::string::npos ||
      input.endpoint.substr(7, input.endpoint.find('/', 7) - 7).empty() ||
      input.endpoint.substr(7, input.endpoint.find('/', 7) - 7).find('@') != std::string::npos)
    return Error(2, "invalid RTSP source; embedded credentials are forbidden");
  const char* username = std::getenv("NANEXUS_RTSP_USERNAME");
  const char* password = std::getenv("NANEXUS_RTSP_PASSWORD");
  if ((username == nullptr) != (password == nullptr)) return Error(2, "RTSP credential variables must be set together");
  if (username) {
    // Bound before copying process environment into runner options.
    const std::string_view user(username), secret(password);
    if (user.empty() || secret.empty() || user.size() > 256 || secret.size() > 256)
      return Error(2, "RTSP credentials exceed policy");
    input.username = user; input.password = secret;
  }
#if !VIDEO_TRUST_HAS_GSTREAMER
  return Error(3, "live RTSP support is unavailable in this build");
#else
  if (!videotrust::LiveIngestDependenciesAvailable()) return Error(3, "live media components unavailable");
  // Reuse official core identity validation without output side effects.
  // The real epoch still creates its own fresh signer, never this preflight.
  {
    auto identity = videotrust::StreamSigner::Create(signing.signer,
        [](std::span<const uint8_t>) { return videotrust::Error{videotrust::ErrorCode::Ok, {}}; });
    if (!identity.ok()) return Error(2, "signing identity or configuration rejected");
    identity.value().Abort();
  }
  auto artifacts = videotrust::EpochArtifacts::Create(root, input.codec);
  if (!artifacts.ok()) return Error(artifacts.error().code == videotrust::ErrorCode::InvalidArgument ? 2 : 3,
      "output root unavailable, unsafe or already exists");
  cancelled = 0;
  const auto old_int = std::signal(SIGINT, Cancel);
  const auto old_term = std::signal(SIGTERM, Cancel);
  const auto result = videotrust::RunGStreamerLiveSigning(input, signing,
      artifacts.value().Factory(), [] { return cancelled != 0; });
  std::signal(SIGINT, old_int); std::signal(SIGTERM, old_term);
  if (artifacts.value().Finish(result).code != videotrust::ErrorCode::Ok)
    return Error(3, "artifact persistence failed; retained evidence may be incomplete");
  if (result.end != videotrust::GatewayEnd::BoundedStop && result.end != videotrust::GatewayEnd::CleanEos)
    return Error(3, "gateway stopped without clean finalization; inspect retained epoch evidence");
  if (result.epochs.empty()) return Error(3, "no random-access signing epoch observed");
  std::cout << "Gateway observed prefix finalized; separate epoch artifacts persisted.\n"
      "Verify each signed media file independently. Source identity: not established.\n";
  return 0;
#endif
}
}
int RunGatewaySignCommand(int argc, char** argv) {
  try { return Run(argc, argv); }
  catch (...) { return Error(3, "gateway runtime failed; retained evidence may be incomplete"); }
}
