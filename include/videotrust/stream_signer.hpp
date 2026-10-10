#pragma once

#include "videotrust/annexb.hpp"
#include "videotrust/session.hpp"

#include <functional>
#include <span>

namespace videotrust {

inline constexpr std::size_t kSigningMaxNalBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kSigningMaxPemBytes = 256 * 1024;
inline constexpr std::size_t kSigningMaxOutputBytes = 1024 * 1024 * 1024;
inline constexpr unsigned kSigningMaxFrames = 300;

enum class SigningState { Active, Finalized, Aborted, Failed };
enum class SigningFailure { None, Input, Signer, Output };

struct StreamSigningOptions {
  Codec codec{Codec::H264};
  std::string key_pem_path;
  std::string cert_pem_path;
  std::size_t max_output_bytes{kSigningMaxOutputBytes};
  unsigned max_signing_frames{kSigningMaxFrames};
};

/// Synchronous, single-threaded, non-reentrant sink. Span is borrowed only for
/// this call. Return Ok only after accepting every byte; no dropping/queueing.
/// Caller owns sink deadlines, flush/close and separate artifact per epoch.
using SignedNalSink = std::function<Error(std::span<const uint8_t>)>;

/// One gateway signing epoch, using official ONVIF state across NALs/AUs.
/// Execution success does not establish camera/source or event authenticity.
/// New Create + separate sink/artifact is required after every terminal state.
class StreamSigner {
 public:
  static Expected<StreamSigner> Create(const StreamSigningOptions& options,
                                       SignedNalSink sink);
  StreamSigner(const StreamSigner&) = delete;
  StreamSigner& operator=(const StreamSigner&) = delete;
  StreamSigner(StreamSigner&&) noexcept = default;
  StreamSigner& operator=(StreamSigner&&) noexcept = default;

  /// Complete unsigned Annex-B NAL + gateway observation time (UTC FILETIME).
  Error AddNal(const NalUnit& nal, int64_t timestamp_100ns);
  /// Clean EOS: force and drain trailing official metadata, then terminate.
  Error Finalize();
  /// Gap, reconnect, replacement or cancellation: no final SEI is fabricated.
  void Abort() noexcept;

  SigningState state() const noexcept { return state_; }
  SigningFailure failure() const noexcept { return failure_; }
  std::size_t output_bytes() const noexcept { return output_bytes_; }
  std::size_t input_nalus() const noexcept { return input_nalus_; }

 private:
  StreamSigner(MediaSigningSession session, StreamSigningOptions options,
               SignedNalSink sink);
  Error Fail(SigningFailure stage, ErrorCode code, const char* message);
  Error Emit(std::span<const uint8_t> bytes);
  Error Drain(const NalUnit* peek, int64_t timestamp);

  MediaSigningSession session_;
  StreamSigningOptions options_;
  SignedNalSink sink_;
  SigningState state_{SigningState::Active};
  SigningFailure failure_{SigningFailure::None};
  std::size_t output_bytes_{0};
  std::size_t input_nalus_{0};
};

}  // namespace videotrust
