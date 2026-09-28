#include "videotrust/correlation.hpp"
#include "videotrust/nal_classify.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<uint8_t>;
using Stream = std::vector<Bytes>;
using videotrust::Codec;
using videotrust::EvidenceState;

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

Bytes Vcl(Codec codec, uint8_t marker) {
  if (codec == Codec::H264) {
    return {0x65, 0x88, marker, 0x11, 0x22};
  }
  return {0x26, 0x01, 0x88, marker, 0x11, 0x22};
}

Bytes ParameterSet(Codec codec) {
  return codec == Codec::H264 ? Bytes{0x67, 0x42, 0x00, 0x0a}
                              : Bytes{0x40, 0x01, 0x0c, 0x01};
}

Bytes UserDataSei(Codec codec, bool signing) {
  Bytes out = codec == Codec::H264 ? Bytes{0x06, 0x05, 0x10}
                                   : Bytes{0x4e, 0x01, 0x05, 0x10};
  if (signing) {
    out.insert(out.end(), videotrust::kOnvifMediaSigningUuid,
               videotrust::kOnvifMediaSigningUuid +
                   videotrust::kOnvifMediaSigningUuidLen);
  } else {
    out.insert(out.end(), videotrust::kOnvifMediaSigningUuidLen, 0xa5);
  }
  out.push_back(0x80);
  return out;
}

Bytes Encode(const Stream& nalus, std::size_t start_code_size) {
  Bytes out;
  const Bytes start = start_code_size == 4 ? Bytes{0, 0, 0, 1}
                                           : Bytes{0, 0, 1};
  for (const auto& nal : nalus) {
    out.insert(out.end(), start.begin(), start.end());
    out.insert(out.end(), nal.begin(), nal.end());
  }
  return out;
}

