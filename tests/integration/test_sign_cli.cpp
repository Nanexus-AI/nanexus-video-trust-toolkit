#include "videotrust/annexb.hpp"
#include "videotrust/sign.hpp"
#include "videotrust/upstream_mapping.hpp"
#include "videotrust/verify.hpp"

#include <onvif_media_signing_validator.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>

namespace fs = std::filesystem;

namespace {

int fail(const std::string& msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

int Run(const std::string& cmd, std::string* out, int* code) {
  std::string full = cmd + " 2>/tmp/vt_sign_err.txt";
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

bool RoundTrip(const std::string& vt,
               const fs::path& fix,
               videotrust::Codec codec,
               const std::string& name) {
  using namespace videotrust;
  const std::string codec_s = codec == Codec::H265 ? "h265" : "h264";
  const std::string ext = codec == Codec::H265 ? "h265" : "h264";
  const fs::path unsigned_path = fix / codec_s / ("unsigned." + ext);
  const fs::path signed_path = fix / codec_s / ("nanexus-signed." + ext);
  const fs::path ca = fix / "pki" / "ca.pem";
  const fs::path key = fix / "pki" / "signer.key.pem";
  // Chain includes leaf + CA (required by upstream; anchor stripped from SEI).
  const fs::path cert = fix / "pki" / "signer-chain.pem";

  fs::remove(signed_path);

  std::string out;
  int code = 0;
  const std::string sign_cmd =
      "\"" + vt + "\" sign --codec " + codec_s + " --key \"" + key.string() +
      "\" --cert \"" + cert.string() + "\" -o \"" + signed_path.string() +
      "\" --force \"" + unsigned_path.string() + "\"";
  if (Run(sign_cmd, &out, &code) != 0) {
    return false;
  }
  if (code != 0) {
    fail(name + " sign exit=" + std::to_string(code));
    return false;
  }
  if (out.find("Signed media written successfully") == std::string::npos) {
    fail(name + " missing success text");
    return false;
  }

  // Output must remain Annex-B parseable (EPB regression guard).
  auto nalus = AnnexBReader::ParseFile(signed_path.string());
  if (!nalus.ok() || nalus.value().empty()) {
    fail(name + " signed output not Annex-B parseable");
    return false;
  }
  // No H.264 NAL type 0 (unspecified) which previously indicated false start-code splits.
  if (codec == Codec::H264) {
    for (const auto& nal : nalus.value()) {
      if (nal.bytes.size() <= nal.start_code_size) {
        continue;
      }
      const uint8_t nt = nal.bytes[nal.start_code_size] & 0x1f;
      if (nt == 0) {
        fail(name + " unexpected H.264 NAL type 0 (possible EPB regression)");
        return false;
      }
    }
  }

  // Verify without CA: integrity may still be ok; source not established; cert not_provided
  {
    VerifyOptions opt;
    opt.codec = codec;
    opt.input_path = signed_path.string();
    auto r = VerifyAnnexBFile(opt);
    if (!r.ok()) {
      fail(name + " verify no-ca: " + r.error().message);
      return false;
    }
    if (r.value().source_authenticity != SourceAuthenticity::NotEstablished) {
      fail(name + " source authenticity must stay not_established");
      return false;
    }
    if (r.value().certificate != CertificateStatus::NotProvided) {
      fail(name + " no-ca certificate should be not_provided");
      return false;
    }
    if (r.value().signature_integrity != SignatureIntegrity::Ok &&
        r.value().signature_integrity != SignatureIntegrity::OkWithMissingInfo) {
      fail(name + " no-ca integrity=" + std::string(ToString(r.value().signature_integrity)));
      return false;
    }
  }

  // Verify with CA → VALID semantics
  {
    VerifyOptions opt;
    opt.codec = codec;
    opt.input_path = signed_path.string();
    opt.ca_pem_path = ca.string();
    auto r = VerifyAnnexBFile(opt);
    if (!r.ok()) {
      fail(name + " verify+ca: " + r.error().message);
      return false;
    }
    if (ExitCodeForVerification(r.value()) != ExitCode::Success) {
      fail(name + " verify+ca exit not 0");
      return false;
    }
    if (DeriveOverallState(r.value()) != OverallState::Valid &&
        DeriveOverallState(r.value()) != OverallState::Partial) {
      fail(name + " overall not VALID/PARTIAL");
      return false;
    }
    if (r.value().source_authenticity != SourceAuthenticity::NotEstablished) {
      fail(name + " source authenticity overclaim");
      return false;
    }
    if (r.value().certificate != CertificateStatus::Ok) {
      fail(name + " certificate should be ok with matching CA");
      return false;
    }

    // Oracle: direct upstream authenticity agrees with Nanexus mapping
    auto session = MediaSigningSession::Create(codec);
    if (!session.ok()) {
      fail(name + " oracle session");
      return false;
    }
    if (SetTrustedCertificateFromFile(session.value(), ca.string()).code != ErrorCode::Ok) {
      fail(name + " oracle ca");
      return false;
    }
    for (const auto& nal : nalus.value()) {
      onvif_media_signing_authenticity_t* report = nullptr;
      const MediaSigningReturnCode rc = onvif_media_signing_add_nalu_and_authenticate(
          session.value().get(), nal.bytes.data(), nal.bytes.size(), &report);
      if (report) {
        onvif_media_signing_authenticity_report_free(report);
      }
      if (rc != OMS_OK) {
        fail(name + " oracle add_nalu");
        return false;
      }
    }
    onvif_media_signing_authenticity_t* final_report =
        onvif_media_signing_get_authenticity_report(session.value().get());
    if (!final_report) {
      fail(name + " oracle null report");
      return false;
    }
    const auto up_auth = final_report->accumulated_validation.authenticity;
    onvif_media_signing_authenticity_report_free(final_report);
    if (up_auth != OMS_AUTHENTICITY_OK &&
        up_auth != OMS_AUTHENTICITY_OK_WITH_MISSING_INFO) {
      fail(name + " oracle upstream authenticity not positive");
      return false;
    }
    if (MapAuthenticity(up_auth) != r.value().signature_integrity) {
      fail(name + " oracle authenticity mismatch");
      return false;
    }
  }

  // CLI verify exit 0
  {
    std::string vout;
    int vcode = 0;
    const std::string vcmd = "\"" + vt + "\" verify --codec " + codec_s + " --ca \"" +
                             ca.string() + "\" \"" + signed_path.string() + "\"";
    if (Run(vcmd, &vout, &vcode) != 0) {
      return false;
    }
    if (vcode != 0) {
      fail(name + " cli verify exit=" + std::to_string(vcode));
      return false;
    }
    if (vout.find("not_established") == std::string::npos &&
        vout.find("not established") == std::string::npos) {
      // RenderText uses ToString → not_established
      fail(name + " cli verify missing not_established");
      return false;
    }
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    return fail("usage: test_sign_cli <video-trust> <fixture_dir>");
  }
  const std::string vt = argv[1];
  const fs::path fix = argv[2];

  using namespace videotrust;

  if (!RoundTrip(vt, fix, Codec::H264, "h264")) {
    return 1;
  }
  if (!RoundTrip(vt, fix, Codec::H265, "h265")) {
    return 1;
  }

  // Existing output without --force → exit 2
  {
    const fs::path out = fix / "h264" / "block-overwrite.h264";
    std::ofstream(out.string()) << "x";
    std::string sout;
    int code = 0;
    const std::string cmd =
        "\"" + vt + "\" sign --codec h264 --key \"" + (fix / "pki/signer.key.pem").string() +
        "\" --cert \"" + (fix / "pki/signer-chain.pem").string() + "\" -o \"" +
        out.string() + "\" \"" + (fix / "h264/unsigned.h264").string() + "\"";
    if (Run(cmd, &sout, &code) != 0) {
      return 1;
    }
    if (code != 2) {
      return fail("overwrite without --force exit=" + std::to_string(code));
    }
  }

  // Missing key → exit 2
  {
    std::string sout;
    int code = 0;
    const std::string cmd =
        "\"" + vt + "\" sign --codec h264 --key \"" + (fix / "pki/no-key.pem").string() +
        "\" --cert \"" + (fix / "pki/signer-chain.pem").string() +
        "\" -o /tmp/vt-sign-miss-key.h264 --force \"" +
        (fix / "h264/unsigned.h264").string() + "\"";
    if (Run(cmd, &sout, &code) != 0) {
      return 1;
    }
    if (code != 2) {
      return fail("missing key exit=" + std::to_string(code));
    }
  }

  // Invalid key material → exit 2 or 3 (upstream reject)
  {
    const fs::path bad_key = fix / "pki" / "bad-key.pem";
    std::ofstream(bad_key) << "-----BEGIN PRIVATE KEY-----\nnot-valid\n-----END PRIVATE KEY-----\n";
    std::string sout;
    int code = 0;
    const std::string cmd =
        "\"" + vt + "\" sign --codec h264 --key \"" + bad_key.string() + "\" --cert \"" +
        (fix / "pki/signer-chain.pem").string() +
        "\" -o /tmp/vt-sign-bad-key.h264 --force \"" +
        (fix / "h264/unsigned.h264").string() + "\"";
    if (Run(cmd, &sout, &code) != 0) {
      return 1;
    }
    if (code != 2 && code != 3) {
      return fail("invalid key exit=" + std::to_string(code));
    }
  }

  // Missing cert → exit 2
  {
    std::string sout;
    int code = 0;
    const std::string cmd =
        "\"" + vt + "\" sign --codec h264 --key \"" +
        (fix / "pki/signer.key.pem").string() +
        "\" --cert \"" + (fix / "pki/no-cert.pem").string() +
        "\" -o /tmp/vt-sign-miss-cert.h264 --force \"" +
        (fix / "h264/unsigned.h264").string() + "\"";
    if (Run(cmd, &sout, &code) != 0) {
      return 1;
    }
    if (code != 2) {
      return fail("missing cert exit=" + std::to_string(code));
    }
  }

  // Malformed input → exit 2
  {
    const fs::path bad = fix / "h264" / "garbage.es";
    std::ofstream(bad) << "garbage-not-annexb";
    std::string sout;
    int code = 0;
    const std::string cmd =
        "\"" + vt + "\" sign --codec h264 --key \"" +
        (fix / "pki/signer.key.pem").string() + "\" --cert \"" +
        (fix / "pki/signer-chain.pem").string() +
        "\" -o /tmp/vt-sign-garbage.h264 --force \"" + bad.string() + "\"";
    if (Run(cmd, &sout, &code) != 0) {
      return 1;
    }
    if (code != 2) {
      return fail("malformed input exit=" + std::to_string(code));
    }
  }

  std::cout << "PASS: sign integration\n";
  return 0;
}
