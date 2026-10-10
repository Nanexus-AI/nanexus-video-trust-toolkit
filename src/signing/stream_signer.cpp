#include "videotrust/stream_signer.hpp"

#include "videotrust/nal_classify.hpp"

#include <onvif_media_signing_signer.h>
#include <openssl/crypto.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <memory>
#include <utility>

namespace videotrust {
namespace {

// Erase temporary PEM buffers on every path. The official session owns its
// parsed key; no secret is retained in configuration or diagnostics.
struct PemBuffer {
  std::string bytes;
  ~PemBuffer() {
    if (!bytes.empty()) OPENSSL_cleanse(bytes.data(), bytes.size());
  }
};

Error LoadPem(const std::string& path, PemBuffer& pem) {
  std::error_code filesystem_error;
  if (!std::filesystem::is_regular_file(path, filesystem_error))
    return MakeError(ErrorCode::IoFailure, "signing identity must be a regular file");
  std::ifstream in(path, std::ios::binary);
  if (!in) return MakeError(ErrorCode::IoFailure, "signing identity unavailable");
  pem.bytes.reserve(kSigningMaxPemBytes);
  std::array<char, 4096> chunk{};
  while (in) {
    in.read(chunk.data(), chunk.size());
    const auto size = static_cast<std::size_t>(in.gcount());
    if (size > kSigningMaxPemBytes - pem.bytes.size()) {
      OPENSSL_cleanse(chunk.data(), chunk.size());
      return MakeError(ErrorCode::InvalidArgument, "signing identity exceeds bound");
    }
    pem.bytes.append(chunk.data(), size);
    OPENSSL_cleanse(chunk.data(), chunk.size());
  }
  if (!in.eof() || pem.bytes.empty())
    return MakeError(ErrorCode::InvalidArgument, "signing identity unreadable or empty");
  return MakeError(ErrorCode::Ok, {});
}

bool IdentityMatches(const PemBuffer& key, const PemBuffer& cert) {
  std::unique_ptr<BIO, decltype(&BIO_free)> key_bio(
      BIO_new_mem_buf(key.bytes.data(), static_cast<int>(key.bytes.size())), BIO_free);
  std::unique_ptr<BIO, decltype(&BIO_free)> cert_bio(
      BIO_new_mem_buf(cert.bytes.data(), static_cast<int>(cert.bytes.size())), BIO_free);
  if (!key_bio || !cert_bio) return false;
  // Encrypted keys are unsupported; avoid any interactive password prompt.
  auto no_password = [](char*, int, int, void*) -> int { return 0; };
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> private_key(
      PEM_read_bio_PrivateKey(key_bio.get(), nullptr, no_password, nullptr), EVP_PKEY_free);
  std::unique_ptr<X509, decltype(&X509_free)> leaf(
      PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr), X509_free);
  return private_key && leaf && X509_check_private_key(leaf.get(), private_key.get()) == 1;
}

Error TerminalError() {
  return MakeError(ErrorCode::InvalidArgument, "signing epoch is terminal");
}

}  // namespace

StreamSigner::StreamSigner(MediaSigningSession session,
                           StreamSigningOptions options, SignedNalSink sink)
    : session_(std::move(session)), options_(std::move(options)),
      sink_(std::move(sink)) {}

Expected<StreamSigner> StreamSigner::Create(const StreamSigningOptions& options,
                                           SignedNalSink sink) {
  if ((options.codec != Codec::H264 && options.codec != Codec::H265) ||
      !sink || options.key_pem_path.empty() || options.cert_pem_path.empty() ||
      options.max_output_bytes == 0 ||
      options.max_output_bytes > kSigningMaxOutputBytes ||
      options.max_signing_frames == 0 ||
      options.max_signing_frames > kSigningMaxFrames) {
    return MakeError(ErrorCode::InvalidArgument, "invalid stream signing configuration");
  }
  PemBuffer key, cert;
  auto error = LoadPem(options.key_pem_path, key);
  if (error.code != ErrorCode::Ok) return error;
  error = LoadPem(options.cert_pem_path, cert);
  if (error.code != ErrorCode::Ok) return error;
  if (!IdentityMatches(key, cert))
    return MakeError(ErrorCode::InvalidArgument, "signing identity malformed or mismatched");
  auto session = MediaSigningSession::Create(options.codec);
  if (!session.ok()) return MakeError(ErrorCode::UpstreamFailure, "signer creation failed");
  error = session.value().SetEmulationPreventionBeforeSigning(true);
  if (error.code == ErrorCode::Ok)
    error = session.value().SetSigningKeyPair(key.bytes, cert.bytes);
  if (error.code != ErrorCode::Ok ||
      onvif_media_signing_set_max_signing_frames(
          session.value().get(), options.max_signing_frames) != OMS_OK) {
    return MakeError(ErrorCode::UpstreamFailure, "signing identity or setup rejected");
  }
  return StreamSigner(std::move(session.value()), options, std::move(sink));
}

