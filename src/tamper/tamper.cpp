#include "videotrust/tamper.hpp"

#include "videotrust/annexb.hpp"
#include "videotrust/nal_classify.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

namespace videotrust {
namespace {

namespace fs = std::filesystem;

Error CheckIoPolicy(const TamperOptions& options) {
  if (options.input_path.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "input path is required");
  }
  if (options.output_path.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "output path is required");
  }

  std::error_code ec_in;
  std::error_code ec_out;
  const fs::path in = fs::weakly_canonical(options.input_path, ec_in);
  const fs::path out = fs::weakly_canonical(options.output_path, ec_out);
  if (options.input_path == options.output_path ||
      (!ec_in && !ec_out && !in.empty() && in == out)) {
    return MakeError(ErrorCode::InvalidArgument,
                     "input and output paths must differ (no in-place tamper)");
  }

  if (fs::exists(options.output_path) && !options.force) {
    return MakeError(ErrorCode::IoFailure,
                     "output exists (use --force to overwrite): " + options.output_path);
  }
  return MakeError(ErrorCode::Ok, {});
}

Error WriteNalus(const std::string& path, const std::vector<NalUnit>& nalus) {
  if (nalus.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "refusing to write empty Annex-B output");
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return MakeError(ErrorCode::IoFailure, "cannot write output: " + path);
  }
  for (const auto& nal : nalus) {
    out.write(reinterpret_cast<const char*>(nal.bytes.data()),
              static_cast<std::streamsize>(nal.bytes.size()));
    if (!out) {
      return MakeError(ErrorCode::IoFailure, "failed writing NAL to output");
    }
  }
  out.flush();
  if (!out) {
    return MakeError(ErrorCode::IoFailure, "failed flushing output: " + path);
  }
  return MakeError(ErrorCode::Ok, {});
}

Error CorruptVcl(Codec codec, std::vector<NalUnit> nalus, const std::string& out_path) {
  // Prefer the first mutable VCL that appears after at least one ONVIF signing SEI.
  // Mutating only a pre-signature VCL can leave authenticity "not_feasible" rather than
  // a clear integrity failure once the signed GOP is evaluated.
  bool seen_oms_sei = false;
  std::size_t fallback = static_cast<std::size_t>(-1);
  for (std::size_t i = 0; i < nalus.size(); ++i) {
    if (IsOnvifMediaSigningSei(codec, nalus[i])) {
      seen_oms_sei = true;
      continue;
    }
    std::size_t off = 0;
    if (!VclMutationOffset(codec, nalus[i], &off)) {
      continue;
    }
    if (fallback == static_cast<std::size_t>(-1)) {
      fallback = i;
    }
    if (!seen_oms_sei) {
      continue;
    }
    nalus[i].bytes[off] = static_cast<uint8_t>(nalus[i].bytes[off] ^ 0xff);
    return WriteNalus(out_path, nalus);
  }
  if (fallback != static_cast<std::size_t>(-1)) {
    std::size_t off = 0;
    if (VclMutationOffset(codec, nalus[fallback], &off)) {
      nalus[fallback].bytes[off] =
          static_cast<uint8_t>(nalus[fallback].bytes[off] ^ 0xff);
      return WriteNalus(out_path, nalus);
    }
  }
  return MakeError(ErrorCode::InvalidArgument,
                   "no suitable VCL NAL found for corrupt-vcl");
}

Error StripSigningSei(Codec codec, std::vector<NalUnit> nalus, const std::string& out_path) {
  std::vector<NalUnit> kept;
  kept.reserve(nalus.size());
  std::size_t removed = 0;
  for (auto& nal : nalus) {
    if (IsOnvifMediaSigningSei(codec, nal)) {
      ++removed;
      continue;
    }
    kept.push_back(std::move(nal));
  }
  if (removed == 0) {
    return MakeError(ErrorCode::InvalidArgument,
                     "no ONVIF Media Signing SEI found to strip");
  }
  return WriteNalus(out_path, kept);
}

Error TruncateNals(std::vector<NalUnit> nalus,
                   std::size_t remove_count,
                   const std::string& out_path) {
  if (remove_count == 0) {
    return MakeError(ErrorCode::InvalidArgument, "truncate count must be >= 1");
  }
  if (nalus.size() <= remove_count) {
    return MakeError(ErrorCode::InvalidArgument,
                     "truncate would remove entire stream (need at least one remaining NAL)");
  }
  nalus.resize(nalus.size() - remove_count);
  return WriteNalus(out_path, nalus);
}

}  // namespace

Error TamperAnnexBFile(const TamperOptions& options) {
  Error io = CheckIoPolicy(options);
  if (io.code != ErrorCode::Ok) {
    return io;
  }

  auto nalus = AnnexBReader::ParseFile(options.input_path);
  if (!nalus.ok()) {
    return nalus.error();
  }

  switch (options.operation) {
    case TamperOperation::CorruptVcl:
      return CorruptVcl(options.codec, std::move(nalus.value()), options.output_path);
    case TamperOperation::StripSigningSei:
      return StripSigningSei(options.codec, std::move(nalus.value()), options.output_path);
    case TamperOperation::Truncate:
      return TruncateNals(std::move(nalus.value()), options.truncate_nal_count,
                          options.output_path);
  }
  return MakeError(ErrorCode::InvalidArgument, "unknown tamper operation");
}

ExitCode ExitCodeForTamper(const Error& error) noexcept {
  if (error.code == ErrorCode::Ok) {
    return ExitCode::Success;
  }
  switch (error.code) {
    case ErrorCode::InvalidArgument:
    case ErrorCode::ParseError:
    case ErrorCode::NotSupported:
    case ErrorCode::IoFailure:
      return ExitCode::UsageOrInputError;
    case ErrorCode::UpstreamFailure:
    case ErrorCode::InternalError:
    case ErrorCode::Ok:
      break;
  }
  return ExitCode::RuntimeFailure;
}

const char* ToString(TamperOperation op) noexcept {
  switch (op) {
    case TamperOperation::CorruptVcl:
      return "corrupt-vcl";
    case TamperOperation::StripSigningSei:
      return "strip-signing-sei";
    case TamperOperation::Truncate:
      return "truncate";
  }
  return "unknown";
}

}  // namespace videotrust
