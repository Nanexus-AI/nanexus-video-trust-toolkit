#pragma once

#include "videotrust/error.hpp"
#include "videotrust/nal_classify.hpp"
#include "videotrust/result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace videotrust {

inline constexpr std::size_t kMaxTrackedNalusHardLimit = 250000;
inline constexpr std::size_t kMaxDiagnosticIndicesHardLimit = 1024;
inline constexpr std::size_t kMaxNalPrefixBytesHardLimit = 64 * 1024;

enum class EvidenceState {
  No,
  Yes,
  Indeterminate,
};

/// Factual identity and bounded scan evidence for one Annex-B artifact.
struct ArtifactEvidence {
  std::uint64_t byte_size{0};
  std::string sha256;
  std::uint64_t nal_count{0};
  std::uint64_t signing_sei_count{0};
  bool sequence_details_complete{true};
  bool signing_sei_classification_complete{true};
};

struct IndexSamples {
  std::uint64_t total_count{0};
  std::vector<std::uint64_t> indices;
  bool samples_truncated{false};
};

struct NalRoleCounts {
  std::uint64_t vcl{0};
  std::uint64_t sei{0};
  std::uint64_t signing_sei{0};
  std::uint64_t parameter_set{0};
  std::uint64_t other{0};
};

/// Compact normalized-NAL correlation evidence. "Normalized payload" means
/// the exact NAL bytes after removal of the 3- or 4-byte Annex-B start code;
/// it does not decode RBSP data or remove emulation-prevention bytes. These
/// fields do not express a preservation verdict or source-coverage policy.
struct NalCorrelationEvidence {
  EvidenceState sequence_equivalent{EvidenceState::Indeterminate};
  EvidenceState after_is_ordered_subsequence{EvidenceState::Indeterminate};
  /// Yes: one alignment; No: multiple plausible alignments; Indeterminate:
  /// no ordered subsequence or bounded detail prevented the decision.
  EvidenceState unique_subsequence_alignment{EvidenceState::Indeterminate};
  EvidenceState reordered{EvidenceState::Indeterminate};
  EvidenceState duplicated_after{EvidenceState::Indeterminate};
  std::uint64_t normalized_payload_matches{0};
  IndexSamples unmatched_before;
  IndexSamples unmatched_after;
  NalRoleCounts unmatched_before_roles;
  NalRoleCounts unmatched_after_roles;
  std::optional<std::uint64_t> unique_before_start;
  std::optional<std::uint64_t> unique_before_end;
  bool diagnostic_detail_bounded{false};
};

struct SigningSeiCorrelationEvidence {
  std::uint64_t before_count{0};
  std::uint64_t after_count{0};
  std::uint64_t matched_payload_count{0};
  std::uint64_t missing_from_after_count{0};
  std::uint64_t unmatched_after_count{0};
  EvidenceState correlation_complete{EvidenceState::Indeterminate};
};

struct CorrelationLimits {
  /// Maximum normalized NAL fingerprints retained per artifact. Payloads are
  /// always streamed; exceeding this bound keeps artifact/count evidence but
  /// makes detailed sequence conclusions indeterminate.
  std::size_t max_tracked_nalus{100000};
  /// Maximum example indices retained for each unmatched side.
  std::size_t max_diagnostic_indices{16};
  /// Maximum prefix retained for codec role/signing-SEI classification.
  std::size_t max_nal_prefix_bytes{4096};
};

struct CorrelationOptions {
  Codec codec{Codec::H264};
  std::string before_path;
  std::string after_path;
  CorrelationLimits limits{};
};

/// M3-A evidence only. No preservation or source-coverage conclusion is made.
struct MediaCorrelationEvidence {
  ArtifactEvidence before;
  ArtifactEvidence after;
  EvidenceState byte_identical{EvidenceState::Indeterminate};
  NalCorrelationEvidence nal;
  SigningSeiCorrelationEvidence signing_sei;
};

Expected<MediaCorrelationEvidence> CorrelateAnnexBFiles(
    const CorrelationOptions& options);

const char* ToString(EvidenceState value) noexcept;

}  // namespace videotrust
