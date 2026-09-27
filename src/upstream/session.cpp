#include "videotrust/session.hpp"

#include <onvif_media_signing_common.h>
#include <onvif_media_signing_signer.h>

#include <utility>

namespace videotrust {
namespace {

MediaSigningCodec ToUpstreamCodec(Codec codec) {
  switch (codec) {
    case Codec::H264:
      return OMS_CODEC_H264;
    case Codec::H265:
      return OMS_CODEC_H265;
  }
  return OMS_CODEC_H264;
}

}  // namespace

MediaSigningSession::MediaSigningSession(onvif_media_signing_t* handle,
                                         Codec codec) noexcept
    : handle_(handle), codec_(codec) {}

MediaSigningSession::MediaSigningSession(MediaSigningSession&& other) noexcept
    : handle_(other.handle_), codec_(other.codec_) {
  other.handle_ = nullptr;
}

MediaSigningSession& MediaSigningSession::operator=(MediaSigningSession&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr) {
      onvif_media_signing_free(handle_);
    }
    handle_ = other.handle_;
    codec_ = other.codec_;
    other.handle_ = nullptr;
  }
  return *this;
}

MediaSigningSession::~MediaSigningSession() {
  if (handle_ != nullptr) {
    onvif_media_signing_free(handle_);
    handle_ = nullptr;
  }
}

Expected<MediaSigningSession> MediaSigningSession::Create(Codec codec) {
  onvif_media_signing_t* handle = onvif_media_signing_create(ToUpstreamCodec(codec));
  if (handle == nullptr) {
    return MakeError(ErrorCode::UpstreamFailure,
                     "onvif_media_signing_create returned null");
  }
  return MediaSigningSession(handle, codec);
}

Error MediaSigningSession::Reset() {
  if (handle_ == nullptr) {
    return MakeError(ErrorCode::InvalidArgument, "session handle is null");
  }
  const MediaSigningReturnCode rc = onvif_media_signing_reset(handle_);
  if (rc != OMS_OK) {
    return MakeError(ErrorCode::UpstreamFailure,
                     std::string("onvif_media_signing_reset failed: ") +
                         std::to_string(static_cast<int>(rc)));
  }
  return MakeError(ErrorCode::Ok, {});
}

Error MediaSigningSession::SetSigningKeyPair(const std::string& private_key_pem,
                                             const std::string& certificate_chain_pem) {
  if (handle_ == nullptr) {
    return MakeError(ErrorCode::InvalidArgument, "session handle is null");
  }
  if (private_key_pem.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "signing private key PEM is empty");
  }
  if (certificate_chain_pem.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "signer certificate chain PEM is empty");
  }
  const MediaSigningReturnCode rc = onvif_media_signing_set_signing_key_pair(
      handle_, private_key_pem.data(), private_key_pem.size(),
      certificate_chain_pem.data(), certificate_chain_pem.size(),
      /*user_provisioned=*/false);
  if (rc != OMS_OK) {
    // Do not echo PEM material.
    return MakeError(ErrorCode::UpstreamFailure,
                     "onvif_media_signing_set_signing_key_pair failed: " +
                         std::to_string(static_cast<int>(rc)));
  }
  return MakeError(ErrorCode::Ok, {});
}

Error MediaSigningSession::SetEmulationPreventionBeforeSigning(bool enable) {
  if (handle_ == nullptr) {
    return MakeError(ErrorCode::InvalidArgument, "session handle is null");
  }
  const MediaSigningReturnCode rc =
      onvif_media_signing_set_emulation_prevention_before_signing(handle_, enable);
  if (rc != OMS_OK) {
    return MakeError(ErrorCode::UpstreamFailure,
                     "onvif_media_signing_set_emulation_prevention_before_signing failed: " +
                         std::to_string(static_cast<int>(rc)));
  }
  return MakeError(ErrorCode::Ok, {});
}

}  // namespace videotrust
