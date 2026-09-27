#include "videotrust/sign.hpp"

#include "videotrust/annexb.hpp"
#include "videotrust/session.hpp"

#include <onvif_media_signing_signer.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace videotrust {
namespace {

namespace fs = std::filesystem;

Expected<std::string> ReadPemFile(const std::string& path, const char* what) {
  std::ifstream in(path);
  if (!in) {
    return MakeError(ErrorCode::IoFailure,
                     std::string("cannot open ") + what + " file: " + path);
  }
  std::string pem((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (pem.empty()) {
    return MakeError(ErrorCode::InvalidArgument,
                     std::string(what) + " file is empty: " + path);
  }
  return pem;
}

/// UTC FILETIME-ish: 100-ns intervals since 1601-01-01.
int64_t NowTimestamp100ns() {
  using namespace std::chrono;
  const auto unix_us =
      duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
  // Difference 1601→1970 in microseconds, then ×10 → 100-ns units.
  constexpr int64_t kEpochDiffUs = 11644473600000000LL;
  return (unix_us + kEpochDiffUs) * 10LL;
}

Error WriteSignedStream(MediaSigningSession& session,
                        const std::vector<NalUnit>& nalus,
                        const std::string& output_path) {
  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return MakeError(ErrorCode::IoFailure, "cannot write output: " + output_path);
  }

  int64_t ts = NowTimestamp100ns();
  for (const auto& nal : nalus) {
    for (;;) {
      uint8_t* sei = nullptr;
      size_t sei_size = 0;
      const MediaSigningReturnCode grc = onvif_media_signing_get_sei(
          session.get(), &sei, &sei_size, nullptr, nal.bytes.data(), nal.bytes.size(),
          nullptr);
      if (grc != OMS_OK) {
        free(sei);
        return MakeError(ErrorCode::UpstreamFailure,
                         "onvif_media_signing_get_sei failed: " +
                             std::to_string(static_cast<int>(grc)));
      }
      if (sei_size == 0) {
        break;
      }
      out.write(reinterpret_cast<const char*>(sei),
                static_cast<std::streamsize>(sei_size));
      if (!out) {
        free(sei);
        return MakeError(ErrorCode::IoFailure, "failed writing SEI to output");
      }
      // In-place SEIs must also pass through the signer (upstream guidance).
      const MediaSigningReturnCode arc =
          onvif_media_signing_add_nalu_for_signing(session.get(), sei, sei_size, ts);
      free(sei);
      if (arc != OMS_OK) {
        return MakeError(ErrorCode::UpstreamFailure,
                         "onvif_media_signing_add_nalu_for_signing(sei) failed: " +
                             std::to_string(static_cast<int>(arc)));
      }
    }

    const MediaSigningReturnCode nrc = onvif_media_signing_add_nalu_for_signing(
        session.get(), nal.bytes.data(), nal.bytes.size(), ts);
    if (nrc != OMS_OK) {
      return MakeError(ErrorCode::UpstreamFailure,
                       "onvif_media_signing_add_nalu_for_signing failed: " +
                           std::to_string(static_cast<int>(nrc)));
    }
    out.write(reinterpret_cast<const char*>(nal.bytes.data()),
              static_cast<std::streamsize>(nal.bytes.size()));
    if (!out) {
      return MakeError(ErrorCode::IoFailure, "failed writing NAL to output");
    }
    ts += 100000;  // +10 ms in 100-ns units between NALs
  }

  const MediaSigningReturnCode erc = onvif_media_signing_set_end_of_stream(session.get());
  if (erc != OMS_OK) {
    return MakeError(ErrorCode::UpstreamFailure,
                     "onvif_media_signing_set_end_of_stream failed: " +
                         std::to_string(static_cast<int>(erc)));
  }

  for (;;) {
    uint8_t* sei = nullptr;
    size_t sei_size = 0;
    const MediaSigningReturnCode grc = onvif_media_signing_get_sei(
        session.get(), &sei, &sei_size, nullptr, nullptr, 0, nullptr);
    if (grc != OMS_OK) {
      free(sei);
      return MakeError(ErrorCode::UpstreamFailure,
                       "onvif_media_signing_get_sei(eos) failed: " +
                           std::to_string(static_cast<int>(grc)));
    }
    if (sei_size == 0) {
      break;
    }
    out.write(reinterpret_cast<const char*>(sei),
              static_cast<std::streamsize>(sei_size));
    free(sei);
    if (!out) {
      return MakeError(ErrorCode::IoFailure, "failed writing trailing SEI");
    }
  }

  out.flush();
  if (!out) {
    return MakeError(ErrorCode::IoFailure, "failed flushing output: " + output_path);
  }
  return MakeError(ErrorCode::Ok, {});
}

}  // namespace

Error SignAnnexBFile(const SignOptions& options) {
  if (options.input_path.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "input path is required");
  }
  if (options.output_path.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "output path is required");
  }
  if (options.key_pem_path.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "--key path is required");
  }
  if (options.cert_pem_path.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "--cert path is required");
  }

  if (fs::exists(options.output_path) && !options.force) {
    return MakeError(ErrorCode::IoFailure,
                     "output exists (use --force to overwrite): " + options.output_path);
  }

  auto key = ReadPemFile(options.key_pem_path, "signing private key");
  if (!key.ok()) {
    return key.error();
  }
  auto cert = ReadPemFile(options.cert_pem_path, "signer certificate chain");
  if (!cert.ok()) {
    return cert.error();
  }

  auto nalus = AnnexBReader::ParseFile(options.input_path);
  if (!nalus.ok()) {
    return nalus.error();
  }
  if (nalus.value().empty()) {
    return MakeError(ErrorCode::ParseError, "no NAL units in input");
  }

  auto session = MediaSigningSession::Create(options.codec);
  if (!session.ok()) {
    return session.error();
  }

  // Annex-B requires EPB in generated SEIs (upstream default sei_epb is false).
  Error epb = session.value().SetEmulationPreventionBeforeSigning(true);
  if (epb.code != ErrorCode::Ok) {
    return epb;
  }

  Error key_err =
      session.value().SetSigningKeyPair(key.value(), cert.value());
  if (key_err.code != ErrorCode::Ok) {
    return key_err;
  }

  // Ensure parent directory exists when possible; missing parent → IoFailure.
  const fs::path out_path(options.output_path);
  if (out_path.has_parent_path() && !out_path.parent_path().empty() &&
      !fs::exists(out_path.parent_path())) {
    return MakeError(ErrorCode::IoFailure,
                     "output directory does not exist: " + out_path.parent_path().string());
  }

  return WriteSignedStream(session.value(), nalus.value(), options.output_path);
}

ExitCode ExitCodeForSign(const Error& error) noexcept {
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

}  // namespace videotrust
