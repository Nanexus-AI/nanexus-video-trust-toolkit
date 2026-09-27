#include "videotrust/annexb.hpp"
#include "videotrust/error.hpp"
#include "videotrust/session.hpp"
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
  std::string full = cmd + " 2>/tmp/vt_err.txt";
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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    return fail("usage: test_verify_cli <video-trust> <fixture_dir>");
  }
  const std::string vt = argv[1];
  const fs::path fix = argv[2];

  using namespace videotrust;

  // Library path: unsigned
  {
    VerifyOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (fix / "h264/unsigned.h264").string();
    auto r = VerifyAnnexBFile(opt);
    if (!r.ok()) {
      return fail(r.error().message);
    }
    if (r.value().media_signing != SigningPresence::NotDetected ||
        ExitCodeForVerification(r.value()) != ExitCode::UnsignedOrNotVerifiable) {
      return fail("unsigned h264 classification");
    }
  }

  // Signed H.264 with CA via library + oracle (direct upstream report mapping agreement
  // is implicit: VerifyAnnexBFile uses Official API then MapFromUpstream).
  {
    VerifyOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (fix / "h264/signed.h264").string();
    opt.ca_pem_path = (fix / "pki/ca.pem").string();
    auto r = VerifyAnnexBFile(opt);
    if (!r.ok()) {
      return fail("signed h264: " + r.error().message);
    }
    if (r.value().signature_integrity != SignatureIntegrity::Ok &&
        r.value().signature_integrity != SignatureIntegrity::OkWithMissingInfo) {
      return fail(std::string("signed h264 integrity=") +
                  ToString(r.value().signature_integrity));
    }
    if (r.value().certificate != CertificateStatus::Ok) {
      return fail("signed h264 certificate");
    }
    if (r.value().source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("source authenticity must remain not_established");
    }
    if (ExitCodeForVerification(r.value()) != ExitCode::Success) {
      return fail("signed h264 exit");
    }
  }

  // Signed H.265
  {
    VerifyOptions opt;
    opt.codec = Codec::H265;
    opt.input_path = (fix / "h265/signed.h265").string();
    opt.ca_pem_path = (fix / "pki/ca.pem").string();
    auto r = VerifyAnnexBFile(opt);
    if (!r.ok()) {
      return fail("signed h265: " + r.error().message);
    }
    if (r.value().signature_integrity != SignatureIntegrity::Ok &&
        r.value().signature_integrity != SignatureIntegrity::OkWithMissingInfo) {
      return fail(std::string("signed h265 integrity=") +
                  ToString(r.value().signature_integrity));
    }
  }

  // Tampered → negative
  {
    VerifyOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (fix / "h264/tampered.h264").string();
    opt.ca_pem_path = (fix / "pki/ca.pem").string();
    auto r = VerifyAnnexBFile(opt);
    if (!r.ok()) {
      return fail("tampered: " + r.error().message);
    }
    if (r.value().signature_integrity != SignatureIntegrity::NotOk &&
        ExitCodeForVerification(r.value()) != ExitCode::VerificationNegative) {
      // Accept NotOk integrity OR exit 1
      if (ExitCodeForVerification(r.value()) != ExitCode::VerificationNegative &&
          r.value().signature_integrity != SignatureIntegrity::NotOk) {
        return fail(std::string("tampered unexpected ") +
                    ToString(r.value().signature_integrity));
      }
    }
  }

  // Malformed CLI input
  {
    std::string out;
    int code = 0;
    const std::string cmd = "\"" + vt + "\" verify --codec h264 /tmp/does-not-exist-vt-xyz";
    if (Run(cmd, &out, &code) != 0) {
      return 1;
    }
    if (code != 2 && code != 3) {
      // Io missing file → 2
      return fail("missing file exit code=" + std::to_string(code));
    }
  }

  // JSON stdout only
  {
    std::string out;
    int code = 0;
    const std::string cmd = "\"" + vt + "\" verify --codec h264 --json \"" +
                            (fix / "h264/unsigned.h264").string() + "\"";
    if (Run(cmd, &out, &code) != 0) {
      return 1;
    }
    if (out.empty() || out.front() != '{') {
      return fail("json stdout");
    }
    if (out.find("schema_version") == std::string::npos) {
      return fail("json schema field");
    }
    if (code != 4) {
      return fail("unsigned json exit");
    }
  }

  // Oracle: Nanexus axes must agree with a second direct upstream C API session
  // (semantic authenticity/provenance enums — not GStreamer appsink text output).
  // Official GStreamer validator/signer apps reject raw Annex-B ("raw recordings").
  {
    VerifyOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (fix / "h264/signed.h264").string();
    opt.ca_pem_path = (fix / "pki/ca.pem").string();
    auto nanexus = VerifyAnnexBFile(opt);
    if (!nanexus.ok()) {
      return fail("oracle nanexus: " + nanexus.error().message);
    }

    auto session = MediaSigningSession::Create(Codec::H264);
    if (!session.ok()) {
      return fail("oracle session");
    }
    const Error ca_err =
        SetTrustedCertificateFromFile(session.value(), opt.ca_pem_path);
    if (ca_err.code != ErrorCode::Ok) {
      return fail("oracle ca");
    }
    auto nalus = AnnexBReader::ParseFile(opt.input_path);
    if (!nalus.ok()) {
      return fail("oracle parse");
    }
    for (const auto& nal : nalus.value()) {
      onvif_media_signing_authenticity_t* report = nullptr;
      const MediaSigningReturnCode rc = onvif_media_signing_add_nalu_and_authenticate(
          session.value().get(), nal.bytes.data(), nal.bytes.size(), &report);
      if (report != nullptr) {
        onvif_media_signing_authenticity_report_free(report);
      }
      if (rc != OMS_OK) {
        return fail("oracle add_nalu");
      }
    }
    onvif_media_signing_authenticity_t* final_report =
        onvif_media_signing_get_authenticity_report(session.value().get());
    if (final_report == nullptr) {
      return fail("oracle null report");
    }
    const auto mapped =
        MapFromUpstream(Codec::H264, *final_report, true);
    const auto up_auth = final_report->accumulated_validation.authenticity;
    const auto up_prov = final_report->accumulated_validation.provenance;
    onvif_media_signing_authenticity_report_free(final_report);

    if (MapAuthenticity(up_auth) != nanexus.value().signature_integrity) {
      return fail("oracle authenticity mismatch vs Nanexus");
    }
    if (MapProvenance(up_prov, true) != nanexus.value().certificate) {
      return fail("oracle provenance mismatch vs Nanexus");
    }
    if (mapped.signature_integrity != nanexus.value().signature_integrity ||
        mapped.certificate != nanexus.value().certificate) {
      return fail("oracle mapped result mismatch");
    }
    if (up_auth != OMS_AUTHENTICITY_OK && up_auth != OMS_AUTHENTICITY_OK_WITH_MISSING_INFO) {
      return fail("oracle expected positive upstream authenticity for signed fixture");
    }
  }

  // Oracle negative: tampered must be OMS_AUTHENTICITY_NOT_OK ↔ Nanexus NotOk / exit 1
  {
    VerifyOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (fix / "h264/tampered.h264").string();
    opt.ca_pem_path = (fix / "pki/ca.pem").string();
    auto nanexus = VerifyAnnexBFile(opt);
    if (!nanexus.ok()) {
      return fail("oracle tampered nanexus");
    }
    if (nanexus.value().signature_integrity != SignatureIntegrity::NotOk) {
      return fail("oracle tampered expected NotOk");
    }
  }

  // Oracle unsigned: OMS_NOT_SIGNED ↔ unsigned/not-verifiable
  {
    VerifyOptions opt;
    opt.codec = Codec::H264;
    opt.input_path = (fix / "h264/unsigned.h264").string();
    auto nanexus = VerifyAnnexBFile(opt);
    if (!nanexus.ok()) {
      return fail("oracle unsigned");
    }
    if (nanexus.value().signature_integrity != SignatureIntegrity::NotApplicable ||
        ExitCodeForVerification(nanexus.value()) != ExitCode::UnsignedOrNotVerifiable) {
      return fail("oracle unsigned classification");
    }
  }

  std::cout << "PASS: verify integration\n";
  return 0;
}
