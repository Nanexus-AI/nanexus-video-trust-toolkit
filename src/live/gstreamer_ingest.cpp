#include "videotrust/live_ingest.hpp"
#include "videotrust/live_signing.hpp"
#include "ingest_continuity.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <atomic>

#include <chrono>
#include <memory>
#include <string>

namespace videotrust {
namespace {

using ElementPtr = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;

bool HasFactory(const char* name) {
  GstElementFactory* factory = gst_element_factory_find(name);
  if (!factory) return false;
  gst_object_unref(factory);
  return true;
}

bool HasRandomAccess(const std::vector<NalUnit>& nalus, Codec codec) {
  for (const auto& nal : nalus) {
    if (nal.bytes.size() <= nal.start_code_size) continue;
    const unsigned char header = nal.bytes[nal.start_code_size];
    if (codec == Codec::H264 && (header & 0x1fU) == 5U) return true;
    const unsigned type = (header >> 1U) & 0x3fU;
    if (codec == Codec::H265 && type >= 16U && type <= 23U) return true;
  }
  return false;
}

struct PadContext {
  GstElement* depay{nullptr};
  Codec codec{Codec::H264};
  std::atomic<bool> linked{false};
  std::atomic<bool> ambiguous{false};
  detail::IngestContinuity* continuity{nullptr};
};

void OnPadAdded(GstElement*, GstPad* pad, gpointer data) {
  auto* context = static_cast<PadContext*>(data);
  GstCaps* caps = gst_pad_get_current_caps(pad);
  if (!caps) caps = gst_pad_query_caps(pad, nullptr);
  if (!caps || gst_caps_is_empty(caps)) {
    if (caps) gst_caps_unref(caps);
    return;
  }
  const GstStructure* structure = gst_caps_get_structure(caps, 0);
  const char* media = gst_structure_get_string(structure, "media");
  const char* encoding = gst_structure_get_string(structure, "encoding-name");
  const char* expected = context->codec == Codec::H264 ? "H264" : "H265";
  if (media && encoding && std::string(media) == "video" &&
      std::string(encoding) == expected) {
    if (context->linked) {
      context->ambiguous = true;
      if (context->continuity) context->continuity->Observe(detail::IngestContinuity::Event::SourceRemoved);
    } else {
      GstPad* sink = gst_element_get_static_pad(context->depay, "sink");
      context->linked = gst_pad_link(pad, sink) == GST_PAD_LINK_OK;
      gst_object_unref(sink);
    }
  }
  gst_caps_unref(caps);
}

void OnPadRemoved(GstElement*, GstPad*, gpointer data) {
  auto* context = static_cast<PadContext*>(data);
  if (context->continuity)
    context->continuity->Observe(detail::IngestContinuity::Event::SourceRemoved);
}

gboolean OnBeforeSend(GstElement*, GstRTSPMessage* message, gpointer data) {
  auto* continuity = static_cast<detail::IngestContinuity*>(data);
  // Public message fields avoid adding a second RTSP implementation/library.
  if (message->type == GST_RTSP_MESSAGE_REQUEST &&
      (message->type_data.request.method == GST_RTSP_SETUP ||
       message->type_data.request.method == GST_RTSP_PLAY))
    continuity->SessionReconfigure();
  return TRUE;
}

struct ProbeContext {
  detail::IngestContinuity* continuity;
  bool output;
};

void ObserveBuffer(ProbeContext& context, GstBuffer* buffer) {
  const bool discont = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT);
  const bool corrupted = GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED) ||
                         GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_GAP);
  if (context.output) {
    context.continuity->Output(discont, corrupted);
    return;
  }
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp)) {
    context.continuity->InvalidPacket();
    return;
  }
  context.continuity->Rtp(gst_rtp_buffer_get_seq(&rtp),
      gst_rtp_buffer_get_ssrc(&rtp), discont, corrupted);
  gst_rtp_buffer_unmap(&rtp);
}

