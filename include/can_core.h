#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace cansim {
struct Frame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
    bool extended = false;
    bool rtr = false;
};
inline bool valid(const Frame& f) {
    return f.dlc <= 8 && f.id <= (f.extended ? 0x1FFFFFFFU : 0x7FFU);
}
struct RecordedFrame { uint32_t timeMs; Frame frame; };
enum class Mode { Manual, Auto, Playback };
enum class Checksum { None, SumIdDlc }; // Explicit opt-in only; never inferred.
struct Signal {
    uint32_t id;
    const char* name;
    uint8_t byte;
    uint8_t width; // byte-aligned 1..4 byte fields
    bool littleEndian;
    bool isSigned;
    double scale;
    double offset;
    double minimum;
    double maximum;
    double initial;
    double ratePerSecond; // AUTO: linear ramp, clamped to min/max
};
struct Message {
    Frame frame;
    uint32_t intervalMs = 100;
    uint64_t nextMs = 0;
    bool enabled = true;
    Checksum checksum = Checksum::None;
};
class Transport {
public:
    virtual ~Transport() = default;
    // A true result means queued, NOT acknowledged on the bus.
    virtual bool offer(const Frame& frame, uint64_t now) = 0;
};
class Simulator {
public:
    static constexpr size_t MaxMessages = 32, MaxSignals = 64;
    static constexpr uint32_t MaxInterval = 3600000;
    bool add(const Frame& frame, uint32_t interval, Checksum checksum = Checksum::None) {
        if (!valid(frame) || frame.rtr || interval < 1 || interval > MaxInterval ||
            (checksum != Checksum::None && frame.dlc != 8)) return false;
        auto* m = find(frame.id, frame.extended);
        if (!m) {
            if (messageCount_ == MaxMessages) return false;
            m = &messages_[messageCount_++];
        } else {
            // Structural changes would invalidate existing signal definitions.
            if (m->frame.dlc != frame.dlc || m->checksum != checksum) return false;
        }
        m->frame = frame; m->intervalMs = interval; m->checksum = checksum;
        m->nextMs = 0; m->enabled = true; updateChecksum(*m); syncSignals(*m); return true;
    }
    Message* find(uint32_t id, bool extended = false) {
        for (size_t i = 0; i < messageCount_; ++i)
            if (messages_[i].frame.id == id && messages_[i].frame.extended == extended)
                return &messages_[i];
        return nullptr;
    }
    bool setMessage(const Frame& f) {
        auto* m = find(f.id, f.extended);
        if (!m || !valid(f) || f.rtr || f.dlc != m->frame.dlc) return false;
        m->frame = f; updateChecksum(*m); syncSignals(*m); return true;
    }
    bool setByte(uint32_t id, unsigned byte, unsigned value) {
        auto* m = find(id);
        if (!m || byte >= m->frame.dlc || value > 255 ||
            (m->checksum != Checksum::None && byte == 7)) return false;
        m->frame.data[byte] = static_cast<uint8_t>(value); updateChecksum(*m);
        syncSignals(*m); return true;
    }
    bool setInterval(uint32_t id, uint32_t interval) {
        auto* m = find(id);
        if (!m || interval < 1 || interval > MaxInterval) return false;
        m->intervalMs = interval; m->nextMs = 0; return true;
    }
    bool addSignal(const Signal& s) {
        auto* m = find(s.id);
        if (!m || signalCount_ == MaxSignals || !s.name || !*s.name || signal(s.name) ||
            s.width < 1 || s.width > 4 || s.byte + s.width > m->frame.dlc ||
            (m->checksum != Checksum::None && s.byte + s.width > 7) ||
            !std::isfinite(s.scale) || s.scale == 0 || !std::isfinite(s.offset) ||
            !std::isfinite(s.minimum) || !std::isfinite(s.maximum) ||
            !std::isfinite(s.ratePerSecond) || s.minimum > s.maximum) return false;
        for (size_t i = 0; i < signalCount_; ++i) {
            const auto& other = signals_[i];
            if (other.id == s.id && s.byte < other.byte + other.width &&
                other.byte < s.byte + s.width) return false;
        }
        Frame trial = m->frame;
        if (!encode(trial, s, s.minimum) || !encode(trial, s, s.maximum) ||
            !encode(trial, s, s.initial)) return false;
        signals_[signalCount_] = s; values_[signalCount_++] = s.initial;
        encode(m->frame, s, s.initial); updateChecksum(*m); return true;
    }
    const Signal* signal(const char* name) const {
        for (size_t i = 0; i < signalCount_; ++i)
            if (std::strcmp(signals_[i].name, name) == 0) return &signals_[i];
        return nullptr;
    }
    bool setSignal(const char* name, double value) {
        for (size_t i = 0; i < signalCount_; ++i) {
            if (std::strcmp(signals_[i].name, name)) continue;
            auto* m = find(signals_[i].id);
            if (!encode(m->frame, signals_[i], value)) return false;
            values_[i] = value; updateChecksum(*m); return true;
        }
        return false;
    }
    bool setBatterySOC(double value) { return setSignal("SOC", value); }
    bool setRPM(double value) { return setSignal("RPM", value); }
    bool setMode(Mode mode) {
        if (running_ || hasQueuedPlayback_) return false;
        mode_ = mode; return true;
    }
    bool start(uint64_t now) {
        if (running_) return true;
        if (hasQueuedPlayback_) return false;
        if (mode_ == Mode::Playback && (!recordingCount_ || complete_)) return false;
        running_ = true; startMs_ = now; autoMs_ = now;
        for (size_t i = 0; i < messageCount_; ++i) messages_[i].nextMs = now;
        return true;
    }
    void stop(uint64_t now) {
        if (running_ && mode_ == Mode::Playback) elapsedMs_ += now - startMs_;
        running_ = false;
    }
    bool restart(uint64_t now) {
        if (running_ || hasQueuedPlayback_) return false;
        index_ = 0; elapsedMs_ = 0; cycleMs_ = 0; startMs_ = now; complete_ = false;
        return true;
    }
    // The hardware adapter reports completion before another tick. A cancelled
    // or failed playback frame is retained for an explicit resume/retry.
    void transmitResult(bool success, uint64_t now) {
        if (!hasQueuedPlayback_) return;
        hasQueuedPlayback_ = false;
        if (success) return;
        index_ = queuedIndex_; cycleMs_ = queuedCycleMs_;
        elapsedMs_ = queuedElapsedMs_; startMs_ = now;
        running_ = false; complete_ = false;
    }
    bool load(const RecordedFrame* data, size_t count, uint32_t loopGapMs) {
        if (running_ || hasQueuedPlayback_ || !data || count == 0 || loopGapMs < 1) return false;
        if (data[0].timeMs != 0) return false;
        for (size_t i = 0; i < count; ++i)
            if (!valid(data[i].frame) || (i && data[i].timeMs < data[i-1].timeMs))
                return false;
        recording_ = data; recordingCount_ = count; loopGapMs_ = loopGapMs;
        index_ = 0; elapsedMs_ = 0; cycleMs_ = 0; complete_ = false; return true;
    }
    void tick(uint64_t now, Transport& transport) {
        if (!running_) return;
        if (mode_ == Mode::Playback) {
            if (hasQueuedPlayback_) return;
            const auto elapsed = elapsedMs_ + now - startMs_;
            const auto& record = recording_[index_];
            const uint64_t due = cycleMs_ + record.timeMs;
            if (elapsed < due) return;
            if (!transport.offer(record.frame, now)) return;
            hasQueuedPlayback_ = true; queuedIndex_ = index_;
            queuedCycleMs_ = cycleMs_; queuedElapsedMs_ = elapsed;
            ++queued; if (elapsed > due) ++late;
            if (++index_ == recordingCount_) {
                if (!loop) { complete_ = true; running_ = false; return; }
                // Advance absolute cycle origin; no cumulative drift. At most one
                // frame is offered per tick, even if the recording is overdue.
                cycleMs_ += uint64_t(record.timeMs) + loopGapMs_;
                index_ = 0;
            }
            return;
        }
        if (mode_ == Mode::Auto) {
            const double seconds = double(now - autoMs_) / 1000.0;
            for (size_t i = 0; i < signalCount_; ++i) {
                const auto& s = signals_[i];
                if (s.ratePerSecond == 0) continue;
                // Keep fractional progress independently of quantized wire values.
                double value = values_[i] + s.ratePerSecond * seconds;
                if (value < s.minimum) value = s.minimum;
                if (value > s.maximum) value = s.maximum;
                values_[i] = value;
                auto* m = find(s.id); encode(m->frame, s, value); updateChecksum(*m);
            }
        }
        autoMs_ = now;
        for (size_t n = 0; n < messageCount_; ++n) {
            const size_t i = (cursor_ + n) % messageCount_;
            auto& m = messages_[i];
            if (m.nextMs == 0) m.nextMs = now;
            if (!m.enabled || now < m.nextMs) continue;
            if (!transport.offer(m.frame, now)) return;
            const uint64_t missed = (now - m.nextMs) / m.intervalMs;
            skipped += missed; m.nextMs += (missed + 1) * m.intervalMs;
            cursor_ = (i + 1) % messageCount_; ++queued; return;
        }
    }
    static double decode(const Frame& frame, const Signal& s) {
        uint64_t raw = 0;
        for (unsigned i = 0; i < s.width; ++i) {
            const unsigned pos = s.littleEndian ? s.byte + s.width - 1 - i : s.byte + i;
            raw = (raw << 8) | frame.data[pos];
        }
        int64_t value = static_cast<int64_t>(raw);
        if (s.isSigned && (raw & (uint64_t(1) << (s.width * 8 - 1))))
            value -= int64_t(1) << (s.width * 8);
        return value * s.scale + s.offset;
    }
    bool running() const { return running_; }
    Mode mode() const { return mode_; }
    size_t messageCount() const { return messageCount_; }
    size_t signalCount() const { return signalCount_; }
    const Message& message(size_t i) const { return messages_[i]; }
    const Signal& definition(size_t i) const { return signals_[i]; }
    size_t position() const { return index_; }
    size_t recordingCount() const { return recordingCount_; }
    bool complete() const { return complete_; }
    bool loop = true;
    uint64_t queued = 0, skipped = 0, late = 0;
private:
    void syncSignals(const Message& m) {
        if (m.frame.extended) return;
        for (size_t i = 0; i < signalCount_; ++i)
            if (signals_[i].id == m.frame.id) values_[i] = decode(m.frame, signals_[i]);
    }
    static bool encode(Frame& f, const Signal& s, double value) {
        if (!std::isfinite(value) || value < s.minimum || value > s.maximum) return false;
        const double raw = std::round((value - s.offset) / s.scale);
        const unsigned bits = s.width * 8;
        const int64_t lo = s.isSigned ? -(int64_t(1) << (bits - 1)) : 0;
        const int64_t hi = s.isSigned ? (int64_t(1) << (bits - 1)) - 1 :
            (int64_t(1) << bits) - 1;
        if (!std::isfinite(raw) || raw < double(lo) || raw > double(hi)) return false;
        uint64_t number = static_cast<uint64_t>(static_cast<int64_t>(raw));
        for (unsigned i = 0; i < s.width; ++i) {
            const unsigned pos = s.littleEndian ? s.byte + i : s.byte + s.width - 1 - i;
            f.data[pos] = static_cast<uint8_t>(number >> (8 * i));
        }
        return true;
    }
    static void updateChecksum(Message& m) {
        if (m.checksum == Checksum::None) return;
        uint32_t sum = m.frame.id + m.frame.dlc;
        for (unsigned i = 0; i < 7; ++i) sum += m.frame.data[i];
        m.frame.data[7] = static_cast<uint8_t>(sum);
    }
    Message messages_[MaxMessages] = {};
    Signal signals_[MaxSignals] = {};
    double values_[MaxSignals] = {};
    size_t messageCount_ = 0, signalCount_ = 0, cursor_ = 0;
    Mode mode_ = Mode::Manual;
    bool running_ = false, complete_ = false;
    bool hasQueuedPlayback_ = false;
    const RecordedFrame* recording_ = nullptr;
    size_t recordingCount_ = 0, index_ = 0;
    uint32_t loopGapMs_ = 1;
    uint64_t startMs_ = 0, elapsedMs_ = 0, cycleMs_ = 0, autoMs_ = 0;
    size_t queuedIndex_ = 0;
    uint64_t queuedCycleMs_ = 0, queuedElapsedMs_ = 0;
};
} // namespace cansim
