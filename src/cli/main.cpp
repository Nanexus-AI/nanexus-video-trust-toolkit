#include "videotrust/compare.hpp"
#include "videotrust/error.hpp"
#include "videotrust/live_contract.hpp"
#include "videotrust/live_ingest.hpp"
#include "videotrust/render.hpp"
#include "videotrust/result.hpp"
#include "videotrust/sign.hpp"
#include "videotrust/tamper.hpp"
#include "videotrust/verify.hpp"

#include <cstdlib>
#include <csignal>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

#ifndef VIDEO_TRUST_VERSION
#define VIDEO_TRUST_VERSION "0.1.0"
#endif

#ifndef VIDEO_TRUST_HAS_GSTREAMER
#define VIDEO_TRUST_HAS_GSTREAMER 0
#endif

#if VIDEO_TRUST_HAS_GSTREAMER
volatile std::sig_atomic_t g_live_cancel_requested = 0;
void RequestLiveCancel(int) { g_live_cancel_requested = 1; }
#endif

void Usage(std::ostream& out) {
  out << "video-trust " << VIDEO_TRUST_VERSION << "\n"
      << "Usage:\n"
      << "  video-trust verify --codec h264|h265 [--ca PATH] [--json] <input.es>\n"
      << "  video-trust inspect --codec h264|h265 [--ca PATH] [--json] <input.es>\n"
      << "  video-trust verify-live --codec h264|h265 --duration SECONDS\n"
      << "                    [--ca PATH] [--jsonl] [--summary-only] <rtsp-url>\n"
      << "  video-trust compare-preservation --codec h264|h265\n"
      << "                   [--before-ca PATH] [--after-ca PATH] [--json]\n"
      << "                   [--transformation CLASS] [--pipeline-id TEXT]\n"
      << "                   <before.es> <after.es>\n"
      << "  video-trust sign --codec h264|h265 --key KEY.pem --cert CHAIN.pem\n"
      << "                   -o OUTPUT.es [--force] [--quiet] <input.es>\n"
      << "  video-trust tamper --codec h264|h265 --operation OP -o OUTPUT.es\n"
      << "                    [--force] [--quiet] [--count N] <input.es>\n"
      << "  video-trust --version\n"
      << "\n"
      << "Operations: corrupt-vcl | strip-signing-sei | truncate\n"
      << "Annex-B H.264/H.265 Media Signing reference-lab tooling.\n"
      << "Tamper creates controlled test material; it is not a video editor.\n"
      << "Signing/tampering does not establish camera/source authenticity.\n"
      << "\n"
      << "verify exit codes: 0 positive, 1 integrity fail, 2 usage/input, 3 runtime,\n"
      << "                  4 unsigned/incomplete/not verifiable\n"
      << "compare exit:     0 preserved, 1 partial/not preserved, 2 usage/input,\n"
      << "                  3 runtime, 4 indeterminate/not applicable\n"
      << "sign/tamper exit:  0 success, 2 usage/input/path, 3 runtime\n";
}

struct CommonCodec {
  videotrust::Codec codec{videotrust::Codec::H264};
  bool set{false};
};

bool ParseCodecValue(std::string_view v, videotrust::Codec& out, std::string& err) {
  if (v == "h264") {
    out = videotrust::Codec::H264;
    return true;
  }
  if (v == "h265") {
    out = videotrust::Codec::H265;
    return true;
  }
  err = "unsupported codec (use h264 or h265)";
  return false;
}

bool ParseOperation(std::string_view v, videotrust::TamperOperation& out, std::string& err) {
  if (v == "corrupt-vcl") {
    out = videotrust::TamperOperation::CorruptVcl;
    return true;
  }
  if (v == "strip-signing-sei") {
    out = videotrust::TamperOperation::StripSigningSei;
    return true;
  }
  if (v == "truncate") {
    out = videotrust::TamperOperation::Truncate;
    return true;
  }
  err = "unsupported operation (corrupt-vcl|strip-signing-sei|truncate)";
  return false;
}

