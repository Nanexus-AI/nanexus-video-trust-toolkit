#include "videotrust/nal_classify.hpp"

#include <cstring>

namespace videotrust {
namespace {

std::size_t SeiPayloadStart(Codec codec, const NalUnit& nal) {
  return nal.start_code_size + NalHeaderLength(codec);
}

/// Decode H.26x SEI payload size field (series of 0xff then a final byte < 0xff).
bool DecodeSeiPayloadSize(const uint8_t* p,
                          std::size_t avail,
                          std::size_t* size_out,
                          std::size_t* bytes_consumed) {
  if (avail == 0) {
    return false;
  }
  std::size_t size = 0;
  std::size_t i = 0;
  while (i < avail && p[i] == 0xff) {
    size += 255;
    ++i;
  }
  if (i >= avail) {
    return false;
  }
  size += p[i];
  ++i;
  *size_out = size;
  *bytes_consumed = i;
  return true;
}

}  // namespace

std::size_t NalHeaderLength(Codec codec) noexcept {
  return codec == Codec::H265 ? 2 : 1;
}

int RawNalType(Codec codec, const NalUnit& nal) noexcept {
  if (nal.bytes.size() <= nal.start_code_size) {
    return -1;
  }
  const uint8_t* hdr = nal.bytes.data() + nal.start_code_size;
  if (codec == Codec::H264) {
    return static_cast<int>(hdr[0] & 0x1f);
  }
  if (nal.bytes.size() < nal.start_code_size + 2) {
    return -1;
  }
  return static_cast<int>((hdr[0] & 0x7e) >> 1);
}

NalKind ClassifyNal(Codec codec, const NalUnit& nal) noexcept {
  const int t = RawNalType(codec, nal);
  if (t < 0) {
    return NalKind::Other;
  }
  if (codec == Codec::H264) {
    if (t == 1 || t == 5) {
      return NalKind::Vcl;
    }
    if (t == 6) {
      return NalKind::Sei;
    }
    if (t == 7 || t == 8 || t == 13 || t == 15) {
      return NalKind::ParameterSet;
    }
    return NalKind::Other;
  }
  // H.265
  if (t == 0 || t == 1 || (t >= 16 && t <= 21)) {
    return NalKind::Vcl;
  }
  if (t == 39 || t == 40) {
    return NalKind::Sei;
  }
  if (t == 32 || t == 33 || t == 34) {
    return NalKind::ParameterSet;
  }
  return NalKind::Other;
}

bool IsVclNal(Codec codec, const NalUnit& nal) noexcept {
  return ClassifyNal(codec, nal) == NalKind::Vcl;
}

bool IsSeiNal(Codec codec, const NalUnit& nal) noexcept {
  return ClassifyNal(codec, nal) == NalKind::Sei;
}

bool IsOnvifMediaSigningSei(Codec codec, const NalUnit& nal) noexcept {
  if (!IsSeiNal(codec, nal)) {
    return false;
  }
  const std::size_t payload_off = SeiPayloadStart(codec, nal);
  if (nal.bytes.size() <= payload_off) {
    return false;
  }
  const uint8_t* p = nal.bytes.data() + payload_off;
  std::size_t avail = nal.bytes.size() - payload_off;
  // SEI payload type
  if (*p != 0x05) {  // USER_DATA_UNREGISTERED
    return false;
  }
  ++p;
  --avail;
  std::size_t payload_size = 0;
  std::size_t size_bytes = 0;
  if (!DecodeSeiPayloadSize(p, avail, &payload_size, &size_bytes)) {
    return false;
  }
  p += size_bytes;
  avail -= size_bytes;
  if (avail < kOnvifMediaSigningUuidLen || payload_size < kOnvifMediaSigningUuidLen) {
    return false;
  }
  return std::memcmp(p, kOnvifMediaSigningUuid, kOnvifMediaSigningUuidLen) == 0;
}

bool VclMutationOffset(Codec codec, const NalUnit& nal, std::size_t* offset) noexcept {
  if (!IsVclNal(codec, nal)) {
    return false;
  }
  const std::size_t hdr = NalHeaderLength(codec);
  // Preserve start code + NAL header; mutate a later payload byte.
  // Need at least 4 payload bytes after the header for a stable target.
  const std::size_t min_size = nal.start_code_size + hdr + 4;
  if (nal.bytes.size() < min_size) {
    return false;
  }
  *offset = nal.start_code_size + hdr + 3;  // deterministic 4th payload byte
  return true;
}

}  // namespace videotrust
