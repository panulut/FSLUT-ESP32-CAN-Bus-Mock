// PC demo adapter. Uses the firmware's unchanged Simulator and Commands classes.
// CAN acknowledgement and reception here are SOFTWARE events, not real hardware.
#include "can_core.h"
#include "commands.h"
#include "mcp2515.h"
#include "vehicle_profile.h"
#include "recording.inc"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>

using namespace cansim;
std::string quote(const std::string& input) {
    std::ostringstream out; out << '"';
    for (unsigned char c : input) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"'; return out.str();
}
std::string hexId(uint32_t id) {
    std::ostringstream out; out << "0x" << std::uppercase << std::hex << id; return out.str();
}
std::string bytes(const Frame& f) {
    std::ostringstream out;
    if (!f.rtr) for (unsigned i = 0; i < f.dlc; ++i) {
        if (i) out << ' ';
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << unsigned(f.data[i]);
    }
    return out.str();
}
struct Event {
    std::string kind, text;
    uint64_t time = 0;
    Frame frame;
    bool hasFrame = false;
};
class Demo : public Transport {
public:
    Demo() { reset(false); }
    bool offer(const Frame& frame, uint64_t time) override {
        if (pending || fault) return false;
        pending = true; pendingFrame = frame; pendingAt = time; ++txQueued;
        return true;
    }
    void frameEvent(const char* kind, const Frame& frame) {
        if (filterEnabled && filterId != frame.id) return;
        if (events.size() < 500) events.push_back({kind, "", now, frame, true});
    }
    void reply(const char* text) {
        if (events.size() < 500) events.push_back({"reply", text, now, {}, false});
    }
    uint64_t clock() const { return now; }
    bool start() { return !fault && sim.start(now); }
    void stop() {
        sim.stop(now);
        if (pending) {
            pending = false; ++txFailed; sim.transmitResult(false, now);
            reply("INFO pending software transmission cancelled");
        }
    }
    void pause() { sim.stop(now); }
    bool idle() const { return !pending; }
    bool recover() {
        if (sim.running() || pending) return false;
        fault = false; return true;
    }
    bool configure(uint32_t bitrate, uint32_t oscillator, bool loopback) {
        BitTiming timing;
        if (sim.running() || pending || !bitTiming(oscillator, bitrate, timing)) return false;
        loopback_ = loopback; fault = false; return true;
    }
    void monitor(const char* target, bool on) {
        if (!std::strcmp(target, "ALL") || !std::strcmp(target, "TX")) monitorTx = on;
        if (!std::strcmp(target, "ALL") || !std::strcmp(target, "RX")) monitorRx = on;
    }
    void filter(bool enabled, uint32_t id) { filterEnabled = enabled; filterId = id; }
    void status() { reply("OK state and counters are shown in the dashboard"); }
    void list() {
        for (size_t i = 0; i < sim.messageCount(); ++i) {
            const auto& m = sim.message(i);
            reply(("MSG " + hexId(m.frame.id) + " DLC=" + std::to_string(m.frame.dlc) +
                   " interval_ms=" + std::to_string(m.intervalMs)).c_str());
        }
        reply("OK END LIST");
    }
    void reset(bool synthetic) {
        sim = Simulator{}; pending = fault = false; ack = true; loopback_ = false;
        synthetic_ = synthetic; monitorRx = monitorTx = true; filterEnabled = false;
        txQueued = txOk = txFailed = rx = 0; events.clear();
        bool valid = loadVehicleProfile(sim) && sim.load(recording::frames, recording::count, recording::loopGapMs);
        if (synthetic) {
            valid = valid && sim.add({0x100, 4, {}}, 100) &&
                sim.addSignal({0x100, "RPM", 0, 2, true, false, 1, 0, 0, 6000, 0, 600}) &&
                sim.addSignal({0x100, "SOC", 2, 2, true, false, 0.1, 0, 0, 100, 80, -0.5}) &&
                sim.setMode(Mode::Auto);
        }
        if (!valid) { fault = true; reply("ERR demo profile initialization failed"); }
    }
    void advance(uint32_t milliseconds) {
        for (uint32_t i = 0; i < milliseconds; ++i) {
            ++now;
            if (pending) {
                if ((ack || loopback_) && now > pendingAt) {
                    pending = false; ++txOk; sim.transmitResult(true, now);
                    if (monitorTx) frameEvent("tx", pendingFrame);
                    if (loopback_) { ++rx; if (monitorRx) frameEvent("rx", pendingFrame); }
                } else if (now - pendingAt >= 100) {
                    pending = false; fault = true; ++txFailed;
                    sim.transmitResult(false, now); sim.stop(now);
                    reply("ERR software ACK missing for 100 ms; simulation stopped. RECOVER to retry.");
                }
            }
            sim.tick(now, *this);
        }
    }
    void command(const std::string& command) {
        if (command == "@PROFILE VEHICLE") { reset(false); reply("OK vehicle profile loaded; stopped"); }
        else if (command == "@PROFILE SYNTHETIC") { reset(true); reply("OK synthetic test profile loaded; not a vehicle protocol"); }
        else if (command == "@ACK ON") { ack = true; reply("OK software ACK enabled"); }
        else if (command == "@ACK OFF") { ack = false; reply("OK software ACK disabled (normal mode only)"); }
        else if (command == "@RX") {
            Frame f{0x123, 8, {0,0x11,0x22,0x33,0x44,0x55,0x66,0x77}};
            ++rx; if (monitorRx) frameEvent("rx", f); reply("OK injected one software RX frame");
        } else if (command.rfind("@STEP ", 0) == 0) {
            uint32_t value = 0;
            if (!unsignedNumber(command.c_str() + 6, value) || value > 60000) reply("ERR step must be 0..60000 ms");
            else advance(value);
        } else {
            Commands<Demo> parser(sim, *this);
            for (char c : command) parser.feed(c);
            parser.feed('\n');
        }
    }
    void snapshot(std::ostream& out) {
        out << std::boolalpha << std::setprecision(12);
        out << "{\"time_ms\":" << now << ",\"running\":" << sim.running()
            << ",\"mode\":" << quote(sim.mode() == Mode::Manual ? "MANUAL" : sim.mode() == Mode::Auto ? "AUTO" : "PLAYBACK")
            << ",\"profile\":" << quote(synthetic_ ? "synthetic" : "vehicle")
            << ",\"fault\":" << fault << ",\"ack\":" << ack << ",\"loopback\":" << loopback_
            << ",\"pending\":" << pending << ",\"loop\":" << sim.loop
            << ",\"position\":" << sim.position() << ",\"count\":" << sim.recordingCount()
            << ",\"complete\":" << sim.complete() << ",\"queued\":" << txQueued
            << ",\"sent\":" << txOk << ",\"failed\":" << txFailed << ",\"rx\":" << rx
            << ",\"late\":" << sim.late << ",\"skipped\":" << sim.skipped << ",\"signals\":[";
        for (size_t i = 0; i < sim.signalCount(); ++i) {
            const auto& s = sim.definition(i);
            if (i) out << ',';
            out << "{\"name\":" << quote(s.name) << ",\"value\":" << Simulator::decode(sim.find(s.id)->frame, s)
                << ",\"min\":" << s.minimum << ",\"max\":" << s.maximum << '}';
        }
        out << "],\"messages\":[";
        for (size_t i = 0; i < sim.messageCount(); ++i) {
            const auto& m = sim.message(i);
            if (i) out << ',';
            out << "{\"id\":" << quote(hexId(m.frame.id)) << ",\"dlc\":" << unsigned(m.frame.dlc)
                << ",\"data\":" << quote(bytes(m.frame)) << ",\"interval\":" << m.intervalMs
                << ",\"enabled\":" << m.enabled << '}';
        }
        out << "],\"events\":[";
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& e = events[i]; if (i) out << ',';
            out << "{\"kind\":" << quote(e.kind) << ",\"time_ms\":" << e.time;
            if (e.hasFrame) {
                out << ",\"id\":" << quote(hexId(e.frame.id)) << ",\"dlc\":" << unsigned(e.frame.dlc)
                    << ",\"data\":" << quote(bytes(e.frame)) << ",\"extended\":" << e.frame.extended
                    << ",\"rtr\":" << e.frame.rtr;
            } else out << ",\"text\":" << quote(e.text);
            out << '}';
        }
        out << "]}\n"; out.flush(); events.clear();
    }
private:
    Simulator sim;
    uint64_t now = 0, pendingAt = 0, txQueued = 0, txOk = 0, txFailed = 0, rx = 0;
    bool pending = false, fault = false, ack = true, synthetic_ = false, loopback_ = false;
    bool monitorRx = true, monitorTx = true, filterEnabled = false;
    uint32_t filterId = 0;
    Frame pendingFrame;
    std::vector<Event> events;
};
int main() {
    Demo demo; std::string line;
    while (std::getline(std::cin, line)) { demo.command(line); demo.snapshot(std::cout); }
}