bool ParseTransformation(std::string_view value,
                         videotrust::TransformationKind& out,
                         std::string& err) {
  using videotrust::TransformationKind;
  if (value == "transparent") out = TransformationKind::Transparent;
  else if (value == "remux") out = TransformationKind::Remux;
  else if (value == "clip") out = TransformationKind::Clip;
  else if (value == "segment") out = TransformationKind::Segment;
  else if (value == "concatenate") out = TransformationKind::Concatenate;
  else if (value == "transcode") out = TransformationKind::Transcode;
  else if (value == "metadata-change") out = TransformationKind::MetadataChange;
  else if (value == "timestamp-rewrite") out = TransformationKind::TimestampRewrite;
  else if (value == "proprietary-or-unknown") {
    out = TransformationKind::ProprietaryOrUnknown;
  } else {
    err = "unsupported transformation (transparent|remux|clip|segment|concatenate|"
          "transcode|metadata-change|timestamp-rewrite|proprietary-or-unknown)";
    return false;
  }
  return true;
}

struct VerifyArgs {
  bool json{false};
  CommonCodec codec;
  std::string ca;
  std::string input;
};

struct LiveArgs {
  CommonCodec codec;
  unsigned duration_seconds{0};
  bool duration_set{false};
  bool jsonl{false};
  bool summary_only{false};
  std::string ca;
  std::string endpoint;
  std::string username;
  std::string password;
};

struct SignArgs {
  CommonCodec codec;
  std::string key;
  std::string cert;
  std::string output;
  std::string input;
  bool force{false};
  bool quiet{false};
};

struct TamperArgs {
  CommonCodec codec;
  videotrust::TamperOperation operation{videotrust::TamperOperation::CorruptVcl};
  bool operation_set{false};
  std::string output;
  std::string input;
  bool force{false};
  bool quiet{false};
  std::size_t truncate_count{1};
  bool truncate_count_set{false};
};

struct CompareArgs {
  bool json{false};
  CommonCodec codec;
  std::string before_ca;
  std::string after_ca;
  std::string before;
  std::string after;
  videotrust::TransformationContext transformation;
};

bool ParseVerifyArgs(int argc, char** argv, VerifyArgs& out, std::string& err) {
  std::vector<std::string> positionals;
  for (int i = 2; i < argc; ++i) {
    const std::string_view a(argv[i]);
    if (a == "--json") {
      out.json = true;
    } else if (a == "--codec") {
      if (i + 1 >= argc) {
        err = "--codec requires a value";
        return false;
      }
      if (!ParseCodecValue(argv[++i], out.codec.codec, err)) {
        return false;
      }
      out.codec.set = true;
    } else if (a == "--ca") {
      if (i + 1 >= argc) {
        err = "--ca requires a path";
        return false;
      }
      out.ca = argv[++i];
    } else if (a == "-h" || a == "--help") {
      Usage(std::cout);
      std::exit(0);
    } else if (a.starts_with('-')) {
      err = "unknown option: " + std::string(a);
      return false;
    } else {
      positionals.emplace_back(a);
    }
  }
  if (!out.codec.set) {
    err = "required --codec h264|h265";
    return false;
  }
  if (positionals.size() != 1) {
    err = "exactly one input path is required";
    return false;
  }
  out.input = positionals[0];
  return true;
}

bool ParseInspectArgs(int argc, char** argv, VerifyArgs& out, std::string& err) {
  return ParseVerifyArgs(argc, argv, out, err);
}

