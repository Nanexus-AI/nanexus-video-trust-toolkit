#include "videotrust/annexb.hpp"
#include "videotrust/sign.hpp"
#include "videotrust/verify.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int fail(const std::string& msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

fs::path TmpDir() {
  const char* t = std::getenv("MESON_TEST_DIR");
  fs::path base = t ? fs::path(t) : fs::temp_directory_path();
  fs::path dir = base / "sign_unit";
  fs::create_directories(dir);
  return dir;
}

void WriteFile(const fs::path& p, const std::string& data) {
  std::ofstream out(p, std::ios::binary);
  out << data;
}

}  // namespace

int main() {
  using namespace videotrust;
  const fs::path dir = TmpDir();

  // Option validation: missing paths
  {
    SignOptions opt;
    Error e = SignAnnexBFile(opt);
    if (e.code == ErrorCode::Ok || ExitCodeForSign(e) != ExitCode::UsageOrInputError) {
      return fail("empty options should be usage error");
    }
  }

  // Overwrite policy without --force
  {
    const fs::path existing = dir / "exists.es";
    WriteFile(existing, std::string("\x00\x00\x00\x01\x67", 5));
    SignOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (dir / "missing-in.es").string();
    opt.output_path = existing.string();
    opt.key_pem_path = (dir / "k.pem").string();
    opt.cert_pem_path = (dir / "c.pem").string();
    opt.force = false;
    WriteFile(dir / "k.pem", "not-a-key");
    WriteFile(dir / "c.pem", "not-a-cert");
    Error e = SignAnnexBFile(opt);
    if (e.code == ErrorCode::Ok) {
      return fail("overwrite without force must fail");
    }
    if (e.message.find("--force") == std::string::npos) {
      return fail("overwrite error should mention --force");
    }
    if (ExitCodeForSign(e) != ExitCode::UsageOrInputError) {
      return fail("overwrite → exit 2");
    }
  }

  // Missing key file
  {
    SignOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (dir / "in.es").string();
    opt.output_path = (dir / "out-missing-key.es").string();
    opt.key_pem_path = (dir / "no-such-key.pem").string();
    opt.cert_pem_path = (dir / "c.pem").string();
    opt.force = true;
    WriteFile(dir / "in.es", std::string("\x00\x00\x00\x01\x67\x42", 6));
    WriteFile(dir / "c.pem", "x");
    Error e = SignAnnexBFile(opt);
    if (e.code == ErrorCode::Ok || ExitCodeForSign(e) != ExitCode::UsageOrInputError) {
      return fail("missing key → exit 2");
    }
  }

  // Malformed Annex-B
  {
    SignOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (dir / "bad.es").string();
    opt.output_path = (dir / "out-bad.es").string();
    opt.key_pem_path = (dir / "k.pem").string();
    opt.cert_pem_path = (dir / "c.pem").string();
    opt.force = true;
    WriteFile(dir / "bad.es", "not-annex-b");
    WriteFile(dir / "k.pem", "-----BEGIN PRIVATE KEY-----\nAA==\n-----END PRIVATE KEY-----\n");
    WriteFile(dir / "c.pem", "-----BEGIN CERTIFICATE-----\nAA==\n-----END CERTIFICATE-----\n");
    Error e = SignAnnexBFile(opt);
    if (e.code == ErrorCode::Ok) {
      return fail("malformed annex-b must fail");
    }
    if (ExitCodeForSign(e) != ExitCode::UsageOrInputError &&
        ExitCodeForSign(e) != ExitCode::RuntimeFailure) {
      // ParseError → 2; bad key may hit upstream → 3 after parse succeeds.
      // Here input has no start codes → ParseError → 2.
      return fail(std::string("malformed exit unexpected: ") + e.message);
    }
    if (e.code == ErrorCode::ParseError &&
        ExitCodeForSign(e) != ExitCode::UsageOrInputError) {
      return fail("parse error must map to exit 2");
    }
  }

  // ExitCodeForSign mapping
  {
    if (ExitCodeForSign(MakeError(ErrorCode::Ok, {})) != ExitCode::Success) {
      return fail("ok → 0");
    }
    if (ExitCodeForSign(MakeError(ErrorCode::InvalidArgument, "x")) !=
        ExitCode::UsageOrInputError) {
      return fail("invalid → 2");
    }
    if (ExitCodeForSign(MakeError(ErrorCode::UpstreamFailure, "x")) !=
        ExitCode::RuntimeFailure) {
      return fail("upstream → 3");
    }
  }

  std::cout << "PASS: sign unit\n";
  return 0;
}
