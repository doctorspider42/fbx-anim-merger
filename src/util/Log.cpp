#include "util/Log.h"

#include <cstdio>

namespace fam {

Log& Log::Get() {
    static Log instance;
    return instance;
}

void Log::Push(LogLevel level, std::string text) {
    const char* prefix = "[info ] ";
    switch (level) {
        case LogLevel::Success: prefix = "[ ok  ] "; break;
        case LogLevel::Warning: prefix = "[warn ] "; break;
        case LogLevel::Error:   prefix = "[error] "; break;
        default: break;
    }
    std::fprintf(level == LogLevel::Error ? stderr : stdout, "%s%s\n", prefix, text.c_str());

    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.push_back({level, std::move(text)});
    while (m_entries.size() > kMaxEntries) m_entries.pop_front();
    dirty = true;
}

void Log::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    dirty = true;
}

std::deque<LogEntry> Log::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

}  // namespace fam
