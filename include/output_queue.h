#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace cansim {
// FIFO with capacity reserved for command responses. Monitoring is best effort.
template<size_t Capacity = 8192, size_t ReplyReserve = 1024> class OutputQueue {
    static_assert(Capacity > ReplyReserve, "reserve must be smaller than queue");
public:
    bool push(const char* text, bool monitor = false) {
        const size_t len = std::strlen(text);
        if (len + 1 + (monitor ? ReplyReserve : 0) > Capacity - used_) {
            if (monitor) ++droppedMonitor; else ++droppedReplies;
            return false;
        }
        for (size_t i = 0; i < len; ++i) put(text[i]);
        put('\n'); return true;
    }
    void format(bool monitor, const char* fmt, ...) {
        char line[384]; va_list args; va_start(args, fmt);
        const int n = vsnprintf(line, sizeof(line), fmt, args); va_end(args);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(line)) {
            if (monitor) ++droppedMonitor; else ++droppedReplies;
            return;
        }
        push(line, monitor);
    }
    template<class Writer> void drain(Writer& writer) {
        const int available = writer.availableForWrite();
        if (available <= 0 || !used_) return;
        size_t n = static_cast<size_t>(available);
        if (n > used_) n = used_;
        if (n > Capacity - tail_) n = Capacity - tail_;
        const size_t wrote = writer.write(reinterpret_cast<const uint8_t*>(buffer_ + tail_), n);
        tail_ = (tail_ + wrote) % Capacity; used_ -= wrote;
    }
    size_t free() const { return Capacity - used_; }
    uint64_t droppedMonitor = 0, droppedReplies = 0;
private:
    void put(char c) { buffer_[head_] = c; head_ = (head_ + 1) % Capacity; ++used_; }
    char buffer_[Capacity] = {};
    size_t head_ = 0, tail_ = 0, used_ = 0;
};
}
