#include "videotrust/correlation.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace videotrust {
namespace {

constexpr std::size_t kSha256Size = 32;
constexpr std::size_t kReadChunkSize = 64 * 1024;
constexpr std::size_t kDigestChunkSize = 64 * 1024;

using Digest = std::array<uint8_t, kSha256Size>;

struct EvpCtxDeleter {
  void operator()(EVP_MD_CTX* ctx) const noexcept { EVP_MD_CTX_free(ctx); }
};

using EvpCtx = std::unique_ptr<EVP_MD_CTX, EvpCtxDeleter>;

class Sha256 {
 public:
  Sha256() : ctx_(EVP_MD_CTX_new()) {
    ok_ = ctx_ && EVP_DigestInit_ex(ctx_.get(), EVP_sha256(), nullptr) == 1;
  }

  bool ok() const noexcept { return ok_; }

  bool Update(std::span<const uint8_t> bytes) {
    if (!ok_ || bytes.empty()) {
      return ok_;
    }
    ok_ = EVP_DigestUpdate(ctx_.get(), bytes.data(), bytes.size()) == 1;
    return ok_;
  }

  bool Final(Digest* digest) {
    unsigned int size = 0;
    if (!ok_ || EVP_DigestFinal_ex(ctx_.get(), digest->data(), &size) != 1 ||
        size != digest->size()) {
      ok_ = false;
      return false;
    }
    return true;
  }

 private:
  EvpCtx ctx_;
  bool ok_{false};
};

std::string Hex(const Digest& digest) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(digest.size() * 2, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    out[i * 2] = kHex[digest[i] >> 4];
    out[i * 2 + 1] = kHex[digest[i] & 0x0f];
  }
  return out;
}

struct NalFingerprint {
  std::string digest;
  NalKind kind{NalKind::Other};
  bool signing_sei{false};
};

struct ScanResult {
  ArtifactEvidence artifact;
  std::vector<NalFingerprint> nalus;
  std::vector<std::string> signing_sei_digests;
  bool signing_details_complete{true};
};

class StreamingAnnexBScanner {
 public:
  StreamingAnnexBScanner(Codec codec, const CorrelationLimits& limits)
      : codec_(codec), limits_(limits) {
    payload_chunk_.reserve(kDigestChunkSize);
    prefix_.reserve(std::min(limits.max_nal_prefix_bytes, kDigestChunkSize));
  }

  Expected<ScanResult> Scan(const std::string& path) {
    if (limits_.max_tracked_nalus == 0 || limits_.max_nal_prefix_bytes == 0) {
      return MakeError(ErrorCode::InvalidArgument,
                       "correlation limits must retain at least one NAL and prefix byte");
    }
    if (limits_.max_tracked_nalus > kMaxTrackedNalusHardLimit ||
        limits_.max_diagnostic_indices > kMaxDiagnosticIndicesHardLimit ||
        limits_.max_nal_prefix_bytes > kMaxNalPrefixBytesHardLimit) {
      return MakeError(ErrorCode::InvalidArgument,
                       "correlation limits exceed hard resource bounds");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
      return MakeError(ErrorCode::IoFailure, "cannot open input file: " + path);
    }
    if (!artifact_hash_.ok()) {
      return MakeError(ErrorCode::InternalError, "cannot initialize SHA-256");
    }

    std::array<uint8_t, kReadChunkSize> chunk{};
    while (in) {
      in.read(reinterpret_cast<char*>(chunk.data()),
              static_cast<std::streamsize>(chunk.size()));
      const std::streamsize got = in.gcount();
      if (got > 0) {
        const auto bytes = std::span<const uint8_t>(
            chunk.data(), static_cast<std::size_t>(got));
        if (!artifact_hash_.Update(bytes)) {
          return MakeError(ErrorCode::InternalError, "SHA-256 update failed");
        }
        result_.artifact.byte_size += static_cast<std::uint64_t>(bytes.size());
        for (uint8_t byte : bytes) {
          const Error error = Consume(byte);
          if (error.code != ErrorCode::Ok) {
            return error;
          }
        }
      }
    }
    if (!in.eof()) {
      return MakeError(ErrorCode::IoFailure, "failed reading input file: " + path);
    }
    if (!saw_start_code_) {
      return MakeError(ErrorCode::ParseError,
                       "Annex-B input does not begin with a start code");
    }
    for (std::size_t i = 0; i < pending_zeros_; ++i) {
      if (!FeedPayload(0)) {
        return MakeError(ErrorCode::InternalError, "NAL SHA-256 update failed");
      }
    }
    pending_zeros_ = 0;
    const Error finish_error = FinishNal();
    if (finish_error.code != ErrorCode::Ok) {
      return finish_error;
    }

    Digest artifact_digest{};
    if (!artifact_hash_.Final(&artifact_digest)) {
      return MakeError(ErrorCode::InternalError, "SHA-256 finalization failed");
    }
    result_.artifact.sha256 = Hex(artifact_digest);
    return std::move(result_);
  }

