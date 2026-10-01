#include "videotrust/live_ingest.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

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
  bool linked{false};
  bool ambiguous{false};
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
    } else {
      GstPad* sink = gst_element_get_static_pad(context->depay, "sink");
      context->linked = gst_pad_link(pad, sink) == GST_PAD_LINK_OK;
      gst_object_unref(sink);
    }
  }
  gst_caps_unref(caps);
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

LiveIngestResult RunGStreamerLiveIngest(const LiveIngestOptions& options,
                                        const LiveNalConsumer& consume,
                                        const LiveCancelRequested& cancelled) {
  gst_init(nullptr, nullptr);
  if (!LiveIngestDependenciesAvailable()) {
    return Failure(ErrorCode::NotSupported,
                   "required live media components are unavailable");
  }

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
  PadContext pad_context{depay, options.codec, false, false};
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
  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (cancelled()) {
      result.stop_reason = LiveStopReason::CallerCancelled;
      break;
    }
    if (now - started >= std::chrono::seconds(options.duration_seconds)) break;
    if (result.samples > 0 &&
        now - last_sample >= std::chrono::seconds(kLiveReadStallTimeoutSeconds)) {
      result.stop_reason = LiveStopReason::TransportFailure;
      result.error = MakeError(ErrorCode::UpstreamFailure, "live transport stalled");
      break;
    }
    GstMessage* message = gst_bus_pop_filtered(
        bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    if (message) {
      const bool eos = GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
      gst_message_unref(message);
      result.stop_reason = eos ? LiveStopReason::OrderlyEnd
                               : LiveStopReason::TransportFailure;
      if (!eos) {
        result.error = MakeError(ErrorCode::UpstreamFailure,
                                 "live transport failed");
      }
      break;
    }
    GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink),
                                                      100 * GST_MSECOND);
    if (!sample) continue;
    last_sample = std::chrono::steady_clock::now();
    ++result.samples;
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    const gsize size = gst_buffer_get_size(buffer);
    if (size == 0 || size > kLiveMaxSampleBytes) {
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
    for (const auto& nal : nalus.value()) {
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
  gst_element_set_state(pipeline.get(), GST_STATE_NULL);
  gst_object_unref(bus);
  return result;
}

}  // namespace videotrust