GstPadProbeReturn ObserveContinuity(GstPad*, GstPadProbeInfo* info, gpointer data) {
  auto& context = *static_cast<ProbeContext*>(data);
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)
    ObserveBuffer(context, GST_PAD_PROBE_INFO_BUFFER(info));
  if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    auto* list = GST_PAD_PROBE_INFO_BUFFER_LIST(info);
    for (guint i = 0; i < gst_buffer_list_length(list); ++i)
      ObserveBuffer(context, gst_buffer_list_get(list, i));
  }
  if (GST_PAD_PROBE_INFO_TYPE(info) &
      (GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH)) {
    auto* event = GST_PAD_PROBE_INFO_EVENT(info);
    using Event = detail::IngestContinuity::Event;
    switch (GST_EVENT_TYPE(event)) {
      case GST_EVENT_STREAM_START: context.continuity->Observe(Event::StreamStart, context.output); break;
      case GST_EVENT_CAPS: context.continuity->Observe(Event::Caps, context.output); break;
      case GST_EVENT_SEGMENT: context.continuity->Observe(Event::Segment, context.output); break;
      case GST_EVENT_FLUSH_START:
      case GST_EVENT_FLUSH_STOP: context.continuity->Observe(Event::Flush, context.output); break;
      case GST_EVENT_GAP: context.continuity->Observe(Event::Gap, context.output); break;
      default: {
        const auto* structure = gst_event_get_structure(event);
        if (structure && gst_structure_has_name(structure, "GstRTPPacketLost"))
          context.continuity->Observe(Event::Gap, context.output);
        break;
      }
    }
  }
  return GST_PAD_PROBE_OK;
}

int64_t ObservationTimestamp() {
  using namespace std::chrono;
  return (duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()
      + 11644473600000000LL) * 10;
}

LiveIngestResult Failure(ErrorCode code, const char* message) {
  LiveIngestResult result;
  result.stop_reason = code == ErrorCode::InvalidArgument
                           ? LiveStopReason::ResourceLimit
                           : LiveStopReason::TransportFailure;
  result.error = MakeError(code, message);
  return result;
}

}  // namespace

bool LiveIngestDependenciesAvailable() noexcept {
  gst_init(nullptr, nullptr);
  return HasFactory("rtspsrc") && HasFactory("rtph264depay") &&
         HasFactory("rtph265depay") && HasFactory("capsfilter") &&
         HasFactory("appsink");
}