bool ParseLiveArgs(int argc, char** argv, LiveArgs& out, std::string& err) {
  std::vector<std::string> positionals;
  for (int i = 2; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--jsonl") out.jsonl = true;
    else if (argument == "--summary-only") out.summary_only = true;
    else if (argument == "--codec") {
      if (i + 1 >= argc) { err = "--codec requires a value"; return false; }
      if (!ParseCodecValue(argv[++i], out.codec.codec, err)) return false;
      out.codec.set = true;
    } else if (argument == "--duration") {
      if (i + 1 >= argc) { err = "--duration requires seconds"; return false; }
      try {
        const unsigned long value = std::stoul(argv[++i]);
        if (value < videotrust::kLiveMinDurationSeconds ||
            value > videotrust::kLiveMaxDurationSeconds) {
          err = "--duration must be between 1 and 300 seconds";
          return false;
        }
        out.duration_seconds = static_cast<unsigned>(value);
        out.duration_set = true;
      } catch (...) {
        err = "--duration requires an integer between 1 and 300";
        return false;
      }
    } else if (argument == "--ca") {
      if (i + 1 >= argc) { err = "--ca requires a path"; return false; }
      out.ca = argv[++i];
    } else if (argument == "-h" || argument == "--help") {
      Usage(std::cout);
      std::exit(0);
    } else if (argument.starts_with('-')) {
      err = "unknown option: " + std::string(argument);
      return false;
    } else {
      positionals.emplace_back(argument);
    }
  }
  if (!out.codec.set) { err = "required --codec h264|h265"; return false; }
  if (!out.duration_set) { err = "required --duration 1..300"; return false; }
  if (out.summary_only && !out.jsonl) {
    err = "--summary-only requires --jsonl";
    return false;
  }
  if (positionals.size() != 1) {
    err = "exactly one RTSP endpoint is required";
    return false;
  }
  out.endpoint = positionals.front();
  if (!out.endpoint.starts_with("rtsp://") || out.endpoint.size() > 2048 ||
      out.endpoint.find_first_of("\r\n\t") != std::string::npos) {
    err = "endpoint must be one bounded rtsp:// URL";
    return false;
  }
  const std::size_t authority_end = out.endpoint.find('/', 7);
  if (out.endpoint.substr(7, authority_end - 7).find('@') != std::string::npos) {
    err = "credentials in the RTSP URL are forbidden";
    return false;
  }
  const char* username = std::getenv("NANEXUS_RTSP_USERNAME");
  const char* password = std::getenv("NANEXUS_RTSP_PASSWORD");
  if ((username == nullptr) != (password == nullptr)) {
    err = "RTSP username and password environment variables must be set together";
    return false;
  }
  if (username) {
    out.username = username;
    out.password = password;
    if (out.username.empty() || out.username.size() > 256 ||
        out.password.empty() || out.password.size() > 256) {
      err = "RTSP credentials exceed the fixed policy";
      return false;
    }
  }
  return true;
}

