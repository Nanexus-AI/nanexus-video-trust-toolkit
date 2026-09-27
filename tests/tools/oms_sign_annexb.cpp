// Thin test helper wrapping SignAnnexBFile for fixture generation.
#include "videotrust/sign.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "Usage: oms_sign_annexb h264|h265 key.pem cert-chain.pem in.es out.es\n";
    return 2;
  }
  videotrust::SignOptions opt;
  opt.codec = std::string(argv[1]) == "h265" ? videotrust::Codec::H265
                                             : videotrust::Codec::H264;
  opt.key_pem_path = argv[2];
  opt.cert_pem_path = argv[3];
  opt.input_path = argv[4];
  opt.output_path = argv[5];
  opt.force = true;

  const videotrust::Error err = videotrust::SignAnnexBFile(opt);
  if (err.code != videotrust::ErrorCode::Ok) {
    std::cerr << err.message << "\n";
    return static_cast<int>(videotrust::ExitCodeForSign(err));
  }
  return 0;
}
