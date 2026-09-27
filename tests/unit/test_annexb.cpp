#include "videotrust/annexb.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int fail(const char* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

}  // namespace

int main() {
  using videotrust::AnnexBReader;

  // 4-byte then 3-byte start codes.
  std::vector<uint8_t> buf = {
      0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a,  // SPS-ish
      0x00, 0x00, 0x01, 0x68, 0xce,                    // PPS-ish
      0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84,        // IDR-ish
  };
  auto parsed = AnnexBReader::Parse(buf);
  if (!parsed.ok()) {
    return fail(parsed.error().message.c_str());
  }
  if (parsed.value().size() != 3) {
    return fail("expected 3 NALs");
  }
  if (parsed.value()[0].start_code_size != 4 || parsed.value()[1].start_code_size != 3 ||
      parsed.value()[2].start_code_size != 4) {
    return fail("start code sizes");
  }

  auto empty = AnnexBReader::Parse({});
  if (empty.ok()) {
    return fail("empty should fail");
  }

  std::vector<uint8_t> bad = {0x67, 0x42};
  auto malformed = AnnexBReader::Parse(bad);
  if (malformed.ok()) {
    return fail("missing start code should fail");
  }

  std::cout << "PASS: annexb reader\n";
  return 0;
}
