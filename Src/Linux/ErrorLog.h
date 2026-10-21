#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

class ErrorLog
{
public:
    ErrorLog() : worker([this] { drain(); }) {}
    ~ErrorLog() {
        { std::lock_guard lock(mutex); stopping = true; }
        ready.notify_one();
        worker.join();
    }
    static ErrorLog& instance() { static ErrorLog log; return log; }
    static void message(QtMsgType type, const QMessageLogContext&, const QString& text) {
        if (type != QtWarningMsg && type != QtCriticalMsg && type != QtFatalMsg) return;
        auto& log = instance();
        { std::lock_guard lock(log.mutex); log.messages.push(QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " " + text + "\n"); }
        log.ready.notify_one();
    }
private:
    void drain() {
        const QString directory = QCoreApplication::applicationDirPath() + "/logs";
        QDir().mkpath(directory);
        QFile file(directory + "/errors.log");
        const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Append);
        std::unique_lock lock(mutex);
        while (true) {
            ready.wait(lock, [this] { return stopping || !messages.empty(); });
            if (messages.empty() && stopping) break;
            QString text = std::move(messages.front());
            messages.pop();
            lock.unlock();
            if (opened) {
                file.write(text.toUtf8());
                file.flush();
            } else {
                const auto bytes = text.toUtf8();
                fwrite(bytes.constData(), 1, bytes.size(), stderr);
            }
            lock.lock();
        }
    }
    std::mutex mutex;
    std::condition_variable ready;
    std::queue<QString> messages;
    bool stopping{false};
    std::thread worker;
};
