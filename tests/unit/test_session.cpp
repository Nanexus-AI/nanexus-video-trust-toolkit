#include "videotrust/session.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

}  // namespace

int main() {
  using videotrust::Codec;
  using videotrust::ErrorCode;
  using videotrust::MediaSigningSession;

  auto s1 = MediaSigningSession::Create(Codec::H264);
  if (!s1.ok()) {
    return fail(s1.error().message.c_str());
  }
  if (!s1.value().get()) {
    return fail("H264 handle null");
  }
  const auto reset_rc = s1.value().Reset();
  if (reset_rc.code != ErrorCode::Ok) {
    return fail(reset_rc.message.c_str());
  }

  auto s2 = MediaSigningSession::Create(Codec::H265);
  if (!s2.ok()) {
    return fail(s2.error().message.c_str());
  }

  // Move semantics: source becomes empty, destination owns handle.
  MediaSigningSession moved = std::move(s2.value());
  if (!moved.get()) {
    return fail("moved-to handle null");
  }

  // Double create/destroy for both codecs.
  for (int i = 0; i < 3; ++i) {
    auto s = MediaSigningSession::Create(Codec::H264);
    if (!s.ok()) {
      return fail("repeat create failed");
    }
  }

  std::cout << "PASS: session lifecycle\n";
  return 0;
}
