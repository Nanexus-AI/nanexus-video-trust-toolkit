#pragma once

#include "videotrust/error.hpp"
#include "videotrust/result.hpp"

#include <memory>
#include <string>

struct _onvif_media_signing_t;
typedef struct _onvif_media_signing_t onvif_media_signing_t;

namespace videotrust {

/// Thin RAII owner for an official ONVIF Media Signing session handle.
///
/// Both signing and validation use onvif_media_signing_create/free. Specialized
/// SignerSession / ValidatorSession aliases document intent without a deep hierarchy.
class MediaSigningSession {
 public:
  static Expected<MediaSigningSession> Create(Codec codec);

  MediaSigningSession(const MediaSigningSession&) = delete;
  MediaSigningSession& operator=(const MediaSigningSession&) = delete;

  MediaSigningSession(MediaSigningSession&& other) noexcept;
  MediaSigningSession& operator=(MediaSigningSession&& other) noexcept;

  ~MediaSigningSession();

  onvif_media_signing_t* get() const noexcept { return handle_; }
  explicit operator bool() const noexcept { return handle_ != nullptr; }

  /// Reset to a pre-stream state (official onvif_media_signing_reset).
  /// Returns Error on failure; ErrorCode::Ok on success.
  Error Reset();

  /// Load signing private key + certificate chain PEM (upstream key pair API).
  /// |certificate_chain_pem| should include the trust anchor; upstream strips it
  /// before embedding. Does not print or log PEM contents.
  Error SetSigningKeyPair(const std::string& private_key_pem,
                          const std::string& certificate_chain_pem);

  /// Enable/disable EPB in generated signing SEIs (required true for Annex-B).
  Error SetEmulationPreventionBeforeSigning(bool enable);

  Codec codec() const noexcept { return codec_; }

 private:
  MediaSigningSession(onvif_media_signing_t* handle, Codec codec) noexcept;

  onvif_media_signing_t* handle_{nullptr};
  Codec codec_{Codec::H264};
};

using SignerSession = MediaSigningSession;
using ValidatorSession = MediaSigningSession;

}  // namespace videotrust
