#include "videotrust/epoch_artifacts.hpp"
#include "videotrust/nal_classify.hpp"
#include "videotrust/verify.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cerrno>
#include <unistd.h>

// Faults at the ordinary POSIX boundary, test-only: no product fault switches.
static int write_fault = 0;
static bool sync_fault = false, close_fault = false;
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t count) {
  if (write_fault == 2) { write_fault = 1; return __real_write(fd, data, 1); }
  if (write_fault == 1) { write_fault = 0; errno = ENOSPC; return -1; }
  return __real_write(fd, data, count);
}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd) {
  if (sync_fault) { sync_fault = false; errno = EIO; return -1; }
  return __real_fsync(fd);
}
extern "C" int __real_close(int);
extern "C" int __wrap_close(int fd) {
  const auto result = __real_close(fd);
  if (close_fault) { close_fault = false; errno = EIO; return -1; }
  return result;
}
using namespace videotrust;
namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (false)
nlohmann::json Read(const fs::path& path) { std::ifstream in(path); return nlohmann::json::parse(in); }
std::string Bytes(const fs::path& path) { std::ifstream in(path, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
int CodecCases(const fs::path& fixtures, const fs::path& out, Codec codec) {
  const std::string name = codec == Codec::H264 ? "h264" : "h265";
  auto parsed = AnnexBReader::ParseFile((fixtures/name/("unsigned."+name)).string());
  CHECK(parsed.ok());
  std::vector<std::vector<NalUnit>> units;
  std::vector<NalUnit> unit;
  for (const auto& nal : parsed.value()) {
    unit.push_back(nal);
    if (IsVclNal(codec, nal)) { units.push_back(std::move(unit)); unit.clear(); }
  }
  LiveSigningOptions options;
  options.signer = {codec, (fixtures/"pki/signer.key.pem").string(), (fixtures/"pki/signer-chain.pem").string()};
  options.signer.max_signing_frames = 5;
  constexpr int64_t time = 133000000000000000LL;
  for (const auto end : {SigningIngestKind::CleanEos, SigningIngestKind::BoundedStop,
      SigningIngestKind::Discontinuity, SigningIngestKind::Abort, SigningIngestKind::Failure}) {
    const auto root = out/(name+"-"+std::to_string(static_cast<int>(end)));
    auto writer = EpochArtifacts::Create(root.string(), codec); CHECK(writer.ok());
    auto gateway = LiveSigningOrchestrator::Create(options, writer.value().Factory()); CHECK(gateway.ok());
    for (const auto& au : units) CHECK(gateway.value()->Consume({SigningIngestKind::Media, au, time}).code == ErrorCode::Ok);
    const auto media = root/"epoch-000001"/("signed."+name);
    const auto prefix = Bytes(media);
    gateway.value()->Consume({end});
    if (end == SigningIngestKind::Discontinuity) {
      CHECK(Bytes(media) == prefix);
      for (const auto& au : units) CHECK(gateway.value()->Consume({SigningIngestKind::Media, au, time}).code == ErrorCode::Ok);
      CHECK(gateway.value()->Consume({SigningIngestKind::CleanEos}).code == ErrorCode::Ok);
      CHECK(Bytes(media) == prefix); // No B bytes ever appended to A.
      const auto second = root/"epoch-000002"/("signed."+name);
      auto verified = VerifyAnnexBFile({codec, second.string(), (fixtures/"pki/ca.pem").string()});
      CHECK(verified.ok() && DeriveOverallState(verified.value()) == OverallState::Valid);
    }
    CHECK(writer.value().Finish(gateway.value()->result()).code == ErrorCode::Ok);
    auto metadata = Read(root/"epoch-000001/epoch.json");
    const bool finalized = end == SigningIngestKind::CleanEos || end == SigningIngestKind::BoundedStop;
    CHECK(metadata["state"] == (finalized ? "finalized" : "aborted"));
    CHECK(metadata["cross_epoch_continuity"] == false && metadata["source_identity"] == "not_established");
    CHECK(metadata["bytes_acknowledged"] == fs::file_size(media));
    CHECK(fs::file_size(root/"epoch-000001/epoch.json") <= 4096);
    CHECK(!EpochArtifacts::Create(root.string(), codec).ok());
    if (finalized) {
      auto verified = VerifyAnnexBFile({codec, media.string(), (fixtures/"pki/ca.pem").string()});
      CHECK(verified.ok() && DeriveOverallState(verified.value()) == OverallState::Valid);
      CHECK(verified.value().source_authenticity == SourceAuthenticity::NotEstablished);
    }
    if (end == SigningIngestKind::Discontinuity) {
      CHECK(metadata["termination_reason"] == "continuity_lost");
      CHECK(Read(root/"epoch-000002/epoch.json")["state"] == "finalized");
    }
  }
  {
    const auto root = out/(name+"-byte-bound");
    auto writer = EpochArtifacts::Create(root.string(), codec); CHECK(writer.ok());
    auto bounded = options; bounded.max_output_bytes = 1;
    auto gateway = LiveSigningOrchestrator::Create(bounded, writer.value().Factory()); CHECK(gateway.ok());
    CHECK(gateway.value()->Consume({SigningIngestKind::Media, units[0], time}).code != ErrorCode::Ok);
    CHECK(gateway.value()->result().failure == GatewayFailure::Output);
    CHECK(writer.value().Finish(gateway.value()->result()).code == ErrorCode::Ok);
    CHECK(Read(root/"epoch-000001/epoch.json")["state"] == "failed");
    CHECK(fs::file_size(root/"epoch-000001"/("signed."+name)) == 0);
  }
  for (int mode = 0; mode < 4; ++mode) {
    const auto root = out/(name+"-fault-"+std::to_string(mode));
    auto writer = EpochArtifacts::Create(root.string(), codec); CHECK(writer.ok());
    auto gateway = LiveSigningOrchestrator::Create(options, writer.value().Factory()); CHECK(gateway.ok());
    CHECK(gateway.value()->Consume({SigningIngestKind::Media, units[0], time}).code == ErrorCode::Ok);
    Error error;
    if (mode == 0) {
      write_fault = 2;
      error = gateway.value()->Consume({SigningIngestKind::Media, units[1], time});
    } else if (mode == 3) {
      // Fail metadata publication after successful sink closure.
      CHECK(gateway.value()->Consume({SigningIngestKind::CleanEos}).code == ErrorCode::Ok);
      sync_fault = true;
      error = writer.value().Finish(gateway.value()->result());
    } else {
      if (mode == 1) sync_fault = true;
      else close_fault = true;
      error = gateway.value()->Consume({SigningIngestKind::Abort});
    }
    CHECK(error.code != ErrorCode::Ok);
    CHECK(!fs::exists(root/"session.json"));
    if (mode != 3) {
      CHECK(gateway.value()->result().failure == GatewayFailure::Output);
      CHECK(Read(root/"epoch-000001/epoch.json")["state"] == "failed");
      CHECK(writer.value().Finish(gateway.value()->result()).code != ErrorCode::Ok);
    }
  }
  std::cout << "PASS " << name << " durable EOS/stop VALID; gap separate VALID restart/no concatenation; abort/loss; partial write/sync/close/publication failures\n";
  return 0;
}
int main(int argc, char** argv) {
  CHECK(argc == 2);
  char pattern[] = "/tmp/nanexus-a3-artifacts-XXXXXX";
  auto path = ::mkdtemp(pattern); CHECK(path);
  const fs::path out(path); // Fixed test-owned temporary directory only.
  CHECK(!EpochArtifacts::Create("relative", Codec::H264).ok());
  CHECK(!EpochArtifacts::Create((out/".."/"escape").string(), Codec::H264).ok());
  CHECK(!EpochArtifacts::Create((out/"."/"escape").string(), Codec::H264).ok());
  fs::create_directory_symlink(out, out/"symlink");
  CHECK(!EpochArtifacts::Create((out/"symlink"/"escape").string(), Codec::H264).ok());
  CHECK(!fs::exists(out/"escape"));
  CHECK(!EpochArtifacts::Create((out/"symlink").string(), Codec::H264).ok());
  { std::ofstream(out/"file") << "untouched"; }
  CHECK(!EpochArtifacts::Create((out/"file").string(), Codec::H264).ok());
  CHECK(Bytes(out/"file") == "untouched");
  const auto result = CodecCases(argv[1], out, Codec::H264) || CodecCases(argv[1], out, Codec::H265);
  std::cout << "PASS root collision/traversal/symlink/file protection\n";
  fs::remove_all(out); // Only mkdtemp directory owned by this test.
  return result;
}
