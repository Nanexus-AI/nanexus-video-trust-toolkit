#include "videotrust/annexb.hpp"

#include <fstream>
#include <iterator>

namespace videotrust {
namespace {

bool IsStartCode(const uint8_t* p, std::size_t n, std::size_t* sc_size) {
  if (n >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 1) {
    *sc_size = 4;
    return true;
  }
  if (n >= 3 && p[0] == 0 && p[1] == 0 && p[2] == 1) {
    *sc_size = 3;
    return true;
  }
  return false;
}

std::size_t FindNextStartCode(const uint8_t* data, std::size_t size, std::size_t from) {
  for (std::size_t i = from; i + 3 <= size; ++i) {
    std::size_t sc = 0;
    if (IsStartCode(data + i, size - i, &sc)) {
      return i;
    }
  }
  return size;
}

}  // namespace

Expected<std::vector<NalUnit>> AnnexBReader::Parse(std::span<const uint8_t> data) {
  if (data.empty()) {
    return MakeError(ErrorCode::ParseError, "empty Annex-B input");
  }

  std::size_t sc0 = 0;
  if (!IsStartCode(data.data(), data.size(), &sc0)) {
    return MakeError(ErrorCode::ParseError,
                     "Annex-B input does not begin with a start code");
  }

  std::vector<NalUnit> out;
  std::size_t pos = 0;
  while (pos < data.size()) {
    std::size_t sc_size = 0;
    if (!IsStartCode(data.data() + pos, data.size() - pos, &sc_size)) {
      return MakeError(ErrorCode::ParseError, "expected start code between NAL units");
    }
    const std::size_t nal_start = pos;
    const std::size_t next = FindNextStartCode(data.data(), data.size(), pos + sc_size);
    if (next <= pos + sc_size) {
      return MakeError(ErrorCode::ParseError, "zero-length NAL unit");
    }
    NalUnit nal;
    nal.start_code_size = sc_size;
    nal.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(nal_start),
                     data.begin() + static_cast<std::ptrdiff_t>(next));
    out.push_back(std::move(nal));
    pos = next;
  }

  if (out.empty()) {
    return MakeError(ErrorCode::ParseError, "no NAL units found");
  }
  return out;
}

Expected<std::vector<NalUnit>> AnnexBReader::ParseFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return MakeError(ErrorCode::IoFailure, "cannot open input file: " + path);
  }
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
  if (!in.good() && !in.eof()) {
    return MakeError(ErrorCode::IoFailure, "failed reading input file: " + path);
  }
  return Parse(buf);
}

}  // namespace videotrust
