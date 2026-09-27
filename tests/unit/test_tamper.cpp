#include "videotrust/annexb.hpp"
#include "videotrust/nal_classify.hpp"
#include "videotrust/tamper.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int fail(const std::string& msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

void WriteBytes(const fs::path& p, const std::vector<uint8_t>& b) {
  std::ofstream out(p, std::ios::binary);
  out.write(reinterpret_cast<const char*>(b.data()),
            static_cast<std::streamsize>(b.size()));
}

videotrust::NalUnit MakeNal(std::size_t sc, const std::vector<uint8_t>& body) {
  videotrust::NalUnit n;
  n.start_code_size = sc;
  if (sc == 4) {
    n.bytes = {0, 0, 0, 1};
  } else {
    n.bytes = {0, 0, 1};
  }
  n.bytes.insert(n.bytes.end(), body.begin(), body.end());
  return n;
}

}  // namespace

int main() {
  using namespace videotrust;
  const fs::path dir = fs::temp_directory_path() / "vt-tamper-unit";
  fs::create_directories(dir);

  // H.264 VCL / SEI classification
  {
    auto idr = MakeNal(4, {0x65, 0x88, 0x84, 0x00, 0x10});  // type 5
    auto p = MakeNal(4, {0x41, 0x9a, 0x00, 0x10, 0x20});    // type 1
    auto sei = MakeNal(4, {0x06, 0x05, 0x10});
    auto sps = MakeNal(4, {0x67, 0x42});
    if (!IsVclNal(Codec::H264, idr) || !IsVclNal(Codec::H264, p)) {
      return fail("h264 vcl");
    }
    if (!IsSeiNal(Codec::H264, sei) || IsVclNal(Codec::H264, sei)) {
      return fail("h264 sei");
    }
    if (ClassifyNal(Codec::H264, sps) != NalKind::ParameterSet) {
      return fail("h264 sps");
    }
  }

  // H.265 VCL / SEI
  {
    // IDR_W_RADL type 19: nal_unit_type in bits [6:1] → header0 = (19<<1)=38=0x26
    auto idr = MakeNal(4, {0x26, 0x01, 0xaf, 0x00, 0x10, 0x20});
    // TRAIL_R type 1 → header0 = 0x02
    auto trail = MakeNal(4, {0x02, 0x01, 0x00, 0x10, 0x20, 0x30});
    // PREFIX_SEI type 39 → header0 = 0x4e
    auto sei = MakeNal(4, {0x4e, 0x01, 0x05, 0x10});
    if (!IsVclNal(Codec::H265, idr) || !IsVclNal(Codec::H265, trail)) {
      return fail("h265 vcl");
    }
    if (!IsSeiNal(Codec::H265, sei)) {
      return fail("h265 sei");
    }
  }

  // Signing-SEI UUID identification vs unrelated SEI
  {
    std::vector<uint8_t> oms_body = {0x06, 0x05, 0x10};
    oms_body.insert(oms_body.end(), kOnvifMediaSigningUuid,
                    kOnvifMediaSigningUuid + kOnvifMediaSigningUuidLen);
    oms_body.push_back(0x80);  // stop-ish
    auto oms = MakeNal(4, oms_body);

    std::vector<uint8_t> other_uuid(16, 0xaa);
    std::vector<uint8_t> other_body = {0x06, 0x05, 0x10};
    other_body.insert(other_body.end(), other_uuid.begin(), other_uuid.end());
    auto other = MakeNal(4, other_body);

    if (!IsOnvifMediaSigningSei(Codec::H264, oms)) {
      return fail("oms sei id");
    }
    if (IsOnvifMediaSigningSei(Codec::H264, other)) {
      return fail("unrelated sei must not match");
    }
  }

  // corrupt-vcl preserves framing / length
  {
    std::vector<uint8_t> stream;
    auto sps = MakeNal(4, {0x67, 0x42, 0x00, 0x0a});
    auto idr = MakeNal(4, {0x65, 0x88, 0x84, 0x00, 0x11, 0x22, 0x33});
    for (const auto& n : {sps, idr}) {
      stream.insert(stream.end(), n.bytes.begin(), n.bytes.end());
    }
    const fs::path in = dir / "tiny.h264";
    const fs::path out = dir / "corrupt.h264";
    WriteBytes(in, stream);
    fs::remove(out);

    TamperOptions opt;
    opt.codec = Codec::H264;
    opt.operation = TamperOperation::CorruptVcl;
    opt.input_path = in.string();
    opt.output_path = out.string();
    opt.force = true;
    Error e = TamperAnnexBFile(opt);
    if (e.code != ErrorCode::Ok) {
      return fail(std::string("corrupt: ") + e.message);
    }
    auto before = AnnexBReader::ParseFile(in.string());
    auto after = AnnexBReader::ParseFile(out.string());
    if (!before.ok() || !after.ok() || before.value().size() != after.value().size()) {
      return fail("corrupt parse/count");
    }
    if (before.value()[1].bytes.size() != after.value()[1].bytes.size()) {
      return fail("corrupt length change");
    }
    if (before.value()[1].bytes == after.value()[1].bytes) {
      return fail("corrupt did not mutate");
    }
    if (before.value()[1].bytes[0] != after.value()[1].bytes[0]) {
      return fail("start code mutated");
    }
  }

  // strip preserves unrelated SEI, removes OMS SEI
  {
    std::vector<uint8_t> oms_body = {0x06, 0x05, 0x10};
    oms_body.insert(oms_body.end(), kOnvifMediaSigningUuid,
                    kOnvifMediaSigningUuid + kOnvifMediaSigningUuidLen);
    auto oms = MakeNal(4, oms_body);
    std::vector<uint8_t> other_body = {0x06, 0x05, 0x08, 1, 2, 3, 4, 5, 6, 7, 8};
    auto other = MakeNal(4, other_body);
    auto idr = MakeNal(4, {0x65, 0x88, 0x84, 0x00, 0x11});
    std::vector<uint8_t> stream;
    for (const auto& n : {other, oms, idr}) {
      stream.insert(stream.end(), n.bytes.begin(), n.bytes.end());
    }
    const fs::path in = dir / "strip-in.h264";
    const fs::path out = dir / "strip-out.h264";
    WriteBytes(in, stream);
    fs::remove(out);
    TamperOptions opt;
    opt.codec = Codec::H264;
    opt.operation = TamperOperation::StripSigningSei;
    opt.input_path = in.string();
    opt.output_path = out.string();
    opt.force = true;
    Error e = TamperAnnexBFile(opt);
    if (e.code != ErrorCode::Ok) {
      return fail(std::string("strip: ") + e.message);
    }
    auto after = AnnexBReader::ParseFile(out.string());
    if (!after.ok() || after.value().size() != 2) {
      return fail("strip count");
    }
    if (IsOnvifMediaSigningSei(Codec::H264, after.value()[0])) {
      return fail("oms sei still present");
    }
    if (!IsSeiNal(Codec::H264, after.value()[0])) {
      return fail("unrelated sei removed");
    }
  }

  // truncate removes trailing NALs
  {
    std::vector<uint8_t> stream;
    for (int i = 0; i < 5; ++i) {
      auto n = MakeNal(4, {0x41, 0x9a, 0x00, static_cast<uint8_t>(i), 0x10, 0x20});
      stream.insert(stream.end(), n.bytes.begin(), n.bytes.end());
    }
    const fs::path in = dir / "trunc-in.h264";
    const fs::path out = dir / "trunc-out.h264";
    WriteBytes(in, stream);
    fs::remove(out);
    TamperOptions opt;
    opt.codec = Codec::H264;
    opt.operation = TamperOperation::Truncate;
    opt.truncate_nal_count = 2;
    opt.input_path = in.string();
    opt.output_path = out.string();
    opt.force = true;
    Error e = TamperAnnexBFile(opt);
    if (e.code != ErrorCode::Ok) {
      return fail(std::string("trunc: ") + e.message);
    }
    auto after = AnnexBReader::ParseFile(out.string());
    if (!after.ok() || after.value().size() != 3) {
      return fail("trunc count");
    }
  }

  // no suitable target / overwrite / exit codes
  {
    auto sps_only = MakeNal(4, {0x67, 0x42, 0x00});
    const fs::path in = dir / "no-vcl.h264";
    WriteBytes(in, sps_only.bytes);
    TamperOptions opt;
    opt.codec = Codec::H264;
    opt.operation = TamperOperation::CorruptVcl;
    opt.input_path = in.string();
    opt.output_path = (dir / "no-vcl-out.h264").string();
    opt.force = true;
    Error e = TamperAnnexBFile(opt);
    if (ExitCodeForTamper(e) != ExitCode::UsageOrInputError) {
      return fail("no vcl → exit 2");
    }

    const fs::path exists = dir / "exists.es";
    WriteBytes(exists, {1});
    opt.output_path = exists.string();
    opt.force = false;
    opt.input_path = in.string();
    e = TamperAnnexBFile(opt);
    if (ExitCodeForTamper(e) != ExitCode::UsageOrInputError) {
      return fail("overwrite → exit 2");
    }

    if (ExitCodeForTamper(MakeError(ErrorCode::Ok, {})) != ExitCode::Success) {
      return fail("ok→0");
    }
    if (ExitCodeForTamper(MakeError(ErrorCode::InternalError, "x")) !=
        ExitCode::RuntimeFailure) {
      return fail("internal→3");
    }
  }

  std::cout << "PASS: tamper unit\n";
  return 0;
}
