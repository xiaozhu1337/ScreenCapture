#include "PipeWireFrames.h"
#include <QMetaObject>
#include <spa/buffer/meta.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <unistd.h>

PipeWireFrames::PipeWireFrames(QObject* parent) : QObject(parent) {}
PipeWireFrames::~PipeWireFrames() { stop(); }

bool PipeWireFrames::start(int remoteFd, uint node, qulonglong serial)
{
    stop();
    pw_init(nullptr, nullptr);
    loop = pw_thread_loop_new("screencapture", nullptr);
    if (!loop) { close(remoteFd); return false; }
    context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    if (!context) { close(remoteFd); stop(); return false; }
    core = pw_context_connect_fd(context, remoteFd, nullptr, 0);
    if (!core) { stop(); return false; }
    auto* properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);
    if (!properties) { stop(); return false; }
    if (serial) pw_properties_setf(properties, PW_KEY_TARGET_OBJECT, "%llu", serial);
    stream = pw_stream_new(core, "ScreenCapture", properties);
    if (!stream) { stop(); return false; }
    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.state_changed = &PipeWireFrames::stateChanged;
        value.param_changed = &PipeWireFrames::formatChanged;
        value.process = &PipeWireFrames::process;
        return value;
    }();
    pw_stream_add_listener(stream, &listener, &events, this);
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod* params[] = {static_cast<spa_pod*>(spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(4, SPA_VIDEO_FORMAT_BGRA,
            SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_RGBx)))};
    if (pw_stream_connect(stream, PW_DIRECTION_INPUT, serial ? PW_ID_ANY : node,
            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1) < 0
        || pw_thread_loop_start(loop) < 0) {
        stop();
        return false;
    }
    return true;
}

void PipeWireFrames::stop()
{
    if (loop) pw_thread_loop_stop(loop);
    ++generation;
    queued = false;
    if (stream) { spa_hook_remove(&listener); pw_stream_destroy(stream); stream = nullptr; }
    if (core) { pw_core_disconnect(core); core = nullptr; }
    if (context) { pw_context_destroy(context); context = nullptr; }
    if (loop) { pw_thread_loop_destroy(loop); loop = nullptr; }
    info = {};
    connected = false;
}

void PipeWireFrames::stateChanged(void* data, pw_stream_state, pw_stream_state state, const char* error)
{
    auto* self = static_cast<PipeWireFrames*>(data);
    if (state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING) self->connected = true;
    const bool disconnected = state == PW_STREAM_STATE_UNCONNECTED && self->connected;
    if (state != PW_STREAM_STATE_ERROR && !disconnected) return;
    self->connected = false;
    const auto message = disconnected ? tr("PipeWire 采集流已断开。")
        : tr("PipeWire 采集失败：%1").arg(QString::fromUtf8(error ? error : "unknown error"));
    const uint current = self->generation;
    QMetaObject::invokeMethod(self, [self, message, current] {
        if (current == self->generation) emit self->failed(message);
    }, Qt::QueuedConnection);
}

void PipeWireFrames::formatChanged(void* data, uint32_t id, const spa_pod* format)
{
    if (id != SPA_PARAM_Format || !format) return;
    auto* self = static_cast<PipeWireFrames*>(data);
    spa_format_video_raw_parse(format, &self->info);
    // Request mapped CPU buffers; importing DMA-BUF requires a graphics context.
    uint8_t buffer[256];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod* params[] = {static_cast<spa_pod*>(spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))))};
    pw_stream_update_params(self->stream, params, 1);
}

QImage PipeWireFrames::copyFrame(const spa_buffer& buffer, const spa_video_info_raw& info)
{
    const auto* header = static_cast<const spa_meta_header*>(spa_buffer_find_meta_data(&buffer, SPA_META_Header, sizeof(spa_meta_header)));
    if (header && (header->flags & SPA_META_HEADER_FLAG_CORRUPTED)) return {};
    if (!buffer.n_datas || !buffer.datas[0].data || !buffer.datas[0].chunk) return {};
    const spa_data& pixels = buffer.datas[0];
    if (pixels.chunk->flags & (SPA_CHUNK_FLAG_CORRUPTED | SPA_CHUNK_FLAG_EMPTY)) return {};
    const int width = int(info.size.width), height = int(info.size.height);
    const int stride = pixels.chunk->stride;
    const uint32_t offset = pixels.chunk->offset;
    const bool bgr = info.format == SPA_VIDEO_FORMAT_BGRA || info.format == SPA_VIDEO_FORMAT_BGRx;
    const bool rgb = info.format == SPA_VIDEO_FORMAT_RGBA || info.format == SPA_VIDEO_FORMAT_RGBx;
    if (!(bgr || rgb) || width <= 0 || height <= 0 || qint64(stride) < qint64(width) * 4 || offset > pixels.maxsize) return {};
    const quint64 required = quint64(stride) * (height - 1) + quint64(width) * 4;
    if (pixels.chunk->size > pixels.maxsize - offset || required > pixels.maxsize - offset || required > pixels.chunk->size) return {};
    const auto* bytes = static_cast<const uchar*>(pixels.data) + offset;
    return QImage(bytes, width, height, stride, bgr ? QImage::Format_RGB32 : QImage::Format_RGBX8888).copy();
}

void PipeWireFrames::process(void* data)
{
    auto* self = static_cast<PipeWireFrames*>(data);
    pw_buffer* latest = nullptr;
    while (pw_buffer* next = pw_stream_dequeue_buffer(self->stream)) {
        if (latest) pw_stream_queue_buffer(self->stream, latest);
        latest = next;
    }
    if (!latest) return;
    if (!self->queued.load()) {
        QImage copy = copyFrame(*latest->buffer, self->info);
        if (!copy.isNull()) {
            self->queued = true;
            const uint current = self->generation;
            QMetaObject::invokeMethod(self, [self, copy = std::move(copy), current] {
                if (current != self->generation) return;
                self->queued = false;
                emit self->frameReady(copy);
            }, Qt::QueuedConnection);
        }
    }
    pw_stream_queue_buffer(self->stream, latest);
}
