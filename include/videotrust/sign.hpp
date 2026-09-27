#pragma once

#include "videotrust/error.hpp"
#include "videotrust/result.hpp"

#include <string>

namespace videotrust {

struct SignOptions {
  Codec codec{Codec::H264};
  std::string input_path;
  std::string output_path;
  /// Signing private key PEM path.
  std::string key_pem_path;
  /// Signer certificate chain PEM path (leaf … trust anchor).
  std::string cert_pem_path;
  /// Overwrite existing output when true.
  bool force{false};
};

/// Sign an Annex-B elementary stream with the official ONVIF signer C API.
/// Uses the unthreaded signing plugin (M1 default). Enables EPB on generated
/// SEIs so Annex-B start-code scanning remains deterministic.
///
/// On success returns ErrorCode::Ok. Does not establish camera/source authenticity.
Error SignAnnexBFile(const SignOptions& options);

/// Map a signing Error to the frozen sign exit-code contract (0 / 2 / 3).
ExitCode ExitCodeForSign(const Error& error) noexcept;

}  // namespace videotrust