static LiveIngestResult RunFixedGStreamerIngest(const LiveIngestOptions& options,
                                        const LiveNalConsumer& consume,
                                        const LiveCancelRequested& cancelled,
                                        const SigningIngestConsumer* signing) {
  gst_init(nullptr, nullptr);
  if (!LiveIngestDependenciesAvailable()) {
    return Failure(ErrorCode::NotSupported,
                   "required live media components are unavailable");
  }

  // Contexts outlive the pipeline, including pad-removal callbacks on teardown.
  detail::IngestContinuity continuity;
  PadContext pad_context{nullptr, options.codec, false, false, signing ? &continuity : nullptr};
  ProbeContext input_probe{&continuity, false}, output_probe{&continuity, true};
  ElementPtr pipeline(gst_pipeline_new("nanexus-live"), gst_object_unref);
  GstElement* source = gst_element_factory_make("rtspsrc", "source");
  GstElement* depay = gst_element_factory_make(
      options.codec == Codec::H264 ? "rtph264depay" : "rtph265depay", "depay");
  GstElement* capsfilter = gst_element_factory_make("capsfilter", "caps");
  GstElement* sink = gst_element_factory_make("appsink", "sink");
  if (!pipeline || !source || !depay || !capsfilter || !sink) {
    return Failure(ErrorCode::NotSupported,
                   "required live media components are unavailable");
  }

  g_object_set(source, "location", options.endpoint.c_str(), "protocols", 4U,
               "latency", 200U, "tcp-timeout",
               static_cast<guint64>(kLiveConnectTimeoutSeconds) * 1000000U,
               "teardown-timeout",
               static_cast<guint64>(kLiveShutdownGraceSeconds) * 1000000U,
               nullptr);
  if (!options.username.empty()) {
    g_object_set(source, "user-id", options.username.c_str(), "user-pw",
                 options.password.c_str(), nullptr);
  }
  GstCaps* caps = gst_caps_new_simple(
      options.codec == Codec::H264 ? "video/x-h264" : "video/x-h265",
      "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING,
      "au", nullptr);
  g_object_set(capsfilter, "caps", caps, nullptr);
  gst_caps_unref(caps);
  g_object_set(sink, "max-buffers", static_cast<guint>(kLiveAppSinkBuffers),
               "drop", FALSE, "sync", FALSE, "emit-signals", FALSE, nullptr);
  gst_bin_add_many(GST_BIN(pipeline.get()), source, depay, capsfilter, sink, nullptr);
  if (!gst_element_link_many(depay, capsfilter, sink, nullptr)) {
    return Failure(ErrorCode::InternalError, "live media graph could not be linked");
  }
  pad_context.depay = depay;
  if (signing) {
    const auto types = static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER |
        GST_PAD_PROBE_TYPE_BUFFER_LIST | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
        GST_PAD_PROBE_TYPE_EVENT_FLUSH);
    GstPad* input = gst_element_get_static_pad(depay, "sink");
    GstPad* output = gst_element_get_static_pad(depay, "src");
    gst_pad_add_probe(input, types, ObserveContinuity, &input_probe, nullptr);
    gst_pad_add_probe(output, types, ObserveContinuity, &output_probe, nullptr);
    gst_object_unref(input);
    gst_object_unref(output);
    g_signal_connect(source, "pad-removed", G_CALLBACK(OnPadRemoved), &pad_context);
    g_signal_connect(source, "before-send", G_CALLBACK(OnBeforeSend), &continuity);
  }
  g_signal_connect(source, "pad-added", G_CALLBACK(OnPadAdded), &pad_context);
  if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    return Failure(ErrorCode::UpstreamFailure, "live transport could not start");
  }

  LiveIngestResult result;
  result.stop_reason = LiveStopReason::DeadlineReached;
  const auto started = std::chrono::steady_clock::now();
  auto last_sample = started;
  bool aligned = false;
  GstBus* bus = gst_element_get_bus(pipeline.get());
  bool discontinuity = false;
  auto continuity_failed = [&]() {
    if (!signing || !continuity.lost()) return false;
    discontinuity = true;
    result.stop_reason = LiveStopReason::TransportFailure;
    result.error = MakeError(ErrorCode::UpstreamFailure, "live media continuity lost");
    return true;
  };
  while (true) {
    if (continuity_failed()) break;
    const auto now = std::chrono::steady_clock::now();
    bool cancel_requested = false;
    if (signing) {
      try { cancel_requested = cancelled(); }
      catch (...) {
        result.stop_reason = LiveStopReason::TransportFailure;
        result.error = MakeError(ErrorCode::InternalError, "live cancellation callback failed");
        break;
      }
    } else cancel_requested = cancelled();
    if (cancel_requested) {
      result.stop_reason = LiveStopReason::CallerCancelled;
      break;
    }
    if (now - started >= std::chrono::seconds(options.duration_seconds)) break;
    if (((result.samples > 0 &&
          now - last_sample >= std::chrono::seconds(kLiveReadStallTimeoutSeconds)) ||
         (signing && result.samples == 0 &&
          now - started >= std::chrono::seconds(kLiveConnectTimeoutSeconds)))) {
      result.stop_reason = LiveStopReason::TransportFailure;
      result.error = MakeError(ErrorCode::UpstreamFailure, "live transport stalled");
      break;
    }
    GstMessage* message = gst_bus_pop_filtered(
        bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
            (signing ? GST_MESSAGE_ELEMENT : 0)));
    if (message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT) {
      const auto* structure = gst_message_get_structure(message);
      const bool timeout = structure && gst_structure_has_name(structure, "GstRTSPSrcTimeout");
      gst_message_unref(message);
      if (timeout) {
        result.stop_reason = LiveStopReason::TransportFailure;
        result.error = MakeError(ErrorCode::UpstreamFailure, "live transport timed out");
        break;
      }
      message = nullptr;
    }
    if (message) {
      const bool eos = GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
      gst_message_unref(message);
      result.stop_reason = eos ? LiveStopReason::OrderlyEnd
                               : LiveStopReason::TransportFailure;
      if (!eos || signing) {
        // rtspsrc can report socket EOF as EOS. Active signing must not turn
        // that ambiguous transport loss into a clean media closure.
        result.stop_reason = LiveStopReason::TransportFailure;
        result.error = MakeError(ErrorCode::UpstreamFailure,
            eos ? "live transport ended without clean closure" : "live transport failed");
      }
      break;
    }
    GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink),
                                                      100 * GST_MSECOND);
    if (!sample) continue;
    last_sample = std::chrono::steady_clock::now();
    ++result.samples;
    if (continuity_failed()) { gst_sample_unref(sample); break; }
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    const gsize size = gst_buffer_get_size(buffer);
    if (!LiveSampleSizeWithinBound(size)) {
      gst_sample_unref(sample);
      result.stop_reason = LiveStopReason::ResourceLimit;
      result.error = MakeError(ErrorCode::InvalidArgument,
                               "live media sample exceeded the fixed bound");
      break;
    }
    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      gst_sample_unref(sample);
      result.stop_reason = LiveStopReason::TransportFailure;
      result.error = MakeError(ErrorCode::UpstreamFailure,
                               "live media sample was unavailable");
      break;
    }
    auto nalus = AnnexBReader::Parse(
        std::span<const uint8_t>(map.data, static_cast<std::size_t>(map.size)));
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    if (!nalus.ok()) {
      result.stop_reason = LiveStopReason::TransportFailure;
      result.error = MakeError(ErrorCode::ParseError,
                               "live media sample was not bounded Annex-B");
      break;
    }
    if (!aligned) {
      if (!HasRandomAccess(nalus.value(), options.codec)) continue;
      aligned = true;
    }
    if (signing) {
      if (continuity_failed()) break;
      try {
        result.error = (*signing)({SigningIngestKind::Media,
            nalus.value(), ObservationTimestamp()});
      } catch (...) {
        result.error = MakeError(ErrorCode::InternalError, "live signing consumer failed");
      }
      if (result.error.code != ErrorCode::Ok) {
        result.stop_reason = LiveStopReason::TransportFailure;
        break;
      }
      result.nalus += nalus.value().size();
    } else for (const auto& nal : nalus.value()) {
      Error consumed = consume(nal);
      if (consumed.code != ErrorCode::Ok) {
        result.stop_reason = consumed.code == ErrorCode::InvalidArgument
                                 ? LiveStopReason::ResourceLimit
                                 : LiveStopReason::TransportFailure;
        result.error = consumed;
        break;
      }
      ++result.nalus;
    }
    if (result.error.code != ErrorCode::Ok || pad_context.ambiguous) {
      if (pad_context.ambiguous) {
        result.stop_reason = LiveStopReason::TransportFailure;
        result.error = MakeError(ErrorCode::NotSupported,
                                 "multiple matching video streams are unsupported");
      }
      break;
    }
  }
  // Check uncertainty before any clean terminal callback. Stop the graph only
  // after snapshotting the cause; our own teardown is not an upstream reset.
  continuity_failed();
  if (signing) {
    const auto kind = discontinuity ? SigningIngestKind::Discontinuity :
        result.error.code != ErrorCode::Ok ? SigningIngestKind::Failure :
        result.stop_reason == LiveStopReason::OrderlyEnd ? SigningIngestKind::CleanEos :
        result.stop_reason == LiveStopReason::DeadlineReached ? SigningIngestKind::BoundedStop :
        SigningIngestKind::Abort;
    try {
      auto terminal = (*signing)({kind, {}, 0, result.error.code});
      if (terminal.code != ErrorCode::Ok) result.error = terminal;
    } catch (...) {
      result.error = MakeError(ErrorCode::InternalError, "live signing consumer failed");
    }
  }
  gst_element_set_state(pipeline.get(), GST_STATE_NULL);
  gst_object_unref(bus);
  return result;
}