 private:
  Error Consume(uint8_t byte) {
    if (byte == 0) {
      ++pending_zeros_;
      return MakeError(ErrorCode::Ok, {});
    }

    if (byte == 1 && pending_zeros_ >= 2) {
      const std::size_t start_code_size = pending_zeros_ == 2 ? 3 : 4;
      if (!saw_start_code_) {
        if (pending_zeros_ != 2 && pending_zeros_ != 3) {
          return MakeError(ErrorCode::ParseError,
                           "Annex-B input does not begin with a start code");
        }
        saw_start_code_ = true;
      } else {
        const std::size_t payload_zeros = pending_zeros_ > 3 ? pending_zeros_ - 3 : 0;
        for (std::size_t i = 0; i < payload_zeros; ++i) {
          if (!FeedPayload(0)) {
            return MakeError(ErrorCode::InternalError, "NAL SHA-256 update failed");
          }
        }
        const Error finish_error = FinishNal();
        if (finish_error.code != ErrorCode::Ok) {
          return finish_error;
        }
      }
      pending_zeros_ = 0;
      BeginNal(start_code_size);
      return MakeError(ErrorCode::Ok, {});
    }

    if (!saw_start_code_) {
      return MakeError(ErrorCode::ParseError,
                       "Annex-B input does not begin with a start code");
    }
    for (std::size_t i = 0; i < pending_zeros_; ++i) {
      if (!FeedPayload(0)) {
        return MakeError(ErrorCode::InternalError, "NAL SHA-256 update failed");
      }
    }
    pending_zeros_ = 0;
    if (!FeedPayload(byte)) {
      return MakeError(ErrorCode::InternalError, "NAL SHA-256 update failed");
    }
    return MakeError(ErrorCode::Ok, {});
  }

  void BeginNal(std::size_t start_code_size) {
    start_code_size_ = start_code_size;
    payload_size_ = 0;
    prefix_.clear();
    payload_chunk_.clear();
    nal_hash_ = std::make_unique<Sha256>();
  }

  bool FeedPayload(uint8_t byte) {
    if (!nal_hash_ || !nal_hash_->ok()) {
      return false;
    }
    ++payload_size_;
    if (prefix_.size() < limits_.max_nal_prefix_bytes) {
      prefix_.push_back(byte);
    }
    payload_chunk_.push_back(byte);
    if (payload_chunk_.size() == kDigestChunkSize) {
      if (!nal_hash_->Update(payload_chunk_)) {
        return false;
      }
      payload_chunk_.clear();
    }
    return true;
  }

  Error FinishNal() {
    if (!nal_hash_ || payload_size_ == 0) {
      return MakeError(ErrorCode::ParseError, "zero-length NAL unit");
    }
    if (!nal_hash_->Update(payload_chunk_)) {
      return MakeError(ErrorCode::InternalError, "NAL SHA-256 update failed");
    }
    Digest digest{};
    if (!nal_hash_->Final(&digest)) {
      return MakeError(ErrorCode::InternalError, "NAL SHA-256 finalization failed");
    }
    const std::string digest_hex = Hex(digest);

    NalUnit prefix_nal;
    prefix_nal.start_code_size = start_code_size_;
    prefix_nal.bytes = start_code_size_ == 4 ? std::vector<uint8_t>{0, 0, 0, 1}
                                             : std::vector<uint8_t>{0, 0, 1};
    prefix_nal.bytes.insert(prefix_nal.bytes.end(), prefix_.begin(), prefix_.end());
    const NalKind kind = ClassifyNal(codec_, prefix_nal);
    const bool complete = prefix_.size() == payload_size_;
    const MediaSigningSeiStatus signing_status =
        ClassifyOnvifMediaSigningSei(codec_, prefix_nal, complete);
    const bool signing = signing_status == MediaSigningSeiStatus::Signing;
    if (signing_status == MediaSigningSeiStatus::Indeterminate) {
      result_.artifact.signing_sei_classification_complete = false;
    }
    if (signing) {
      ++result_.artifact.signing_sei_count;
      if (result_.signing_sei_digests.size() < limits_.max_tracked_nalus) {
        result_.signing_sei_digests.push_back(digest_hex);
      } else {
        result_.signing_details_complete = false;
      }
    }

    ++result_.artifact.nal_count;
    if (result_.artifact.sequence_details_complete &&
        result_.nalus.size() < limits_.max_tracked_nalus) {
      result_.nalus.push_back(NalFingerprint{digest_hex, kind, signing});
    } else {
      result_.artifact.sequence_details_complete = false;
      result_.nalus.clear();
      result_.nalus.shrink_to_fit();
    }
    nal_hash_.reset();
    payload_chunk_.clear();
    prefix_.clear();
    payload_size_ = 0;
    return MakeError(ErrorCode::Ok, {});
  }

