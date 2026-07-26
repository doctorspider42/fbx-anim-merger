#pragma once

#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace fam {

enum class LogLevel { Info, Success, Warning, Error };

struct LogEntry {
    LogLevel level;
    std::string text;
};

class Log {
public:
    static Log& Get();

    void Push(LogLevel level, std::string text);
    void Clear();

    // Mirrors everything to disk, flushed per line, so a crash or a message that
    // scrolled away is still recoverable. Opt-in: only the application calls this.
    void OpenFile(const std::string& path);
    const std::string& FilePath() const { return m_filePath; }

    // Where the console mirror goes: `info` takes everything below Error, `error`
    // takes Error. Either may be null to silence that half. The CLI points both at
    // stderr so stdout can carry nothing but its machine-readable report.
    void SetConsoleStreams(std::FILE* info, std::FILE* error);

    // Every entry as one blob, for the clipboard.
    std::string ToText() const;

    // Copy is intentional: keeps the UI thread away from the mutex while drawing.
    std::deque<LogEntry> Snapshot() const;

    bool dirty = true;  // set on every push, cleared by the UI to auto-scroll

private:
    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    std::ofstream m_file;
    std::string m_filePath;
    std::FILE* m_console = stdout;
    std::FILE* m_consoleError = stderr;
    static constexpr size_t kMaxEntries = 2000;
};

namespace detail {
template <typename... Args>
std::string Format(const char* fmt, Args&&... args) {
    if constexpr (sizeof...(Args) == 0) {
        return std::string(fmt);
    } else {
        char buffer[2048];
        std::snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
        return std::string(buffer);
    }
}
}  // namespace detail

template <typename... Args>
void LogInfo(const char* fmt, Args&&... args) {
    Log::Get().Push(LogLevel::Info, detail::Format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void LogSuccess(const char* fmt, Args&&... args) {
    Log::Get().Push(LogLevel::Success, detail::Format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void LogWarn(const char* fmt, Args&&... args) {
    Log::Get().Push(LogLevel::Warning, detail::Format(fmt, std::forward<Args>(args)...));
}
template <typename... Args>
void LogError(const char* fmt, Args&&... args) {
    Log::Get().Push(LogLevel::Error, detail::Format(fmt, std::forward<Args>(args)...));
}

}  // namespace fam
