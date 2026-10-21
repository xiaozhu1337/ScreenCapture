#pragma once
#include <QImage>
#include <QObject>
#include <atomic>
#include <pipewire/pipewire.h>
#include <spa/param/video/raw.h>

class PipeWireFrames : public QObject
{
    Q_OBJECT
public:
    explicit PipeWireFrames(QObject* parent = nullptr);
    ~PipeWireFrames() override;
    bool start(int remoteFd, uint node, qulonglong serial = 0);
    void stop();
    static QImage copyFrame(const spa_buffer& buffer, const spa_video_info_raw& info);
signals:
    void frameReady(QImage image);
    void failed(QString message);
private:
    static void formatChanged(void* data, uint32_t id, const spa_pod* format);
    static void process(void* data);
    static void stateChanged(void* data, pw_stream_state oldState, pw_stream_state state, const char* error);
    pw_thread_loop* loop{};
    pw_context* context{};
    pw_core* core{};
    pw_stream* stream{};
    spa_hook listener{};
    spa_video_info_raw info{};
    std::atomic_bool queued{false};
    bool connected{false};
    std::atomic_uint generation{0};
};
