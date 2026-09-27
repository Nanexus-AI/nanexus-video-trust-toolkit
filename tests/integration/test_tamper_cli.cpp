#include "videotrust/annexb.hpp"
#include "videotrust/nal_classify.hpp"
#include "videotrust/sign.hpp"
#include "videotrust/tamper.hpp"
#include "videotrust/upstream_mapping.hpp"
#include "videotrust/verify.hpp"

#include <onvif_media_signing_validator.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>

namespace fs = std::filesystem;

namespace {

int fail(const std::string& msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

int Run(const std::string& cmd, std::string* out, int* code) {
  std::string full = cmd + " 2>/tmp/vt_tamper_err.txt";
  FILE* p = popen(full.c_str(), "r");
  if (!p) {
    return fail("popen failed");
  }
  std::ostringstream oss;
  char buf[4096];
  while (fgets(buf, sizeof(buf), p)) {
    oss << buf;
  }
  *code = pclose(p);
  if (WIFEXITED(*code)) {
    *code = WEXITSTATUS(*code);
  }
  *out = oss.str();
  return 0;
}

bool IntegrityPositive(const videotrust::VerificationResult& v) {
  using videotrust::SignatureIntegrity;
  return v.signature_integrity == SignatureIntegrity::Ok ||
         v.signature_integrity == SignatureIntegrity::OkWithMissingInfo;
}

videotrust::VerificationResult MustVerify(videotrust::Codec codec,
                                          const std::string& path,
                                          const std::string& ca) {
  videotrust::VerifyOptions opt;
  opt.codec = codec;
  opt.input_path = path;
  opt.ca_pem_path = ca;
  auto r = videotrust::VerifyAnnexBFile(opt);
  if (!r.ok()) {
    throw std::runtime_error(r.error().message);
  }
  return r.value();
}

MediaSigningAuthenticityResult UpstreamAuth(videotrust::Codec codec,
                                            const std::string& path,
                                            const std::string& ca) {
  using namespace videotrust;
  auto session = MediaSigningSession::Create(codec);
  if (!session.ok()) {
    throw std::runtime_error("oracle session");
  }
  if (!ca.empty()) {
    if (SetTrustedCertificateFromFile(session.value(), ca).code != ErrorCode::Ok) {
      throw std::runtime_error("oracle ca");
    }
  }
  auto nalus = AnnexBReader::ParseFile(path);
  if (!nalus.ok()) {
    throw std::runtime_error(nalus.error().message);
  }
  for (const auto& nal : nalus.value()) {
    onvif_media_signing_authenticity_t* report = nullptr;
    const MediaSigningReturnCode rc = onvif_media_signing_add_nalu_and_authenticate(
        session.value().get(), nal.bytes.data(), nal.bytes.size(), &report);
    if (report) {
      onvif_media_signing_authenticity_report_free(report);
    }
    if (rc != OMS_OK) {
      throw std::runtime_error("oracle add_nalu");
    }
  }
  onvif_media_signing_authenticity_t* final_report =
      onvif_media_signing_get_authenticity_report(session.value().get());
  if (!final_report) {
    throw std::runtime_error("oracle null");
  }
  const auto auth = final_report->accumulated_validation.authenticity;
  onvif_media_signing_authenticity_report_free(final_report);
  return auth;
}

bool MatrixForCodec(const std::string& vt, const fs::path& fix, videotrust::Codec codec) {
  using namespace videotrust;
  const std::string cs = codec == Codec::H265 ? "h265" : "h264";
  const fs::path work = fix / cs / "tamper-work";
  fs::create_directories(work);

  const fs::path unsigned_in = fix / cs / ("unsigned." + cs);
  const fs::path key = fix / "pki/signer.key.pem";
  const fs::path cert = fix / "pki/signer-chain.pem";
  const fs::path ca = fix / "pki/ca.pem";
  const fs::path signed_path = work / ("signed." + cs);

  std::string out;
  int code = 0;
  const std::string sign_cmd =
      "\"" + vt + "\" sign --codec " + cs + " --key \"" + key.string() + "\" --cert \"" +
      cert.string() + "\" -o \"" + signed_path.string() + "\" --force --quiet \"" +
      unsigned_in.string() + "\"";
  if (Run(sign_cmd, &out, &code) != 0) {
    return false;
  }
  if (code != 0) {
    fail(cs + " sign failed exit=" + std::to_string(code));
    return false;
  }

  // Baseline VALID
  {
    auto v = MustVerify(codec, signed_path.string(), ca.string());
    if (ExitCodeForVerification(v) != ExitCode::Success) {
      fail(cs + " baseline not success");
      return false;
    }
    if (v.source_authenticity != SourceAuthenticity::NotEstablished) {
      fail(cs + " source overclaim");
      return false;
    }
    const auto up = UpstreamAuth(codec, signed_path.string(), ca.string());
    if (MapAuthenticity(up) != v.signature_integrity) {
      fail(cs + " baseline oracle mismatch");
      return false;
    }
  }

  // corrupt-vcl → INVALID
  {
    const fs::path corrupted = work / ("corrupt." + cs);
    const std::string cmd =
        "\"" + vt + "\" tamper --codec " + cs +
        " --operation corrupt-vcl -o \"" + corrupted.string() + "\" --force --quiet \"" +
        signed_path.string() + "\"";
    if (Run(cmd, &out, &code) != 0 || code != 0) {
      fail(cs + " corrupt tamper exit=" + std::to_string(code));
      return false;
    }
    auto parsed = AnnexBReader::ParseFile(corrupted.string());
    if (!parsed.ok()) {
      fail(cs + " corrupt not parseable");
      return false;
    }
    auto v = MustVerify(codec, corrupted.string(), ca.string());
    if (v.signature_integrity != SignatureIntegrity::NotOk &&
        ExitCodeForVerification(v) != ExitCode::VerificationNegative) {
      fail(cs + " corrupt expected INVALID integrity=" +
           std::string(ToString(v.signature_integrity)));
      return false;
    }
    if (DeriveOverallState(v) != OverallState::Invalid) {
      fail(cs + " corrupt overall=" + std::string(ToString(DeriveOverallState(v))));
      return false;
    }
    const auto up = UpstreamAuth(codec, corrupted.string(), ca.string());
    if (MapAuthenticity(up) != v.signature_integrity) {
      fail(cs + " corrupt oracle mismatch");
      return false;
    }
    if (MapAuthenticity(up) != SignatureIntegrity::NotOk) {
      fail(cs + " corrupt upstream not negative");
      return false;
    }
  }

  // strip-signing-sei
  {
    const fs::path stripped = work / ("stripped." + cs);
    const std::string cmd =
        "\"" + vt + "\" tamper --codec " + cs +
        " --operation strip-signing-sei -o \"" + stripped.string() +
        "\" --force --quiet \"" + signed_path.string() + "\"";
    if (Run(cmd, &out, &code) != 0 || code != 0) {
      fail(cs + " strip tamper exit=" + std::to_string(code));
      return false;
    }
    auto parsed = AnnexBReader::ParseFile(stripped.string());
    if (!parsed.ok()) {
      fail(cs + " strip not parseable");
      return false;
    }
    for (const auto& nal : parsed.value()) {
      if (IsOnvifMediaSigningSei(codec, nal)) {
        fail(cs + " OMS SEI still present after strip");
        return false;
      }
    }
    auto v = MustVerify(codec, stripped.string(), ca.string());
    if (v.source_authenticity != SourceAuthenticity::NotEstablished) {
      fail(cs + " strip source overclaim");
      return false;
    }
    const OverallState overall = DeriveOverallState(v);
    if (overall != OverallState::Unsigned && overall != OverallState::NotVerifiable &&
        overall != OverallState::Partial) {
      fail(cs + " strip unexpected overall=" + std::string(ToString(overall)) +
           " integrity=" + ToString(v.signature_integrity));
      return false;
    }
    if (IntegrityPositive(v)) {
      fail(cs + " strip still positive integrity");
      return false;
    }
    const auto up = UpstreamAuth(codec, stripped.string(), ca.string());
    if (MapAuthenticity(up) != v.signature_integrity) {
      fail(cs + " strip oracle mismatch");
      return false;
    }
  }

  // truncate (NAL-boundary)
  {
    const fs::path trunc = work / ("trunc." + cs);
    const std::string cmd =
        "\"" + vt + "\" tamper --codec " + cs +
        " --operation truncate --count 3 -o \"" + trunc.string() +
        "\" --force --quiet \"" + signed_path.string() + "\"";
    if (Run(cmd, &out, &code) != 0 || code != 0) {
      fail(cs + " truncate tamper exit=" + std::to_string(code));
      return false;
    }
    auto parsed = AnnexBReader::ParseFile(trunc.string());
    if (!parsed.ok() || parsed.value().empty()) {
      fail(cs + " truncate not parseable");
      return false;
    }
    auto v = MustVerify(codec, trunc.string(), ca.string());
    const OverallState overall = DeriveOverallState(v);
    if (overall == OverallState::Valid) {
      fail(cs + " truncate unexpectedly VALID");
      return false;
    }
    if (v.source_authenticity != SourceAuthenticity::NotEstablished) {
      fail(cs + " truncate source overclaim");
      return false;
    }
    const auto up = UpstreamAuth(codec, trunc.string(), ca.string());
    if (MapAuthenticity(up) != v.signature_integrity) {
      fail(cs + " truncate oracle mismatch");
      return false;
    }
    std::cerr << "INFO " << cs << " truncate overall=" << ToString(overall)
              << " integrity=" << ToString(v.signature_integrity)
              << " completeness=" << ToString(v.completeness)
              << " continuity=" << ToString(v.continuity) << "\n";
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    return fail("usage: test_tamper_cli <video-trust> <fixture_dir>");
  }
  const std::string vt = argv[1];
  const fs::path fix = argv[2];

  try {
    if (!MatrixForCodec(vt, fix, videotrust::Codec::H264)) {
      return 1;
    }
    if (!MatrixForCodec(vt, fix, videotrust::Codec::H265)) {
      return 1;
    }
  } catch (const std::exception& ex) {
    return fail(ex.what());
  }

  {
    const fs::path out = fix / "h264/tamper-overwrite.h264";
    std::ofstream(out.string()) << "x";
    std::string sout;
    int code = 0;
    const std::string cmd =
        "\"" + vt + "\" tamper --codec h264 --operation corrupt-vcl -o \"" +
        out.string() + "\" \"" + (fix / "h264/unsigned.h264").string() + "\"";
    if (Run(cmd, &sout, &code) != 0) {
      return 1;
    }
    if (code != 2) {
      return fail("overwrite exit=" + std::to_string(code));
    }
  }

  std::cout << "PASS: tamper integration\n";
  return 0;
}