int RunLive(const LiveArgs& args) {
#if !VIDEO_TRUST_HAS_GSTREAMER
  (void)args;
  std::cerr << "error: live RTSP support is unavailable in this build\n";
  return 3;
#else
  auto validator = videotrust::IncrementalLiveValidator::Create(args.codec.codec);
  if (!validator.ok()) { std::cerr << "error: live validator initialization failed\n"; return 3; }
  if (!args.ca.empty()) {
    const auto error = validator.value().SetTrustedCertificateFile(args.ca);
    if (error.code != videotrust::ErrorCode::Ok) {
      std::cerr << "error: trust configuration could not be loaded\n";
      return 2;
    }
  }
  videotrust::LiveContractRenderer renderer;
  std::size_t event_count = 0;
  auto emit_json = [&](const std::string& document, bool summary) {
    if (document.size() > videotrust::kLiveMaxDocumentBytes) return false;
    if (!args.summary_only || summary) std::cout << document;
    ++event_count;
    return true;
  };
  auto started = renderer.Start();
  if (!started.ok() || (args.jsonl && !emit_json(started.value(), false))) return 3;
  if (!args.jsonl) std::cout << "Nanexus live session started\n";
  auto epoch = renderer.RenderObservation(validator.value().epoch_started());
  if (!epoch.ok() || (args.jsonl && !emit_json(epoch.value(), false))) return 3;
  if (!args.jsonl) {
    std::cout << videotrust::RenderLiveObservationText(validator.value().epoch_started());
  }
  event_count = 2;
  g_live_cancel_requested = 0;
  std::signal(SIGINT, RequestLiveCancel);
  std::signal(SIGTERM, RequestLiveCancel);
  videotrust::LiveIngestOptions ingest{args.codec.codec, args.endpoint,
                                       args.username, args.password,
                                       args.duration_seconds};
  auto ingest_result = videotrust::RunGStreamerLiveIngest(
      ingest,
      [&](const videotrust::NalUnit& nal) {
        if (event_count >= videotrust::kLiveMaxEvents - 2) {
          return videotrust::MakeError(videotrust::ErrorCode::InvalidArgument,
                                       "live event limit reached");
        }
        auto observation = validator.value().AddNal(nal);
        if (!observation.ok()) return observation.error();
        if (observation.value()) {
          auto rendered = renderer.RenderObservation(*observation.value());
          if (!rendered.ok()) return rendered.error();
          if (args.jsonl) {
            if (!emit_json(rendered.value(), false)) {
              return videotrust::MakeError(videotrust::ErrorCode::InvalidArgument,
                                           "live document limit reached");
            }
          } else {
            std::cout << videotrust::RenderLiveObservationText(*observation.value());
            ++event_count;
          }
        }
        return videotrust::Error{videotrust::ErrorCode::Ok, {}};
      },
      [] { return g_live_cancel_requested != 0; });
  auto final_events = validator.value().EndEpoch(ingest_result.stop_reason);
  if (!final_events.ok()) { std::cerr << "error: live session could not be finalized\n"; return 3; }
  for (const auto& observation : final_events.value()) {
    auto rendered = renderer.RenderObservation(observation);
    if (!rendered.ok()) return 3;
    if (args.jsonl) {
      if (!emit_json(rendered.value(), false)) return 3;
    } else {
      std::cout << videotrust::RenderLiveObservationText(observation);
      ++event_count;
    }
  }
  auto summary = renderer.Finalize(ingest_result.stop_reason);
  if (!summary.ok()) return 3;
  if (args.jsonl) {
    if (!emit_json(summary.value(), true)) return 3;
  } else {
    auto text = renderer.SummaryOnlyText();
    if (!text.ok()) return 3;
    std::cout << text.value();
  }
  if (ingest_result.error.code != videotrust::ErrorCode::Ok) {
    std::cerr << "error: "
              << ingest_result.error.message.substr(0, videotrust::kLiveMaxDiagnosticBytes)
              << "\n";
  }
  return static_cast<int>(videotrust::ExitCodeForLive(
      validator.value().semantic().closed_summary(),
      validator.value().semantic().tail_state(), ingest_result.stop_reason));
#endif
}

bool ParseCompareArgs(int argc, char** argv, CompareArgs& out, std::string& err) {
  std::vector<std::string> positionals;
  for (int i = 2; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--json") {
      out.json = true;
    } else if (argument == "--codec") {
      if (i + 1 >= argc) {
        err = "--codec requires a value";
        return false;
      }
      if (!ParseCodecValue(argv[++i], out.codec.codec, err)) return false;
      out.codec.set = true;
    } else if (argument == "--before-ca") {
      if (i + 1 >= argc) {
        err = "--before-ca requires a path";
        return false;
      }
      out.before_ca = argv[++i];
    } else if (argument == "--after-ca") {
      if (i + 1 >= argc) {
        err = "--after-ca requires a path";
        return false;
      }
      out.after_ca = argv[++i];
    } else if (argument == "--transformation") {
      if (i + 1 >= argc) {
        err = "--transformation requires a value";
        return false;
      }
      if (!ParseTransformation(argv[++i], out.transformation.kind, err)) return false;
    } else if (argument == "--pipeline-id") {
      if (i + 1 >= argc) {
        err = "--pipeline-id requires a value";
        return false;
      }
      out.transformation.pipeline_id = argv[++i];
    } else if (argument == "-h" || argument == "--help") {
      Usage(std::cout);
      std::exit(0);
    } else if (argument.starts_with('-')) {
      err = "unknown option: " + std::string(argument);
      return false;
    } else {
      positionals.emplace_back(argument);
    }
  }
  if (!out.codec.set) {
    err = "required --codec h264|h265";
    return false;
  }
  if (positionals.size() != 2) {
    err = "exactly two input paths are required: before and after";
    return false;
  }
  out.before = positionals[0];
  out.after = positionals[1];
  return true;
}

