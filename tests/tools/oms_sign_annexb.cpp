// Test-only helper: sign Annex-B with the official ONVIF C API (not product CLI).
#include "videotrust/annexb.hpp"
#include "videotrust/session.hpp"

#include <onvif_media_signing_signer.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot read " + path);
  }
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "Usage: oms_sign_annexb h264|h265 key.pem cert-chain.pem in.es out.es\n";
    return 2;
  }
  try {
    const videotrust::Codec codec =
        std::string(argv[1]) == "h265" ? videotrust::Codec::H265 : videotrust::Codec::H264;
    const std::string key = ReadAll(argv[2]);
    const std::string chain = ReadAll(argv[3]);
    auto nalus = videotrust::AnnexBReader::ParseFile(argv[4]);
    if (!nalus.ok()) {
      std::cerr << nalus.error().message << "\n";
      return 2;
    }
    auto session = videotrust::MediaSigningSession::Create(codec);
    if (!session.ok()) {
      std::cerr << session.error().message << "\n";
      return 3;
    }
    if (onvif_media_signing_set_signing_key_pair(session.value().get(), key.data(), key.size(),
                                                 chain.data(), chain.size(),
                                                 false) != OMS_OK) {
      std::cerr << "set_signing_key_pair failed\n";
      return 3;
    }
    // Default sei_epb is false in upstream; Annex-B byte-stream parsers require EPB so
    // signature/TLV payloads do not introduce false start codes.
    if (onvif_media_signing_set_emulation_prevention_before_signing(session.value().get(),
                                                                     true) != OMS_OK) {
      std::cerr << "set_emulation_prevention_before_signing failed\n";
      return 3;
    }

    std::ofstream out(argv[5], std::ios::binary);
    if (!out) {
      std::cerr << "cannot write output\n";
      return 2;
    }

    // 100-ns intervals since 1601-01-01; step ~10ms per NAL for determinism.
    int64_t ts = 132537600000000000LL;  // approx 2022-01-01 UTC in FILETIME-ish scale
    for (const auto& nal : nalus.value()) {
      for (;;) {
        uint8_t* sei = nullptr;
        size_t sei_size = 0;
        const MediaSigningReturnCode grc = onvif_media_signing_get_sei(
            session.value().get(), &sei, &sei_size, nullptr, nal.bytes.data(), nal.bytes.size(),
            nullptr);
        if (grc != OMS_OK) {
          std::cerr << "get_sei failed\n";
          return 3;
        }
        if (sei_size == 0) {
          break;
        }
        out.write(reinterpret_cast<const char*>(sei), static_cast<std::streamsize>(sei_size));
        if (onvif_media_signing_add_nalu_for_signing(session.value().get(), sei, sei_size, ts) !=
            OMS_OK) {
          free(sei);
          std::cerr << "add_nalu_for_signing(sei) failed\n";
          return 3;
        }
        free(sei);
      }

      if (onvif_media_signing_add_nalu_for_signing(session.value().get(), nal.bytes.data(),
                                                   nal.bytes.size(), ts) != OMS_OK) {
        std::cerr << "add_nalu_for_signing(nal) failed\n";
        return 3;
      }
      out.write(reinterpret_cast<const char*>(nal.bytes.data()),
                static_cast<std::streamsize>(nal.bytes.size()));
      ts += 100000;  // +10ms in 100-ns units
    }

    if (onvif_media_signing_set_end_of_stream(session.value().get()) != OMS_OK) {
      std::cerr << "set_end_of_stream failed\n";
      return 3;
    }
    // Drain trailing SEIs (no peek NAL).
    for (;;) {
      uint8_t* sei = nullptr;
      size_t sei_size = 0;
      const MediaSigningReturnCode grc = onvif_media_signing_get_sei(
          session.value().get(), &sei, &sei_size, nullptr, nullptr, 0, nullptr);
      if (grc != OMS_OK || sei_size == 0) {
        break;
      }
      out.write(reinterpret_cast<const char*>(sei), static_cast<std::streamsize>(sei_size));
      free(sei);
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << "\n";
    return 3;
  }
  return 0;
}
