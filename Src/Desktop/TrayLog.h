#pragma once
#include <Windows.h>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class TrayLog
{
public:
    static void error(const char* operation, DWORD code)
    {
        static TrayLog log;
        SYSTEMTIME time{};
        GetSystemTime(&time);
        char text[256]{};
        sprintf_s(text, "%04u-%02u-%02uT%02u:%02u:%02uZ %s (code=%lu)\n",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
            time.wSecond, operation, code);
        {
            std::lock_guard lock(log.mutex);
            if (log.messages.size() >= 64) return;
            log.messages.emplace(text);
        }
        log.ready.notify_one();
    }
private:
    TrayLog() : worker([this] { drain(); }) {}
    ~TrayLog()
    {
        { std::lock_guard lock(mutex); stopping = true; }
        ready.notify_one();
        worker.join();
    }
    void drain()
    {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, 32768);
        const auto directory = std::filesystem::path(executable).parent_path() / L"logs";
        const auto file = directory / L"tray.log";
        const auto previous = directory / L"tray.previous.log";
        std::unique_lock lock(mutex);
        for (;;) {
            ready.wait(lock, [this] { return stopping || !messages.empty(); });
            if (messages.empty() && stopping) return;
            auto text = std::move(messages.front());
            messages.pop();
            lock.unlock();
            try {
                std::filesystem::create_directories(directory);
                if (std::filesystem::exists(file) &&
                    std::filesystem::file_size(file) + text.size() > 256 * 1024) {
                    std::filesystem::remove(previous);
                    std::filesystem::rename(file, previous);
                }
                std::ofstream stream(file, std::ios::app | std::ios::binary);
                stream << text;
            }
            catch (...) { OutputDebugStringA(text.c_str()); }
            lock.lock();
        }
    }
    std::mutex mutex;
    std::condition_variable ready;
    std::queue<std::string> messages;
    bool stopping{};
    std::thread worker;
};
