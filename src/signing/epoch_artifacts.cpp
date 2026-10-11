#include "videotrust/epoch_artifacts.hpp"
#include <nlohmann/json.hpp>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <utility>

namespace videotrust {
namespace {
struct Fd {
  int value{-1};
  explicit Fd(int fd = -1) : value(fd) {}
  ~Fd() { if (value >= 0) ::close(value); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : value(std::exchange(other.value, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (value >= 0) ::close(value);
    value = std::exchange(other.value, -1); return *this;
  }
  bool Close() { return value < 0 || ::close(std::exchange(value, -1)) == 0; }
};
Error Io() { return MakeError(ErrorCode::IoFailure, "epoch artifact persistence failed"); }
Error Ok() { return MakeError(ErrorCode::Ok, {}); }
bool Write(int fd, std::span<const uint8_t> data) {
  while (!data.empty()) {
    const auto count = ::write(fd, data.data(), data.size());
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    data = data.subspan(static_cast<std::size_t>(count));
  }
  return true;
}
const char* State(SigningState state) {
  switch (state) {
    case SigningState::Finalized: return "finalized";
    case SigningState::Aborted: return "aborted";
    case SigningState::Failed: return "failed";
    default: return "incomplete";
  }
}
const char* End(GatewayEnd end) {
  switch (end) {
    case GatewayEnd::CleanEos: return "clean_eos";
    case GatewayEnd::BoundedStop: return "bounded_stop";
    case GatewayEnd::Aborted: return "cancelled_or_aborted";
    case GatewayEnd::ContinuityLost: return "continuity_lost";
    case GatewayEnd::Failed: return "failed";
    default: return "incomplete";
  }
}
const char* Failure(GatewayFailure failure) {
  switch (failure) {
    case GatewayFailure::Input: return "input";
    case GatewayFailure::Identity: return "identity";
    case GatewayFailure::Signer: return "signer";
    case GatewayFailure::Output: return "output";
    case GatewayFailure::Transport: return "transport";
    case GatewayFailure::Resource: return "resource";
    case GatewayFailure::Continuity: return "continuity";
    default: return "none";
  }
}
// Temp files are exclusive and retained on failure, never arbitrary cleanup.
// Replacement is confined to our newly created private directory and own file.
bool Document(int directory, const char* name, const nlohmann::json& json, bool replace) {
  auto text = json.dump(2) + "\n";
  if (text.size() > 4096) return false;
  const std::string temporary = std::string(name) + ".pending";
  Fd file(::openat(directory, replace ? temporary.c_str() : name,
      O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  if (file.value < 0 || !Write(file.value,
      {reinterpret_cast<const uint8_t*>(text.data()), text.size()}) ||
      ::fsync(file.value) != 0 || !file.Close()) return false;
  if (replace && ::renameat(directory, temporary.c_str(), directory, name) != 0) return false;
  return ::fsync(directory) == 0;
}
}

struct EpochArtifacts::Impl {
  struct Epoch {
    Fd directory, media;
    unsigned id;
    std::size_t bytes{0};
    SigningState state{SigningState::Active};
    bool closed{false}, io_failed{false};
  };
  Fd root;
  Codec codec;
  std::vector<std::shared_ptr<Epoch>> epochs;
  std::size_t bytes{0};
  bool finished{false}, failed{false};
  const char* MediaName() const { return codec == Codec::H264 ? "signed.h264" : "signed.h265"; }
  nlohmann::json Metadata(const Epoch& epoch, const char* reason, GatewayFailure failure) const {
    return {{"format", "nanexus.gateway-epoch.v1"}, {"epoch", epoch.id},
      {"codec", codec == Codec::H264 ? "h264" : "h265"}, {"media", MediaName()},
      {"bytes_acknowledged", epoch.bytes}, {"state", State(epoch.state)},
      {"start_reason", "random_access_after_start_or_discontinuity"},
      {"termination_reason", reason}, {"failure_stage", Failure(failure)},
      {"source_identity", "not_established"}, {"cross_epoch_continuity", false},
      {"evidence", "unsigned_operator_metadata_not_verification"}};
  }
};

Expected<EpochArtifacts> EpochArtifacts::Create(const std::string& path, Codec codec) {
  if (path.size() < 2 || path.size() > 4096 || path.front() != '/' ||
      path.back() == '/' || path.find('\0') != std::string::npos ||
      path.find_first_of("\r\n\t") != std::string::npos ||
      (codec != Codec::H264 && codec != Codec::H265))
    return MakeError(ErrorCode::InvalidArgument, "output root must be a new absolute directory");
  std::vector<std::string> parts;
  for (std::size_t start = 1; start < path.size();) {
    auto end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    auto part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == ".." || part.size() > 255)
      return MakeError(ErrorCode::InvalidArgument, "unsafe output root component");
    parts.push_back(std::move(part)); start = end + 1;
  }
  Fd parent(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
    Fd next(::openat(parent.value, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (next.value < 0) return MakeError(ErrorCode::InvalidArgument, "output parent unavailable or unsafe");
    parent = std::move(next);
  }
  if (::mkdirat(parent.value, parts.back().c_str(), 0700) != 0)
    return MakeError(ErrorCode::InvalidArgument, "output root unavailable or already exists");
  Fd root(::openat(parent.value, parts.back().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (root.value < 0 || ::fsync(parent.value) != 0) return Io();
  auto impl = std::make_shared<Impl>();
  impl->root = std::move(root); impl->codec = codec;
  return EpochArtifacts(std::move(impl));
}

SigningEpochSinkFactory EpochArtifacts::Factory() const {
  return [impl = impl_](unsigned id) -> Expected<SigningEpochSink> {
    if (impl->finished || impl->failed || id != impl->epochs.size() + 1 || id > 8 ||
        (!impl->epochs.empty() && !impl->epochs.back()->closed)) return Io();
    char name[24]; std::snprintf(name, sizeof(name), "epoch-%06u", id);
    if (::mkdirat(impl->root.value, name, 0700) != 0) { impl->failed = true; return Io(); }
    auto epoch = std::make_shared<Impl::Epoch>();
    epoch->id = id;
    epoch->directory = Fd(::openat(impl->root.value, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (epoch->directory.value < 0) { impl->failed = true; return Io(); }
    epoch->media = Fd(::openat(epoch->directory.value, impl->MediaName(),
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    impl->epochs.push_back(epoch);
    if (epoch->media.value < 0 || !Document(epoch->directory.value, "epoch.json",
        impl->Metadata(*epoch, "incomplete", GatewayFailure::None), false) ||
        ::fsync(impl->root.value) != 0) { impl->failed = true; return Io(); }
    return SigningEpochSink{
      [impl, epoch](std::span<const uint8_t> data) {
        if (impl->failed || epoch->closed || data.size() > kSigningMaxNalBytes ||
            data.size() > kSigningMaxOutputBytes - impl->bytes || !Write(epoch->media.value, data)) {
          epoch->io_failed = impl->failed = true; return Io();
        }
        epoch->bytes += data.size(); impl->bytes += data.size(); return Ok();
      },
      [impl, epoch](SigningState state) {
        if (epoch->closed) return Io();
        epoch->closed = true;
        const bool synced = ::fsync(epoch->media.value) == 0;
        const bool closed = epoch->media.Close();
        epoch->state = synced && closed && !epoch->io_failed ? state : SigningState::Failed;
        const bool documented = Document(epoch->directory.value, "epoch.json",
            impl->Metadata(*epoch, "pending_run_result", epoch->state == SigningState::Failed
                ? GatewayFailure::Output : GatewayFailure::None), true);
        if (!synced || !closed || !documented || epoch->io_failed) {
          impl->failed = true; return Io();
        }
        return Ok();
      }};
  };
}

Error EpochArtifacts::Finish(const LiveSigningResult& result) {
  if (impl_->finished || impl_->failed || result.end == GatewayEnd::Running) return Io();
  impl_->finished = true;
  for (const auto& epoch : impl_->epochs) {
    if (!epoch->closed) return Io();
    auto failure = result.failure;
    const char* reason = End(result.end);
    for (const auto& summary : result.epochs) if (summary.epoch == epoch->id) {
      failure = summary.failure;
      if (failure == GatewayFailure::Continuity) reason = "continuity_lost";
    }
    if (!Document(epoch->directory.value, "epoch.json",
        impl_->Metadata(*epoch, reason, failure), true)) return Io();
  }
  if (!Document(impl_->root.value, "session.json", {
      {"format", "nanexus.gateway-session.v1"}, {"end", End(result.end)},
      {"failure_stage", Failure(result.failure)}, {"epochs", impl_->epochs.size()},
      {"bytes_acknowledged", impl_->bytes}, {"cross_epoch_continuity", false},
      {"source_identity", "not_established"}}, false)) return Io();
  return Ok();
}
} // namespace videotrust