Error StreamSigner::Fail(SigningFailure stage, ErrorCode code, const char* message) {
  state_ = SigningState::Failed;
  failure_ = stage;
  return MakeError(code, message);
}

Error StreamSigner::Emit(std::span<const uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > kSigningMaxNalBytes ||
      bytes.size() > options_.max_output_bytes - output_bytes_) {
    return Fail(SigningFailure::Output, ErrorCode::IoFailure,
                "signed output exceeds bound");
  }
  try {
    if (sink_(bytes).code != ErrorCode::Ok)
      return Fail(SigningFailure::Output, ErrorCode::IoFailure, "signed output rejected");
  } catch (...) {
    return Fail(SigningFailure::Output, ErrorCode::IoFailure, "signed output failed");
  }
  output_bytes_ += bytes.size();
  return MakeError(ErrorCode::Ok, {});
}

Error StreamSigner::Drain(const NalUnit* peek, int64_t timestamp) {
  // Bound even an unexpected upstream metadata loop. The official unthreaded
  // plugin generates metadata synchronously; no polling thread is necessary.
  for (unsigned count = 0; count <= 64; ++count) {
    uint8_t* raw = nullptr;
    std::size_t size = 0;
    const auto rc = onvif_media_signing_get_sei(
        session_.get(), &raw, &size, nullptr,
        peek ? peek->bytes.data() : nullptr, peek ? peek->bytes.size() : 0, nullptr);
    std::unique_ptr<uint8_t, decltype(&std::free)> sei(raw, std::free);
    if (rc != OMS_OK)
      return Fail(SigningFailure::Signer, ErrorCode::UpstreamFailure,
                  "signer metadata retrieval failed");
    if (size == 0) return MakeError(ErrorCode::Ok, {});
    if (!raw || size > kSigningMaxNalBytes || count == 64)
      return Fail(SigningFailure::Signer, ErrorCode::UpstreamFailure,
                  "signer metadata exceeds bound");
    // Generated in-place SEIs must also pass through the official signer.
    // After EOS, adding NALs is forbidden, so trailing SEIs are emitted only.
    if (peek && onvif_media_signing_add_nalu_for_signing(
                    session_.get(), raw, size, timestamp) != OMS_OK)
      return Fail(SigningFailure::Signer, ErrorCode::UpstreamFailure,
                  "signer metadata processing failed");
    auto error = Emit({raw, size});
    if (error.code != ErrorCode::Ok) return error;
  }
  return Fail(SigningFailure::Signer, ErrorCode::InternalError, "signer drain failed");
}

Error StreamSigner::AddNal(const NalUnit& nal, int64_t timestamp) {
  if (state_ != SigningState::Active || !session_) return TerminalError();
  if (nal.bytes.size() > kSigningMaxNalBytes || timestamp < 0)
    return Fail(SigningFailure::Input, ErrorCode::InvalidArgument,
                "signing input exceeds bound or has invalid timestamp");
  // Parse exactly one NAL to reject false boundaries and inconsistent metadata.
  auto parsed = AnnexBReader::Parse(nal.bytes);
  if (!parsed.ok() || parsed.value().size() != 1 ||
      parsed.value()[0].start_code_size != nal.start_code_size ||
      nal.bytes.size() <= nal.start_code_size + NalHeaderLength(options_.codec) ||
      (nal.bytes[nal.start_code_size] & 0x80U) != 0 ||
      (options_.codec == Codec::H265 &&
       (nal.bytes[nal.start_code_size + 1] & 7U) == 0))
    return Fail(SigningFailure::Input, ErrorCode::ParseError,
                "signing input is not one complete codec NAL");
  if (IsOnvifMediaSigningSei(options_.codec, nal))
    return Fail(SigningFailure::Input, ErrorCode::InvalidArgument,
                "gateway input already contains Media Signing");
  auto error = Drain(&nal, timestamp);
  if (error.code != ErrorCode::Ok) return error;
  if (onvif_media_signing_add_nalu_for_signing(
          session_.get(), nal.bytes.data(), nal.bytes.size(), timestamp) != OMS_OK)
    return Fail(SigningFailure::Signer, ErrorCode::UpstreamFailure,
                "signer input processing failed");
  error = Emit(nal.bytes);
  if (error.code == ErrorCode::Ok) ++input_nalus_;
  return error;
}

Error StreamSigner::Finalize() {
  if (state_ != SigningState::Active || !session_) return TerminalError();
  if (input_nalus_ == 0)
    return Fail(SigningFailure::Input, ErrorCode::InvalidArgument, "empty signing epoch");
  if (onvif_media_signing_set_end_of_stream(session_.get()) != OMS_OK)
    return Fail(SigningFailure::Signer, ErrorCode::UpstreamFailure,
                "signer finalization failed");
  auto error = Drain(nullptr, 0);
  if (error.code == ErrorCode::Ok) state_ = SigningState::Finalized;
  return error;
}

void StreamSigner::Abort() noexcept {
  if (state_ == SigningState::Active) state_ = SigningState::Aborted;
}

}  // namespace videotrust