  Codec codec_;
  const CorrelationLimits& limits_;
  Sha256 artifact_hash_;
  std::unique_ptr<Sha256> nal_hash_;
  ScanResult result_;
  bool saw_start_code_{false};
  std::size_t pending_zeros_{0};
  std::size_t start_code_size_{0};
  std::size_t payload_size_{0};
  std::vector<uint8_t> prefix_;
  std::vector<uint8_t> payload_chunk_;
};

void AddRole(const NalFingerprint& fingerprint, NalRoleCounts* counts) {
  if (fingerprint.signing_sei) {
    ++counts->signing_sei;
    return;
  }
  switch (fingerprint.kind) {
    case NalKind::Vcl:
      ++counts->vcl;
      break;
    case NalKind::Sei:
      ++counts->sei;
      break;
    case NalKind::ParameterSet:
      ++counts->parameter_set;
      break;
    case NalKind::Other:
      ++counts->other;
      break;
  }
}

void AddSample(std::uint64_t index, std::size_t limit, IndexSamples* samples) {
  ++samples->total_count;
  if (samples->indices.size() < limit) {
    samples->indices.push_back(index);
  } else {
    samples->samples_truncated = true;
  }
}

std::unordered_map<std::string, std::uint64_t> Frequencies(
    const std::vector<NalFingerprint>& nalus) {
  std::unordered_map<std::string, std::uint64_t> counts;
  for (const auto& nal : nalus) {
    ++counts[nal.digest];
  }
  return counts;
}

std::uint64_t MultisetOverlap(
    const std::unordered_map<std::string, std::uint64_t>& before,
    const std::unordered_map<std::string, std::uint64_t>& after) {
  std::uint64_t overlap = 0;
  for (const auto& [digest, after_count] : after) {
    const auto it = before.find(digest);
    if (it != before.end()) {
      overlap += std::min(it->second, after_count);
    }
  }
  return overlap;
}

std::vector<std::uint64_t> LeftmostSubsequence(
    const std::vector<NalFingerprint>& before,
    const std::vector<NalFingerprint>& after) {
  std::vector<std::uint64_t> positions;
  positions.reserve(after.size());
  std::size_t i = 0;
  for (const auto& target : after) {
    while (i < before.size() && before[i].digest != target.digest) {
      ++i;
    }
    if (i == before.size()) {
      return {};
    }
    positions.push_back(static_cast<std::uint64_t>(i));
    ++i;
  }
  return positions;
}

std::vector<std::uint64_t> RightmostSubsequence(
    const std::vector<NalFingerprint>& before,
    const std::vector<NalFingerprint>& after) {
  std::vector<std::uint64_t> positions(after.size());
  std::size_t i = before.size();
  for (std::size_t a = after.size(); a > 0; --a) {
    while (i > 0 && before[i - 1].digest != after[a - 1].digest) {
      --i;
    }
    if (i == 0) {
      return {};
    }
    --i;
    positions[a - 1] = static_cast<std::uint64_t>(i);
  }
  return positions;
}

void RecordUnmatched(const std::vector<NalFingerprint>& source,
                     std::unordered_map<std::string, std::uint64_t> available,
                     std::size_t sample_limit,
                     IndexSamples* samples,
                     NalRoleCounts* roles) {
  for (std::size_t i = 0; i < source.size(); ++i) {
    auto it = available.find(source[i].digest);
    if (it != available.end() && it->second > 0) {
      --it->second;
      continue;
    }
    AddSample(static_cast<std::uint64_t>(i), sample_limit, samples);
    AddRole(source[i], roles);
  }
}

