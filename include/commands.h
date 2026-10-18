#pragma once
#include "can_core.h"
#include <cstdlib>
#include <cerrno>

namespace cansim {
inline bool unsignedNumber(const char* s, uint32_t& value) {
    if (!s || !*s) return false;
    unsigned base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (!*s) return false;
    uint64_t n = 0;
    for (; *s; ++s) {
        unsigned digit = *s >= '0' && *s <= '9' ? *s - '0' :
            (*s >= 'a' && *s <= 'f' ? *s - 'a' + 10 :
             (*s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : 99));
        if (digit >= base) return false;
        n = n * base + digit; if (n > 0xFFFFFFFFULL) return false;
    }
    value = static_cast<uint32_t>(n); return true;
}
inline bool physicalNumber(const char* s, double& value) {
    if (!s || !*s) return false;
    errno = 0; char* end = nullptr; value = std::strtod(s, &end);
    return end && !*end && errno != ERANGE && std::isfinite(value);
}
// Host supplies clock(), start(), stop(), recover(), configure(), status(), list(),
// monitor(), filter(), reply(). No Arduino dependency, heap allocation or waiting.
template<class Host> class Commands {
public:
    Commands(Simulator& sim, Host& host) : sim_(sim), host_(host) {}
    void feed(char c) {
        if (c == '\r' || c == '\n') {
            if (c == '\n' && lastCr_) { lastCr_ = false; return; }
            lastCr_ = c == '\r';
            if (overflow_) host_.reply("ERR line too long or invalid control character");
            else if (length_) { line_[length_] = 0; execute(line_); }
            overflow_ = false; length_ = 0; return;
        }
        lastCr_ = false;
        if ((static_cast<unsigned char>(c) < 32 && c != '\t') ||
            static_cast<unsigned char>(c) > 126) { overflow_ = true; return; }
        if (length_ == sizeof(line_) - 1) { overflow_ = true; return; }
        if (!overflow_) line_[length_++] = c;
    }
    void execute(char* line) {
        char* args[16]; size_t count = 0;
        char* p = line;
        while (*p) {
            while (*p == ' ' || *p == '\t') ++p;
            if (!*p) break;
            if (count == 16) { host_.reply("ERR too many arguments"); return; }
            args[count++] = p;
            while (*p && *p != ' ' && *p != '\t') ++p;
            if (*p) *p++ = 0;
        }
        if (!count) return;
        const auto is = [&](const char* cmd) { return std::strcmp(args[0], cmd) == 0; };
        const auto equals = [](const char* a, const char* b) { return std::strcmp(a, b) == 0; };
        bool ok = false;
        uint32_t id = 0, a = 0, b = 0; double value = 0;
        if (is("HELP") && count == 1) {
            host_.reply("OK commands: START STOP STATUS LIST MODE SET SET_SIGNAL SET_SOC SET_RPM FRAME ADD ENABLE INTERVAL PLAY PAUSE RESTART LOOP MONITOR FILTER CONFIG RECOVER"); return;
        }
        if (is("STATUS") && count == 1) { host_.status(); return; }
        if (is("LIST") && count == 1) { host_.list(); return; }
        if (is("START") && count == 1) {
            if (sim_.mode() != Mode::Playback && sim_.messageCount() == 0) {
                host_.reply("ERR no cyclic messages configured; ADD a raw test message or PLAY the recording"); return;
            }
            ok = host_.start();
        }
        else if (is("STOP") && count == 1) { host_.stop(); ok = true; }
        else if (is("PAUSE") && count == 1) { host_.pause(); ok = true; }
        else if (is("RECOVER") && count == 1) ok = host_.recover();
        else if (is("MODE") && count == 2) {
            if (equals(args[1], "AUTO")) ok = sim_.setMode(Mode::Auto);
            else if (equals(args[1], "MANUAL")) ok = sim_.setMode(Mode::Manual);
            else if (equals(args[1], "PLAYBACK")) ok = sim_.setMode(Mode::Playback);
        }
        else if (is("PLAY") && count == 1) {
            if (sim_.mode() == Mode::Playback) ok = host_.start();
            else if (sim_.setMode(Mode::Playback)) ok = host_.start();
        }
        else if (is("RESTART") && count == 1) {
            if (!sim_.running() && sim_.mode() == Mode::Playback && host_.idle()) {
                ok = sim_.restart(host_.clock());
            }
        }
        else if (is("LOOP") && count == 2) {
            if (equals(args[1], "ON") || equals(args[1], "OFF")) {
                sim_.loop = equals(args[1], "ON"); ok = true;
            }
        }
        else if (is("SET") && count == 4) {
            ok = unsignedNumber(args[1], id) && unsignedNumber(args[2], a) &&
                unsignedNumber(args[3], b) && sim_.setByte(id, a, b);
        }
        else if (is("SET_SIGNAL") && count == 3) {
            ok = physicalNumber(args[2], value) && sim_.setSignal(args[1], value);
            if (!sim_.signal(args[1])) { host_.reply("ERR signal has no verified profile definition; see LIST"); return; }
        }
        else if ((is("SET_SOC") || is("SET_RPM")) && count == 2) {
            const char* name = is("SET_SOC") ? "SOC" : "RPM";
            if (!sim_.signal(name)) { host_.reply("ERR signal has no verified profile definition; see LIST"); return; }
            ok = physicalNumber(args[1], value) && sim_.setSignal(name, value);
        }
        else if (is("INTERVAL") && count == 3) {
            ok = unsignedNumber(args[1], id) && unsignedNumber(args[2], a) && sim_.setInterval(id, a);
        }
        else if (is("ENABLE") && count == 3) {
            if (unsignedNumber(args[1], id) && (equals(args[2], "ON") || equals(args[2], "OFF"))) {
                auto* m = sim_.find(id);
                if (m) { m->enabled = equals(args[2], "ON"); m->nextMs = 0; ok = true; }
            }
        }
        else if (is("FRAME") || is("ADD")) {
            const size_t offset = is("ADD") ? 4 : 3;
            Frame f;
            ok = count >= offset && unsignedNumber(args[1], id) && id <= 0x7FF;
            if (ok && is("ADD")) ok = unsignedNumber(args[2], a) && a >= 1 && a <= Simulator::MaxInterval;
            if (ok) ok = unsignedNumber(args[offset - 1], b) && b <= 8 && count == offset + b;
            if (ok) {
                f.id = id; f.dlc = static_cast<uint8_t>(b);
                for (size_t i = 0; i < f.dlc; ++i) {
                    uint32_t byte = 0;
                    if (!unsignedNumber(args[offset + i], byte) || byte > 255) { ok = false; break; }
                    f.data[i] = static_cast<uint8_t>(byte);
                }
            }
            if (ok) ok = is("ADD") ? sim_.add(f, a) : sim_.setMessage(f);
        }
        else if (is("MONITOR") && (count == 2 || count == 3)) {
            const char* target = count == 2 ? "ALL" : args[1];
            const char* on = args[count - 1];
            if ((equals(target, "RX") || equals(target, "TX") || equals(target, "ALL")) &&
                (equals(on, "ON") || equals(on, "OFF"))) {
                host_.monitor(target, equals(on, "ON")); ok = true;
            }
        }
        else if (is("FILTER") && count == 2) {
            if (equals(args[1], "OFF")) { host_.filter(false, 0); ok = true; }
            else if (unsignedNumber(args[1], id) && id <= 0x1FFFFFFF) { host_.filter(true, id); ok = true; }
        }
        else if (is("CONFIG") && count == 4) {
            if (unsignedNumber(args[1], a) && unsignedNumber(args[2], b) &&
                (equals(args[3], "NORMAL") || equals(args[3], "LOOPBACK")))
                ok = host_.configure(a, b, equals(args[3], "LOOPBACK"));
        }
        else { host_.reply("ERR unknown command or wrong argument count; HELP"); return; }
        host_.reply(ok ? "OK" : "ERR invalid value, state, message or CAN configuration; STOP/STATUS/LIST");
    }
private:
    Simulator& sim_;
    Host& host_;
    char line_[192] = {};
    size_t length_ = 0;
    bool overflow_ = false, lastCr_ = false;
};
}