bool ParseSignArgs(int argc, char** argv, SignArgs& out, std::string& err) {
  std::vector<std::string> positionals;
  for (int i = 2; i < argc; ++i) {
    const std::string_view a(argv[i]);
    if (a == "--force") {
      out.force = true;
    } else if (a == "--quiet" || a == "-q") {
      out.quiet = true;
    } else if (a == "--codec") {
      if (i + 1 >= argc) {
        err = "--codec requires a value";
        return false;
      }
      if (!ParseCodecValue(argv[++i], out.codec.codec, err)) {
        return false;
      }
      out.codec.set = true;
    } else if (a == "--key") {
      if (i + 1 >= argc) {
        err = "--key requires a path";
        return false;
      }
      out.key = argv[++i];
    } else if (a == "--cert") {
      if (i + 1 >= argc) {
        err = "--cert requires a path";
        return false;
      }
      out.cert = argv[++i];
    } else if (a == "-o" || a == "--output") {
      if (i + 1 >= argc) {
        err = "-o/--output requires a path";
        return false;
      }
      out.output = argv[++i];
    } else if (a == "-h" || a == "--help") {
      Usage(std::cout);
      std::exit(0);
    } else if (a.starts_with('-')) {
      err = "unknown option: " + std::string(a);
      return false;
    } else {
      positionals.emplace_back(a);
    }
  }
  if (!out.codec.set) {
    err = "required --codec h264|h265";
    return false;
  }
  if (out.key.empty()) {
    err = "required --key <path>";
    return false;
  }
  if (out.cert.empty()) {
    err = "required --cert <path>";
    return false;
  }
  if (out.output.empty()) {
    err = "required -o/--output <path>";
    return false;
  }
  if (positionals.size() != 1) {
    err = "exactly one input path is required";
    return false;
  }
  out.input = positionals[0];
  return true;
}

bool ParseTamperArgs(int argc, char** argv, TamperArgs& out, std::string& err) {
  std::vector<std::string> positionals;
  for (int i = 2; i < argc; ++i) {
    const std::string_view a(argv[i]);
    if (a == "--force") {
      out.force = true;
    } else if (a == "--quiet" || a == "-q") {
      out.quiet = true;
    } else if (a == "--codec") {
      if (i + 1 >= argc) {
        err = "--codec requires a value";
        return false;
      }
      if (!ParseCodecValue(argv[++i], out.codec.codec, err)) {
        return false;
      }
      out.codec.set = true;
    } else if (a == "--operation") {
      if (i + 1 >= argc) {
        err = "--operation requires a value";
        return false;
      }
      if (!ParseOperation(argv[++i], out.operation, err)) {
        return false;
      }
      out.operation_set = true;
    } else if (a == "--count") {
      if (i + 1 >= argc) {
        err = "--count requires a positive integer";
        return false;
      }
      const std::string v(argv[++i]);
      try {
        const int n = std::stoi(v);
        if (n < 1) {
          err = "--count must be >= 1";
          return false;
        }
        out.truncate_count = static_cast<std::size_t>(n);
        out.truncate_count_set = true;
      } catch (...) {
        err = "--count requires a positive integer";
        return false;
      }
    } else if (a == "-o" || a == "--output") {
      if (i + 1 >= argc) {
        err = "-o/--output requires a path";
        return false;
      }
      out.output = argv[++i];
    } else if (a == "-h" || a == "--help") {
      Usage(std::cout);
      std::exit(0);
    } else if (a.starts_with('-')) {
      err = "unknown option: " + std::string(a);
      return false;
    } else {
      positionals.emplace_back(a);
    }
  }
  if (!out.codec.set) {
    err = "required --codec h264|h265";
    return false;
  }
  if (!out.operation_set) {
    err = "required --operation corrupt-vcl|strip-signing-sei|truncate";
    return false;
  }
  if (out.truncate_count_set && out.operation != videotrust::TamperOperation::Truncate) {
    err = "--count is only valid with --operation truncate";
    return false;
  }
  if (out.output.empty()) {
    err = "required -o/--output <path>";
    return false;
  }
  if (positionals.size() != 1) {
    err = "exactly one input path is required";
    return false;
  }
  out.input = positionals[0];
  return true;
}