SigningSeiCorrelationEvidence CorrelateSigningSeis(const ScanResult& before,
                                                   const ScanResult& after) {
  SigningSeiCorrelationEvidence out;
  out.before_count = before.artifact.signing_sei_count;
  out.after_count = after.artifact.signing_sei_count;
  if (!before.artifact.signing_sei_classification_complete ||
      !after.artifact.signing_sei_classification_complete ||
      !before.signing_details_complete || !after.signing_details_complete) {
    return out;
  }
  std::unordered_map<std::string, std::uint64_t> before_counts;
  std::unordered_map<std::string, std::uint64_t> after_counts;
  for (const auto& digest : before.signing_sei_digests) {
    ++before_counts[digest];
  }
  for (const auto& digest : after.signing_sei_digests) {
    ++after_counts[digest];
  }
  out.matched_payload_count = MultisetOverlap(before_counts, after_counts);
  out.missing_from_after_count = out.before_count - out.matched_payload_count;
  out.unmatched_after_count = out.after_count - out.matched_payload_count;
  out.correlation_complete = EvidenceState::Yes;
  return out;
}

NalCorrelationEvidence CorrelateNalus(const ScanResult& before,
                                      const ScanResult& after,
                                      const CorrelationLimits& limits) {
  NalCorrelationEvidence out;
  if (!before.artifact.sequence_details_complete ||
      !after.artifact.sequence_details_complete) {
    out.diagnostic_detail_bounded = true;
    return out;
  }

  const bool equal = before.nalus.size() == after.nalus.size() &&
                     std::equal(before.nalus.begin(), before.nalus.end(),
                                after.nalus.begin(),
                                [](const NalFingerprint& a, const NalFingerprint& b) {
                                  return a.digest == b.digest;
                                });
  out.sequence_equivalent = equal ? EvidenceState::Yes : EvidenceState::No;

  const auto left = LeftmostSubsequence(before.nalus, after.nalus);
  const bool subsequence = left.size() == after.nalus.size();
  out.after_is_ordered_subsequence =
      subsequence ? EvidenceState::Yes : EvidenceState::No;
  if (subsequence) {
    const auto right = RightmostSubsequence(before.nalus, after.nalus);
    const bool unique = left == right;
    out.unique_subsequence_alignment = unique ? EvidenceState::Yes : EvidenceState::No;
    if (unique && !left.empty()) {
      out.unique_before_start = left.front();
      out.unique_before_end = left.back();
    }
  } else {
    out.unique_subsequence_alignment = EvidenceState::Indeterminate;
  }

  const auto before_counts = Frequencies(before.nalus);
  const auto after_counts = Frequencies(after.nalus);
  out.normalized_payload_matches = MultisetOverlap(before_counts, after_counts);
  RecordUnmatched(before.nalus, after_counts, limits.max_diagnostic_indices,
                  &out.unmatched_before, &out.unmatched_before_roles);
  RecordUnmatched(after.nalus, before_counts, limits.max_diagnostic_indices,
                  &out.unmatched_after, &out.unmatched_after_roles);
  out.diagnostic_detail_bounded = out.unmatched_before.samples_truncated ||
                                  out.unmatched_after.samples_truncated;

  bool duplicated = false;
  for (const auto& [digest, after_count] : after_counts) {
    const auto it = before_counts.find(digest);
    if (it != before_counts.end() && after_count > it->second) {
      duplicated = true;
      break;
    }
  }
  out.duplicated_after = duplicated ? EvidenceState::Yes : EvidenceState::No;

  if (subsequence) {
    out.reordered = EvidenceState::No;
  } else if (out.normalized_payload_matches == after.nalus.size()) {
    out.reordered = EvidenceState::Yes;
  } else {
    out.reordered = EvidenceState::Indeterminate;
  }
  return out;
}

}  // namespace

Expected<MediaCorrelationEvidence> CorrelateAnnexBFiles(
    const CorrelationOptions& options) {
  StreamingAnnexBScanner before_scanner(options.codec, options.limits);
  auto before = before_scanner.Scan(options.before_path);
  if (!before.ok()) {
    return before.error();
  }
  StreamingAnnexBScanner after_scanner(options.codec, options.limits);
  auto after = after_scanner.Scan(options.after_path);
  if (!after.ok()) {
    return after.error();
  }

  MediaCorrelationEvidence out;
  out.before = before.value().artifact;
  out.after = after.value().artifact;
  out.byte_identical = out.before.byte_size == out.after.byte_size &&
                               out.before.sha256 == out.after.sha256
                           ? EvidenceState::Yes
                           : EvidenceState::No;
  out.nal = CorrelateNalus(before.value(), after.value(), options.limits);
  out.signing_sei = CorrelateSigningSeis(before.value(), after.value());
  return out;
}

const char* ToString(EvidenceState value) noexcept {
  switch (value) {
    case EvidenceState::No:
      return "no";
    case EvidenceState::Yes:
      return "yes";
    case EvidenceState::Indeterminate:
      return "indeterminate";
  }
  return "unknown";
}

}  // namespace videotrust
