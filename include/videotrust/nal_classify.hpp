#pragma once

#include "videotrust/annexb.hpp"
#include "videotrust/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace videotrust {

/// ONVIF Media Signing user-data-unregistered UUID (oms_common.c kUuidMediaSigning).
inline constexpr std::size_t kOnvifMediaSigningUuidLen = 16;
inline constexpr uint8_t kOnvifMediaSigningUuid[kOnvifMediaSigningUuidLen] = {
    0x00, 0x5b, 0xc9, 0x3f, 0x2d, 0x71, 0x5e, 0x95,
    0xad, 0xa4, 0x79, 0x6f, 0x90, 0x87, 0x7a, 0x6f};

enum class NalKind {
  Other,
  Vcl,
  Sei,
  ParameterSet,
};

/// Codec NAL header length after the start code (H.264: 1, H.265: 2).
std::size_t NalHeaderLength(Codec codec) noexcept;

/// Raw codec NAL type bits (H.264: 5-bit; H.265: 6-bit).
int RawNalType(Codec codec, const NalUnit& nal) noexcept;

NalKind ClassifyNal(Codec codec, const NalUnit& nal) noexcept;

bool IsVclNal(Codec codec, const NalUnit& nal) noexcept;
bool IsSeiNal(Codec codec, const NalUnit& nal) noexcept;

/// True iff this is an ONVIF Media Signing SEI (payload type 5 + kUuidMediaSigning).
/// Unrelated user-data SEIs (e.g. encoder info) return false.
bool IsOnvifMediaSigningSei(Codec codec, const NalUnit& nal) noexcept;

/// Index of payload byte suitable for corrupt-vcl (after SC + NAL header).
/// Returns false if the NAL is too short to mutate safely.
bool VclMutationOffset(Codec codec, const NalUnit& nal, std::size_t* offset) noexcept;

}  // namespace videotrust