int RunVerify(const VerifyArgs& args) {
  videotrust::VerifyOptions opt;
  opt.codec = args.codec.codec;
  opt.input_path = args.input;
  opt.ca_pem_path = args.ca;

  auto result = videotrust::VerifyAnnexBFile(opt);
  if (!result.ok()) {
    const auto& e = result.error();
    std::cerr << "error: " << e.message << "\n";
    using videotrust::ErrorCode;
    if (e.code == ErrorCode::ParseError || e.code == ErrorCode::InvalidArgument ||
        e.code == ErrorCode::IoFailure) {
      if (e.code == ErrorCode::IoFailure &&
          e.message.find("cannot open") != std::string::npos) {
        return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
      }
      if (e.code == ErrorCode::ParseError || e.code == ErrorCode::InvalidArgument) {
        return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
      }
      return static_cast<int>(videotrust::ExitCode::RuntimeFailure);
    }
    return static_cast<int>(videotrust::ExitCode::RuntimeFailure);
  }

  if (args.json) {
    std::cout << videotrust::RenderJson(result.value());
  } else {
    std::cout << videotrust::RenderText(result.value());
  }
  return static_cast<int>(videotrust::ExitCodeForVerification(result.value()));
}

int RunInspect(const VerifyArgs& args) {
  videotrust::VerifyOptions opt;
  opt.codec = args.codec.codec;
  opt.input_path = args.input;
  opt.ca_pem_path = args.ca;

  auto result = videotrust::InspectAnnexBFile(opt);
  if (!result.ok()) {
    const auto& e = result.error();
    std::cerr << "error: " << e.message << "\n";
    using videotrust::ErrorCode;
    if (e.code == ErrorCode::ParseError || e.code == ErrorCode::InvalidArgument ||
        (e.code == ErrorCode::IoFailure &&
         e.message.find("cannot open") != std::string::npos)) {
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return static_cast<int>(videotrust::ExitCode::RuntimeFailure);
  }

  if (args.json) {
    std::cout << videotrust::RenderInspectionJson(result.value());
  } else {
    std::cout << videotrust::RenderInspectionText(result.value());
  }
  return static_cast<int>(
      videotrust::ExitCodeForVerification(result.value().verification));
}

int RunCompare(const CompareArgs& args) {
  videotrust::PreservationCompareOptions options;
  options.codec = args.codec.codec;
  options.before_path = args.before;
  options.after_path = args.after;
  options.before_ca_pem_path = args.before_ca;
  options.after_ca_pem_path = args.after_ca;
  options.transformation = args.transformation;

  auto result = videotrust::ComparePreservation(options);
  if (!result.ok()) {
    const auto& error = result.error();
    std::cerr << "error: " << error.message << "\n";
    using videotrust::ErrorCode;
    if (error.code == ErrorCode::ParseError ||
        error.code == ErrorCode::InvalidArgument ||
        (error.code == ErrorCode::IoFailure &&
         error.message.find("cannot open") != std::string::npos)) {
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return static_cast<int>(videotrust::ExitCode::RuntimeFailure);
  }

  if (args.json) {
    std::cout << videotrust::RenderPreservationJson(result.value());
  } else {
    std::cout << videotrust::RenderPreservationText(result.value());
  }
  return static_cast<int>(videotrust::ExitCodeForPreservation(result.value()));
}

int RunSign(const SignArgs& args) {
  videotrust::SignOptions opt;
  opt.codec = args.codec.codec;
  opt.input_path = args.input;
  opt.output_path = args.output;
  opt.key_pem_path = args.key;
  opt.cert_pem_path = args.cert;
  opt.force = args.force;

  const videotrust::Error err = videotrust::SignAnnexBFile(opt);
  if (err.code != videotrust::ErrorCode::Ok) {
    std::cerr << "error: " << err.message << "\n";
    return static_cast<int>(videotrust::ExitCodeForSign(err));
  }

  if (!args.quiet) {
    std::cout << "Signed media written successfully\n"
              << "Codec: "
              << (args.codec.codec == videotrust::Codec::H265 ? "H.265" : "H.264")
              << "\n"
              << "Output: " << args.output << "\n";
  }
  return static_cast<int>(videotrust::ExitCode::Success);
}

int RunTamper(const TamperArgs& args) {
  videotrust::TamperOptions opt;
  opt.codec = args.codec.codec;
  opt.operation = args.operation;
  opt.input_path = args.input;
  opt.output_path = args.output;
  opt.force = args.force;
  opt.truncate_nal_count = args.truncate_count;

  const videotrust::Error err = videotrust::TamperAnnexBFile(opt);
  if (err.code != videotrust::ErrorCode::Ok) {
    std::cerr << "error: " << err.message << "\n";
    return static_cast<int>(videotrust::ExitCodeForTamper(err));
  }

  if (!args.quiet) {
    std::cout << "Tampered media written successfully\n"
              << "Operation: " << videotrust::ToString(args.operation) << "\n"
              << "Codec: "
              << (args.codec.codec == videotrust::Codec::H265 ? "H.265" : "H.264")
              << "\n"
              << "Output: " << args.output << "\n";
  }
  return static_cast<int>(videotrust::ExitCode::Success);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "error: missing subcommand\n";
    Usage(std::cerr);
    return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
  }

  const std::string_view cmd(argv[1]);
  if (cmd == "-h" || cmd == "--help") {
    Usage(std::cout);
    return 0;
  }
  if (cmd == "-V" || cmd == "--version") {
    std::cout << "video-trust " << VIDEO_TRUST_VERSION << "\n";
    return 0;
  }

  std::string err;
  if (cmd == "verify") {
    VerifyArgs args;
    if (!ParseVerifyArgs(argc, argv, args, err)) {
      std::cerr << "error: " << err << "\n";
      Usage(std::cerr);
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return RunVerify(args);
  }
  if (cmd == "inspect") {
    VerifyArgs args;
    if (!ParseInspectArgs(argc, argv, args, err)) {
      std::cerr << "error: " << err << "\n";
      Usage(std::cerr);
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return RunInspect(args);
  }
  if (cmd == "verify-live") {
    LiveArgs args;
    if (!ParseLiveArgs(argc, argv, args, err)) {
      std::cerr << "error: " << err << "\n";
      Usage(std::cerr);
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return RunLive(args);
  }
  if (cmd == "compare-preservation") {
    CompareArgs args;
    if (!ParseCompareArgs(argc, argv, args, err)) {
      std::cerr << "error: " << err << "\n";
      Usage(std::cerr);
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return RunCompare(args);
  }
  if (cmd == "sign") {
    SignArgs args;
    if (!ParseSignArgs(argc, argv, args, err)) {
      std::cerr << "error: " << err << "\n";
      Usage(std::cerr);
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return RunSign(args);
  }
  if (cmd == "tamper") {
    TamperArgs args;
    if (!ParseTamperArgs(argc, argv, args, err)) {
      std::cerr << "error: " << err << "\n";
      Usage(std::cerr);
      return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
    }
    return RunTamper(args);
  }

  std::cerr << "error: unknown subcommand (use verify, inspect, verify-live, "
               "compare-preservation, sign, or tamper)\n";
  Usage(std::cerr);
  return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
}