void Write(const fs::path& path, const Bytes& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

videotrust::MediaCorrelationEvidence Correlate(
    Codec codec,
    const fs::path& dir,
    const std::string& name,
    const Bytes& before,
    const Bytes& after,
    videotrust::CorrelationLimits limits = {}) {
  const fs::path before_path = dir / (name + "-before.es");
  const fs::path after_path = dir / (name + "-after.es");
  Write(before_path, before);
  Write(after_path, after);
  videotrust::CorrelationOptions options;
  options.codec = codec;
  options.before_path = before_path.string();
  options.after_path = after_path.string();
  options.limits = limits;
  auto result = videotrust::CorrelateAnnexBFiles(options);
  if (!result.ok()) {
    throw std::runtime_error(result.error().message);
  }
  return std::move(result).value();
}

int RunCodecMatrix(Codec codec, const fs::path& dir) {
  const std::string tag = codec == Codec::H264 ? "h264" : "h265";
  const Bytes a = Vcl(codec, 0x01);
  const Bytes b = Vcl(codec, 0x02);
  const Bytes c = Vcl(codec, 0x03);
  const Bytes d = Vcl(codec, 0x04);
  const Bytes parameter = ParameterSet(codec);
  const Bytes signing = UserDataSei(codec, true);
  const Bytes metadata = UserDataSei(codec, false);

  // 1. Exact artifact.
  const Stream base{parameter, a, signing, b, c, d};
  const Bytes base4 = Encode(base, 4);
  auto exact = Correlate(codec, dir, tag + "-exact", base4, base4);
  if (exact.byte_identical != EvidenceState::Yes ||
      exact.nal.sequence_equivalent != EvidenceState::Yes ||
      exact.nal.after_is_ordered_subsequence != EvidenceState::Yes ||
      exact.nal.unmatched_before.total_count != 0 ||
      exact.nal.unmatched_after.total_count != 0 || exact.before.sha256.size() != 64) {
    return Fail(tag + " exact evidence");
  }

  // 2. Same normalized NAL payload sequence, different Annex-B framing.
  auto framing = Correlate(codec, dir, tag + "-framing", base4, Encode(base, 3));
  if (framing.byte_identical != EvidenceState::No ||
      framing.nal.sequence_equivalent != EvidenceState::Yes) {
    return Fail(tag + " framing normalization");
  }

  // 3/7. Clean unique ordered subset, equivalent to a NAL-boundary truncation.
  const Stream source{a, b, c, d};
  const Stream subset{b, c};
  auto subsequence = Correlate(codec, dir, tag + "-subsequence", Encode(source, 4),
                               Encode(subset, 4));
  if (subsequence.nal.after_is_ordered_subsequence != EvidenceState::Yes ||
      subsequence.nal.sequence_equivalent != EvidenceState::No ||
      subsequence.nal.unique_subsequence_alignment != EvidenceState::Yes ||
      subsequence.nal.unique_before_start != 1 ||
      subsequence.nal.unique_before_end != 2 ||
      subsequence.nal.unmatched_before.total_count != 2 ||
      subsequence.nal.unmatched_after.total_count != 0) {
    return Fail(tag + " ordered subsequence");
  }

  // 4. Signing SEI stripped, with factual signing metadata counts only.
  const Stream signed_source{a, signing, b};
  const Stream stripped{a, b};
  auto strip = Correlate(codec, dir, tag + "-strip-signing",
                         Encode(signed_source, 4), Encode(stripped, 4));
  if (strip.before.signing_sei_count != 1 || strip.after.signing_sei_count != 0 ||
      strip.signing_sei.missing_from_after_count != 1 ||
      strip.nal.unmatched_before_roles.signing_sei != 1) {
    return Fail(tag + " signing SEI correlation");
  }

  // 5. Same source units in a changed order.
  auto reordered = Correlate(codec, dir, tag + "-reordered",
                             Encode(Stream{a, b, c}, 4),
                             Encode(Stream{b, a, c}, 4));
  if (reordered.nal.sequence_equivalent != EvidenceState::No ||
      reordered.nal.after_is_ordered_subsequence != EvidenceState::No ||
      reordered.nal.reordered != EvidenceState::Yes ||
      reordered.nal.normalized_payload_matches != 3) {
    return Fail(tag + " reordered evidence");
  }

  // 6. Duplicated source material is not a clean subsequence.
  auto duplicate = Correlate(codec, dir, tag + "-duplicate",
                             Encode(Stream{a, b, c}, 4),
                             Encode(Stream{a, b, b, c}, 4));
  if (duplicate.nal.duplicated_after != EvidenceState::Yes ||
      duplicate.nal.after_is_ordered_subsequence != EvidenceState::No ||
      duplicate.nal.unmatched_after.total_count != 1) {
    return Fail(tag + " duplicate evidence");
  }

  // 8. Repeated payloads produce multiple valid source alignments.
  auto ambiguous = Correlate(codec, dir, tag + "-ambiguous",
                             Encode(Stream{a, b, a}, 4), Encode(Stream{a}, 4));
  if (ambiguous.nal.after_is_ordered_subsequence != EvidenceState::Yes ||
      ambiguous.nal.unique_subsequence_alignment != EvidenceState::No ||
      ambiguous.nal.unique_before_start.has_value() ||
      ambiguous.nal.unique_before_end.has_value()) {
    return Fail(tag + " repeated-NAL ambiguity");
  }

  // 9. Trustworthy unrelated user-data-unregistered SEI removal: classifier
  // proves the removed SEI does not carry the ONVIF Media Signing UUID.
  const Stream metadata_source{a, metadata, signing, b};
  const Stream metadata_removed{a, signing, b};
  auto metadata_case = Correlate(codec, dir, tag + "-metadata",
                                 Encode(metadata_source, 4),
                                 Encode(metadata_removed, 4));
  if (metadata_case.nal.unmatched_before.total_count != 1 ||
      metadata_case.nal.unmatched_before_roles.sei != 1 ||
      metadata_case.nal.unmatched_before_roles.signing_sei != 0 ||
      metadata_case.signing_sei.matched_payload_count != 1 ||
      metadata_case.signing_sei.missing_from_after_count != 0) {
    return Fail(tag + " non-signing metadata evidence");
  }

  // Explicit fingerprint retention limit keeps artifact hashing/counting
  // complete while making sequence conclusions indeterminate.
  videotrust::CorrelationLimits limits;
  limits.max_tracked_nalus = 2;
  auto bounded = Correlate(codec, dir, tag + "-bounded", Encode(source, 4),
                           Encode(source, 4), limits);
  if (bounded.before.nal_count != 4 || bounded.after.nal_count != 4 ||
      bounded.before.sequence_details_complete ||
      bounded.nal.sequence_equivalent != EvidenceState::Indeterminate ||
      !bounded.nal.diagnostic_detail_bounded) {
    return Fail(tag + " resource-bound evidence");
  }

  limits.max_tracked_nalus = videotrust::kMaxTrackedNalusHardLimit + 1;
  const fs::path invalid_before = dir / (tag + "-invalid-limit-before.es");
  const fs::path invalid_after = dir / (tag + "-invalid-limit-after.es");
  Write(invalid_before, Encode(source, 4));
  Write(invalid_after, Encode(source, 4));
  videotrust::CorrelationOptions invalid_options;
  invalid_options.codec = codec;
  invalid_options.before_path = invalid_before.string();
  invalid_options.after_path = invalid_after.string();
  invalid_options.limits = limits;
  if (videotrust::CorrelateAnnexBFiles(invalid_options).ok()) {
    return Fail(tag + " hard resource limit");
  }

  const fs::path empty_path = dir / (tag + "-empty.es");
  const fs::path malformed_path = dir / (tag + "-malformed.es");
  Write(empty_path, {});
  Write(malformed_path, Bytes{0x65, 0x88, 0x01});
  videotrust::CorrelationOptions parse_options;
  parse_options.codec = codec;
  parse_options.before_path = empty_path.string();
  parse_options.after_path = invalid_after.string();
  if (videotrust::CorrelateAnnexBFiles(parse_options).ok()) {
    return Fail(tag + " empty input accepted");
  }
  parse_options.before_path = malformed_path.string();
  if (videotrust::CorrelateAnnexBFiles(parse_options).ok()) {
    return Fail(tag + " malformed input accepted");
  }

  return 0;
}

}  // namespace

int main() {
  const fs::path dir = fs::temp_directory_path() / "vt-correlation-unit";
  fs::create_directories(dir);
  try {
    if (const int rc = RunCodecMatrix(Codec::H264, dir); rc != 0) {
      return rc;
    }
    if (const int rc = RunCodecMatrix(Codec::H265, dir); rc != 0) {
      return rc;
    }
  } catch (const std::exception& error) {
    return Fail(error.what());
  }
  std::cout << "PASS: artifact/NAL correlation (H.264 + H.265)\n";
  return 0;
}
