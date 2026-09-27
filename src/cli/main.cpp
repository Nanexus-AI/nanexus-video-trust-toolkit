#include "videotrust/error.hpp"
#include "videotrust/render.hpp"
#include "videotrust/result.hpp"
#include "videotrust/verify.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Usage(std::ostream& out) {
  out << "Usage:\n"
      << "  video-trust verify --codec h264|h265 [--ca PATH] [--json] <input.es>\n"
      << "\n"
      << "Verify ONVIF Media Signing on an Annex-B H.264/H.265 elementary stream.\n"
      << "Exit codes: 0 success/valid, 1 integrity fail, 2 usage/input, 3 runtime, "
         "4 unsigned/not verifiable\n";
}

struct Args {
  bool json{false};
  videotrust::Codec codec{videotrust::Codec::H264};
  bool codec_set{false};
  std::string ca;
  std::string input;
};

bool ParseArgs(int argc, char** argv, Args& out, std::string& err) {
  if (argc < 2) {
    err = "missing subcommand";
    return false;
  }
  if (std::string_view(argv[1]) != "verify") {
    err = "unknown subcommand (only 'verify' is implemented in this build)";
    return false;
  }

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
      const std::string_view v(argv[++i]);
      if (v == "h264") {
        out.codec = videotrust::Codec::H264;
      } else if (v == "h265") {
        out.codec = videotrust::Codec::H265;
      } else {
        err = "unsupported codec (use h264 or h265)";
        return false;
      }
      out.codec_set = true;
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

  if (!out.codec_set) {
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

}  // namespace

int main(int argc, char** argv) {
  Args args;
  std::string err;
  if (!ParseArgs(argc, argv, args, err)) {
    std::cerr << "error: " << err << "\n";
    Usage(std::cerr);
    return static_cast<int>(videotrust::ExitCode::UsageOrInputError);
  }

  videotrust::VerifyOptions opt;
  opt.codec = args.codec;
  opt.input_path = args.input;
  opt.ca_pem_path = args.ca;

  auto result = videotrust::VerifyAnnexBFile(opt);
  if (!result.ok()) {
    const auto& e = result.error();
    std::cerr << "error: " << e.message << "\n";
    using videotrust::ErrorCode;
    if (e.code == ErrorCode::ParseError || e.code == ErrorCode::InvalidArgument ||
        e.code == ErrorCode::IoFailure) {
      // Missing file / malformed Annex-B → usage/input class.
      if (e.code == ErrorCode::IoFailure && e.message.find("cannot open") != std::string::npos) {
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