LiveIngestResult RunGStreamerLiveIngest(const LiveIngestOptions& options,
    const LiveNalConsumer& consume, const LiveCancelRequested& cancelled) {
  return RunFixedGStreamerIngest(options, consume, cancelled, nullptr);
}

LiveIngestResult RunGStreamerSigningIngest(const LiveIngestOptions& options,
    const SigningIngestConsumer& consume, const LiveCancelRequested& cancelled) {
  if (!consume || !cancelled || options.duration_seconds < kLiveMinDurationSeconds ||
      options.duration_seconds > kLiveMaxDurationSeconds ||
      (options.codec != Codec::H264 && options.codec != Codec::H265) ||
      !options.endpoint.starts_with("rtsp://") || options.endpoint.size() > 2048 ||
      options.endpoint.find_first_of("\r\n\t") != std::string::npos ||
      options.endpoint.substr(7, options.endpoint.find('/', 7) - 7).find('@') != std::string::npos ||
      (options.username.empty() != options.password.empty()) ||
      options.username.size() > 256 || options.password.size() > 256)
    return Failure(ErrorCode::InvalidArgument, "invalid active ingest configuration");
  return RunFixedGStreamerIngest(options, {}, cancelled, &consume);
}

LiveSigningResult RunGStreamerLiveSigning(const LiveIngestOptions& ingest,
    const LiveSigningOptions& signing, SigningEpochSinkFactory factory,
    const LiveCancelRequested& cancelled) {
  if (ingest.codec != signing.signer.codec)
    return {GatewayEnd::Failed, GatewayFailure::Input,
        MakeError(ErrorCode::InvalidArgument, "signing codec mismatch"), {}};
  auto gateway = LiveSigningOrchestrator::Create(signing, std::move(factory));
  if (!gateway.ok())
    return {GatewayEnd::Failed, GatewayFailure::Input, gateway.error(), {}};
  bool continuity_lost = false;
  auto result = RunGStreamerSigningIngest(ingest,
      [&](const SigningIngestEvent& event) {
        continuity_lost |= event.kind == SigningIngestKind::Discontinuity;
        return gateway.value()->Consume(event);
      }, cancelled);
  if (gateway.value()->result().end == GatewayEnd::Running) {
    // Early ingest/setup failure or a terminal discontinuity (which the generic
    // event consumer otherwise permits to resume only in a separate epoch).
    gateway.value()->Consume({!continuity_lost && result.error.code != ErrorCode::Ok &&
        result.error.code != ErrorCode::InvalidArgument
        ? SigningIngestKind::Failure : SigningIngestKind::Abort, {}, 0, result.error.code});
  }
  auto output = gateway.value()->result();
  if (continuity_lost && output.failure == GatewayFailure::None) {
    output.end = GatewayEnd::ContinuityLost;
    output.failure = GatewayFailure::Continuity;
    output.error = MakeError(ErrorCode::UpstreamFailure, "live media continuity lost");
  }
  if (result.error.code == ErrorCode::InvalidArgument && output.failure == GatewayFailure::None) {
    output.end = GatewayEnd::Failed;
    output.failure = GatewayFailure::Input;
    output.error = MakeError(ErrorCode::InvalidArgument, "invalid active ingest input");
  }
  return output;
}

}  // namespace videotrust
