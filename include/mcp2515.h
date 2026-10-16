#pragma once
#include "can_core.h"

namespace cansim {
struct BitTiming { uint8_t cnf1 = 0, cnf2 = 0, cnf3 = 0; unsigned samplePermille = 0; };
inline bool bitTiming(unsigned oscillator, unsigned bitrate, BitTiming& out) {
    if ((oscillator != 8000000 && oscillator != 16000000 && oscillator != 20000000) ||
        bitrate < 10000 || bitrate > 1000000) return false;
    unsigned best = 0xFFFFFFFFU;
    const unsigned target = bitrate > 500000 ? 800 : 875;
    for (unsigned brp = 1; brp <= 64; ++brp)
        for (unsigned prop = 1; prop <= 8; ++prop)
            for (unsigned ps1 = 1; ps1 <= 8; ++ps1)
                for (unsigned ps2 = 2; ps2 <= 8; ++ps2) {
                    const unsigned tq = 1 + prop + ps1 + ps2;
                    if (tq < 8 || prop + ps1 < ps2 ||
                        uint64_t(bitrate) * 2 * brp * tq != oscillator) continue;
                    const unsigned point = (1 + prop + ps1) * 1000 / tq;
                    const unsigned error = point > target ? point - target : target - point;
                    const unsigned score = error * 32 + (25 - tq);
                    if (score >= best) continue;
                    best = score; out.cnf1 = brp - 1; // SJW = 1 TQ
                    out.cnf2 = 0x80 | ((ps1 - 1) << 3) | (prop - 1);
                    out.cnf3 = ps2 - 1; out.samplePermille = point;
                }
    return best != 0xFFFFFFFFU;
}

// Register I/O is injected so register behavior can be tested without Arduino.
// IO: reset(), read(addr), readBlock(addr,buf,n), write(addr,val),
// writeBlock(addr,buf,n), modify(addr,mask,value), waitUs(us).
template<class IO> class Mcp2515 : public Transport {
public:
    explicit Mcp2515(IO& io) : io_(io) {}
    bool begin(unsigned oscillator, unsigned bitrate, bool loopback = false) {
        ready_ = false; pending_ = false; fault_ = false;
        if (!bitTiming(oscillator, bitrate, timing)) return false;
        io_.reset(); io_.waitUs(10000);
        if (!mode(0x80)) return false;
        io_.write(0x2A, timing.cnf1); io_.write(0x29, timing.cnf2);
        io_.write(0x28, timing.cnf3);
        // Masks zero: accept all valid standard/extended IDs. Rollover RX0 -> RX1.
        for (unsigned a = 0x20; a <= 0x27; ++a) io_.write(a, 0);
        io_.write(0x60, 0x04); io_.write(0x70, 0);
        io_.write(0x2B, 0); io_.write(0x2C, 0); io_.write(0x2D, 0);
        io_.write(0x30, 0); io_.write(0x40, 0); io_.write(0x50, 0);
        loopback_ = loopback;
        ready_ = mode(loopback ? 0x40 : 0x60); // loopback or listen-only
        return ready_;
    }
    bool activate() {
        if (!ready_ || fault_ || pending_) return false;
        return mode(loopback_ ? 0x40 : 0x00);
    }
    bool listen() {
        if (!ready_ || pending_) return false;
        return mode(loopback_ ? 0x40 : 0x60);
    }
    bool offer(const Frame& f, uint64_t now) override {
        if (!ready_ || fault_ || pending_ || !valid(f) || (io_.read(0x30) & 8)) return false;
        const uint8_t currentMode = io_.read(0x0E) & 0xE0;
        if (currentMode != 0 && currentMode != 0x40) return false;
        uint8_t bytes[13] = {};
        pack(f, bytes);
        io_.modify(0x2C, 0x04, 0);
        io_.writeBlock(0x31, bytes, sizeof(bytes));
        pendingFrame_ = f; sinceMs_ = now; pending_ = true; cancelling_ = false;
        io_.modify(0x30, 8, 8); ++queued; return true;
    }
    // Returns 1 for a completed acknowledged transmission, -1 for a failed or
    // cancelled transmission, 0 if no completion. One buffer preserves replay order.
    int service(uint64_t now, Frame& completed, unsigned timeoutMs) {
        if (!ready_) return 0;
        errorFlags = io_.read(0x2D); tec = io_.read(0x1C); rec = io_.read(0x1D);
        if (errorFlags & 0xC0) {
            if (errorFlags & 0x40) ++overruns;
            if (errorFlags & 0x80) ++overruns;
            io_.modify(0x2D, 0xC0, 0);
        }
        if (errorFlags & 0x20) fault_ = true;
        if (!pending_) return 0;
        const uint8_t ctrl = io_.read(0x30), flags = io_.read(0x2C);
        if (!(ctrl & 8)) {
            completed = pendingFrame_; pending_ = false;
            io_.modify(0x2C, 0x04, 0);
            if (flags & 0x04) { ++sent; return 1; }
            ++failed; return -1;
        }
        if (!cancelling_ && (fault_ || now - sinceMs_ >= timeoutMs)) {
            cancelling_ = true; io_.modify(0x30, 8, 0);
            // Halt on timeout; explicit RECOVER is required before sending again.
            fault_ = true;
        }
        if (cancelling_ && now - sinceMs_ >= uint64_t(timeoutMs) * 2) {
            // A disconnected/stuck SPI controller must not make RECOVER
            // permanently unavailable by holding software pending forever.
            completed = pendingFrame_; pending_ = false; ready_ = false;
            fault_ = true; ++failed; return -1;
        }
        return 0;
    }
    void abort() {
        if (pending_) { cancelling_ = true; io_.modify(0x30, 8, 0); }
    }
    bool receive(Frame& f) {
        if (!ready_) return false;
        const uint8_t flags = io_.read(0x2C);
        const uint8_t bit = flags & 1 ? 1 : (flags & 2 ? 2 : 0);
        if (!bit) return false;
        const uint8_t base = bit == 1 ? 0x60 : 0x70;
        const uint8_t ctrl = io_.read(base);
        uint8_t bytes[13]; io_.readBlock(base + 1, bytes, sizeof(bytes));
        io_.modify(0x2C, bit, 0);
        f = {}; f.extended = bytes[1] & 8;
        const uint32_t sid = (uint32_t(bytes[0]) << 3) | (bytes[1] >> 5);
        f.id = f.extended ? (sid << 18) | (uint32_t(bytes[1] & 3) << 16) |
            (uint32_t(bytes[2]) << 8) | bytes[3] : sid;
        f.dlc = bytes[4] & 15;
        f.rtr = f.extended ? (bytes[4] & 0x40) : (ctrl & 8);
        if (!valid(f)) { ++invalidRx; return false; }
        if (!f.rtr) std::memcpy(f.data, bytes + 5, f.dlc);
        ++received; return true;
    }
    bool pending() const { return pending_; }
    bool fault() const { return fault_; }
    bool ready() const { return ready_; }
    bool loopback() const { return loopback_; }
    uint64_t queued = 0, sent = 0, failed = 0, received = 0, overruns = 0, invalidRx = 0;
    uint8_t errorFlags = 0, tec = 0, rec = 0;
    BitTiming timing;
    static void pack(const Frame& f, uint8_t* bytes) {
        const uint32_t sid = f.extended ? f.id >> 18 : f.id;
        bytes[0] = sid >> 3; bytes[1] = (sid & 7) << 5;
        bytes[2] = bytes[3] = 0;
        if (f.extended) {
            bytes[1] |= 8 | ((f.id >> 16) & 3);
            bytes[2] = f.id >> 8; bytes[3] = f.id;
        }
        bytes[4] = f.dlc | (f.rtr ? 0x40 : 0);
        std::memcpy(bytes + 5, f.data, 8);
    }
private:
    bool mode(uint8_t requested) {
        io_.modify(0x0F, 0xE0, requested);
        for (unsigned i = 0; i < 100; ++i) {
            if ((io_.read(0x0E) & 0xE0) == requested) return true;
            io_.waitUs(100);
        }
        return false;
    }
    IO& io_;
    bool ready_ = false, pending_ = false, fault_ = false, loopback_ = false;
    bool cancelling_ = false;
    uint64_t sinceMs_ = 0;
    Frame pendingFrame_;
};
}
