#include "can_core.h"
#include "commands.h"
#include "mcp2515.h"
#include "output_queue.h"
#include "vehicle_profile.h"
#include "recording.inc"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace cansim;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

struct FakeTransport : Transport {
    bool accept = true;
    std::vector<Frame> frames;
    std::vector<uint64_t> times;
    bool offer(const Frame& f, uint64_t now) override {
        if (!accept) return false;
        frames.push_back(f); times.push_back(now); return true;
    }
};

void encoding() {
    Simulator sim;
    CHECK(sim.add({0x100, 8, {}}, 100, Checksum::SumIdDlc));
    CHECK(sim.addSignal({0x100, "CURRENT", 0, 2, false, true, 0.1, 0, -3276.8, 3276.7, -12.3, 0}));
    CHECK(sim.addSignal({0x100, "VOLTAGE", 2, 2, true, false, 0.1, 0, 0, 6553.5, 350, 0}));
    CHECK(sim.addSignal({0x100, "SOC", 4, 1, false, false, 0.5, 0, 0, 100, 75.5, 0}));
    auto* m = sim.find(0x100);
    CHECK(m->frame.data[0] == 0xFF && m->frame.data[1] == 0x85);
    CHECK(m->frame.data[2] == 0xAC && m->frame.data[3] == 0x0D);
    CHECK(m->frame.data[4] == 151);
    // Independent expected sum: 0x100 + 8 + 255 + 133 + 172 + 13 + 151 = 988.
    CHECK(m->frame.data[7] == 220);
    CHECK(sim.setBatterySOC(0)); CHECK(m->frame.data[4] == 0);
    const Frame before = m->frame;
    CHECK(!sim.setBatterySOC(100.1)); CHECK(!sim.setBatterySOC(NAN));
    CHECK(!std::memcmp(before.data, m->frame.data, 8));
    CHECK(!sim.setByte(0x100, 7, 0)); CHECK(!sim.setByte(0x100, 8, 0));
    CHECK(!sim.addSignal({0x100, "OVERLAP", 0, 1, false, false, 1, 0, 0, 255, 0, 0}));
    CHECK(!sim.addSignal({0x100, "CHECKSUM", 7, 1, false, false, 1, 0, 0, 255, 0, 0}));
    CHECK(sim.setSignal("CURRENT", -3276.8));
    CHECK(m->frame.data[0] == 0x80 && m->frame.data[1] == 0);
    CHECK(std::fabs(Simulator::decode(m->frame, sim.definition(0)) + 3276.8) < 0.00001);
    CHECK(!sim.setMessage({0x100, 7, {}}));
    CHECK(!sim.setRPM(100));
    Simulator wide;
    CHECK(wide.add({0x101, 8, {}}, 1));
    CHECK(wide.addSignal({0x101, "U32", 0, 4, false, false, 1, 0, 0, 4294967295.0, 4294967295.0, 0}));
    CHECK(wide.addSignal({0x101, "S32", 4, 4, true, true, 1, 0, -2147483648.0, 2147483647.0, -2147483648.0, 0}));
    CHECK(wide.find(0x101)->frame.data[7] == 0x80);
    CHECK(Simulator::decode(wide.find(0x101)->frame, wide.definition(0)) == 4294967295.0);
}

void cyclicAndAuto() {
    Simulator sim; FakeTransport bus;
    CHECK(sim.add({0x100, 2, {}}, 100));
    CHECK(sim.add({0x101, 1, {}}, 100));
    CHECK(sim.addSignal({0x100, "RPM", 0, 2, true, false, 1, 0, 0, 6000, 0, 1000}));
    CHECK(sim.setMode(Mode::Auto)); CHECK(sim.start(4294967290ULL));
    CHECK(!sim.setMode(Mode::Manual));
    sim.tick(4294967290ULL, bus); sim.tick(4294967290ULL, bus);
    CHECK(bus.frames.size() == 2 && bus.frames[0].id != bus.frames[1].id);
    sim.tick(4294967340ULL, bus); CHECK(bus.frames.size() == 2);
    sim.tick(4294967390ULL, bus); CHECK(bus.frames.size() == 3);
    CHECK(Simulator::decode(sim.find(0x100)->frame, sim.definition(0)) == 100);
    sim.tick(4294974290ULL, bus);
    CHECK(Simulator::decode(sim.find(0x100)->frame, sim.definition(0)) == 6000);
    CHECK(sim.skipped > 0);
    bus.accept = false;
    const auto queued = sim.queued;
    sim.tick(4294974390ULL, bus); CHECK(sim.queued == queued);
    sim.stop(4294974390ULL); sim.tick(4294974490ULL, bus); CHECK(sim.queued == queued);
    CHECK(sim.setMode(Mode::Manual));
    CHECK(!sim.setInterval(0x100, 0)); CHECK(sim.setInterval(0x100, 1));
    CHECK(sim.start(4294974490ULL)); bus.accept = true; sim.tick(4294974490ULL, bus);
    CHECK(sim.queued == queued + 1);

    Simulator fractional; FakeTransport b;
    CHECK(fractional.add({1, 1, {}}, 1));
    CHECK(fractional.addSignal({1, "SOC", 0, 1, false, false, 1, 0, 0, 100, 100, -1}));
    CHECK(fractional.setMode(Mode::Auto)); CHECK(fractional.start(0));
    for (uint64_t t = 0; t <= 1000; ++t) fractional.tick(t, b);
    CHECK(fractional.find(1)->frame.data[0] == 99);
    CHECK(fractional.setByte(1, 0, 50));
    fractional.tick(2000, b); CHECK(fractional.find(1)->frame.data[0] == 49);
}

void playback() {
    const RecordedFrame frames[] = {{0, {1, 1, {1}}}, {10, {2, 7, {2}}}, {10, {3, 0, {}}}};
    Simulator sim; FakeTransport bus;
    const auto tick = [&](uint64_t now) { sim.tick(now, bus); sim.transmitResult(true, now); };
    CHECK(sim.load(frames, 3, 5)); CHECK(sim.setMode(Mode::Playback));
    CHECK(sim.start(100)); tick(100); tick(109); CHECK(bus.frames.size() == 1);
    sim.stop(109); CHECK(sim.start(1000)); tick(1000); CHECK(bus.frames.size() == 1);
    bus.accept = false; tick(1001); CHECK(sim.position() == 1);
    bus.accept = true; tick(1001); tick(1001);
    CHECK(bus.frames.size() == 3 && bus.frames[1].id == 2 && bus.frames[2].id == 3);
    tick(1005); CHECK(bus.frames.size() == 3);
    tick(1006); CHECK(bus.frames.size() == 4 && bus.frames.back().id == 1);
    // loop origin remained 15 ms, despite a pause of 891 ms.
    sim.stop(1007); CHECK(sim.restart(2000)); sim.loop = false; CHECK(sim.start(2000));
    tick(2000); tick(2010); tick(2010);
    CHECK(sim.complete() && !sim.running()); CHECK(!sim.start(3000));
    CHECK(sim.restart(3000)); CHECK(sim.start(3000));
    CHECK(!sim.load(frames, 3, 1));
    sim.stop(3000);
    const RecordedFrame unordered[] = {{0, {1, 0, {}}}, {10, {1, 0, {}}}, {9, {1, 0, {}}}};
    CHECK(!sim.load(unordered, 3, 1)); CHECK(!sim.load(frames, 0, 1));

    Simulator actual; FakeTransport transport;
    CHECK(actual.load(recording::frames, recording::count, recording::loopGapMs));
    CHECK(actual.setMode(Mode::Playback)); actual.loop = false; CHECK(actual.start(0));
    for (size_t i = 0; i < recording::count; ++i) {
        actual.tick(recording::frames[i].timeMs, transport);
        actual.transmitResult(true, recording::frames[i].timeMs);
    }
    CHECK(transport.frames.size() == recording::count);
    for (size_t i = 0; i < recording::count; ++i) {
        CHECK(transport.times[i] == recording::frames[i].timeMs);
        CHECK(transport.frames[i].dlc == recording::frames[i].frame.dlc);
        CHECK(!std::memcmp(transport.frames[i].data, recording::frames[i].frame.data, 8));
    }
    Simulator retry; FakeTransport tx;
    CHECK(retry.load(frames, 3, 5)); CHECK(retry.setMode(Mode::Playback)); retry.loop = false;
    CHECK(retry.start(0)); retry.tick(0, tx);
    retry.tick(10, tx); CHECK(tx.frames.size() == 1); // no second offer before completion
    retry.stop(10); CHECK(!retry.restart(10)); CHECK(!retry.setMode(Mode::Manual));
    retry.transmitResult(false, 11); CHECK(retry.position() == 0 && !retry.running());
    CHECK(retry.start(100)); retry.tick(100, tx); CHECK(tx.frames.back().id == 1);
    retry.transmitResult(true, 100); retry.tick(110, tx); retry.transmitResult(true, 110);
    retry.tick(110, tx); CHECK(retry.complete());
    retry.transmitResult(false, 111); CHECK(!retry.complete() && retry.position() == 2);
    CHECK(retry.start(200)); retry.tick(200, tx); CHECK(tx.frames.back().id == 3);
    retry.transmitResult(true, 200); CHECK(retry.complete());
}

struct RegisterIO {
    uint8_t regs[128] = {};
    bool changeModes = true, ignoreAbort = false;
    void reset() { std::memset(regs, 0, sizeof(regs)); regs[0x0E] = 0x80; }
    void waitUs(unsigned) {}
    uint8_t read(uint8_t address) { return regs[address]; }
    void readBlock(uint8_t a, uint8_t* b, size_t n) { std::memcpy(b, regs+a, n); }
    void write(uint8_t a, uint8_t v) { regs[a] = v; }
    void writeBlock(uint8_t a, const uint8_t* b, size_t n) { std::memcpy(regs+a, b, n); }
    void modify(uint8_t a, uint8_t mask, uint8_t value) {
        if (ignoreAbort && a == 0x30 && mask == 8 && value == 0) return;
        regs[a] = (regs[a] & ~mask) | (value & mask);
        if (a == 0x0F && changeModes) regs[0x0E] = regs[a] & 0xE0;
    }
};

void driver() {
    BitTiming bt;
    for (unsigned osc : {8000000U, 16000000U, 20000000U})
        for (unsigned bitrate : {10000U, 20000U, 50000U, 100000U, 125000U, 250000U, 500000U, 1000000U}) {
            const bool ok = bitTiming(osc, bitrate, bt);
            if (osc == 8000000 && bitrate == 1000000) { CHECK(!ok); continue; }
            CHECK(ok);
            const unsigned tq = 1 + ((bt.cnf2 & 7) + 1) + (((bt.cnf2 >> 3) & 7) + 1) + ((bt.cnf3 & 7) + 1);
            CHECK(osc == bitrate * 2 * ((bt.cnf1 & 63) + 1) * tq);
            CHECK((bt.cnf3 & 7) >= 1);
        }
    CHECK(!bitTiming(12345678, 500000, bt)); CHECK(!bitTiming(16000000, 123456, bt));
    RegisterIO io; Mcp2515<RegisterIO> can(io);
    CHECK(can.begin(16000000, 1000000)); CHECK((io.regs[0x0E] & 0xE0) == 0x60);
    Frame f{0x301, 7, {1,2,3,4,5,6,7}};
    CHECK(!can.offer(f, 0)); CHECK(can.activate()); CHECK(can.offer(f, 0));
    CHECK(!can.offer(f, 1)); CHECK(io.regs[0x35] == 7);
    CHECK(io.regs[0x31] == 0x60 && io.regs[0x32] == 0x20); // standard 0x301
    Frame complete;
    CHECK(can.service(1, complete, 100) == 0 && can.sent == 0);
    io.regs[0x30] &= ~8; io.regs[0x2C] |= 4;
    CHECK(can.service(2, complete, 100) == 1 && can.sent == 1 && complete.dlc == 7);
    CHECK(can.offer(f, 3)); CHECK(can.service(103, complete, 100) == 0);
    CHECK(can.fault()); CHECK(can.service(104, complete, 100) == -1 && can.failed == 1);
    CHECK(!can.offer(f, 105)); CHECK(can.begin(16000000, 500000, true)); CHECK(can.activate());
    CHECK(can.offer(f, 106)); can.abort(); CHECK(can.service(107, complete, 100) == -1);
    CHECK(can.begin(16000000, 500000)); CHECK(can.activate()); CHECK(can.offer(f, 108));
    io.regs[0x2D] = 0x20; can.service(109, complete, 100);
    CHECK(can.fault()); CHECK(can.service(110, complete, 100) == -1);
    CHECK(can.begin(16000000, 1000000));
    for (Frame received : {Frame{0x321, 7, {1,2,3,4,5,6,7}},
                           Frame{0x1234567, 8, {0,1,2,3,4,5,6,7}, true},
                           Frame{0x123, 4, {}, true, true}, Frame{0x123, 8, {}, false, true}}) {
        uint8_t wire[13] = {}; Mcp2515<RegisterIO>::pack(received, wire);
        io.writeBlock(0x61, wire, 13); io.regs[0x60] = received.rtr ? 8 : 0; io.regs[0x2C] |= 1;
        Frame decoded; CHECK(can.receive(decoded));
        CHECK(decoded.id == received.id && decoded.dlc == received.dlc &&
              decoded.extended == received.extended && decoded.rtr == received.rtr);
        if (!received.rtr) CHECK(!std::memcmp(decoded.data, received.data, received.dlc));
        CHECK(!(io.regs[0x2C] & 1));
    }
    io.regs[0x65] = 9; io.regs[0x2C] = 1;
    CHECK(!can.receive(complete) && can.invalidRx == 1);
    io.regs[0x2D] = 0xC0; can.service(200, complete, 100);
    CHECK(can.overruns == 2 && !(io.regs[0x2D] & 0xC0));
    CHECK(can.activate()); CHECK(can.offer(f, 201));
    // An abort request cannot undo a frame that already completed on the wire.
    io.regs[0x30] &= ~8; io.regs[0x2C] |= 4; can.abort();
    CHECK(can.service(202, complete, 100) == 1);
    uint8_t expected[13] = {};
    Mcp2515<RegisterIO>::pack({0x1234567, 0, {}, true}, expected);
    CHECK(expected[0] == 0x09 && expected[1] == 0x0B && expected[2] == 0x45 && expected[3] == 0x67);
    CHECK(can.offer(f, 203)); io.ignoreAbort = true;
    CHECK(can.service(303, complete, 100) == 0);
    CHECK(can.service(403, complete, 100) == -1);
    CHECK(!can.pending() && !can.ready() && can.fault());
    io.ignoreAbort = false; CHECK(can.begin(16000000, 1000000));
    io.changeModes = false; CHECK(!can.begin(16000000, 1000000));
}

void outputQueue() {
    struct Writer {
        int available = 0;
        std::string bytes;
        int availableForWrite() const { return available; }
        size_t write(const uint8_t* text, size_t n) {
            CHECK(n <= static_cast<size_t>(available));
            bytes.append(reinterpret_cast<const char*>(text), n); return n;
        }
    } writer;
    OutputQueue<64, 16> out;
    CHECK(out.push("01234567890123456789012345678901234567890123456", true)); // 48 bytes
    CHECK(!out.push("x", true)); CHECK(out.droppedMonitor == 1);
    CHECK(out.push("OK")); CHECK(out.free() == 13);
    out.drain(writer); CHECK(writer.bytes.empty());
    writer.available = 7;
    while (out.free() < 64) out.drain(writer);
    CHECK(writer.bytes == "01234567890123456789012345678901234567890123456\nOK\n");
    writer.bytes.clear();
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(out.push("wrap-around"));
        while (out.free() < 64) out.drain(writer);
    }
    CHECK(writer.bytes.size() == 1200);
    CHECK(!out.push(std::string(64, 'X').c_str())); CHECK(out.droppedReplies == 1);
    CHECK(out.free() == 64);
    out.format(true, "%s", std::string(400, 'X').c_str()); CHECK(out.droppedMonitor == 2);
}

struct Host {
    Simulator& sim;
    std::vector<std::string> replies;
    uint64_t clock() { return 100; }
    void reply(const char* text) { replies.emplace_back(text); }
    bool start() { return sim.start(clock()); }
    void stop() { sim.stop(clock()); }
    void pause() { sim.stop(clock()); }
    bool idle() const { return true; }
    bool recover() { return !sim.running(); }
    bool configure(uint32_t a, uint32_t b, bool) { BitTiming t; return !sim.running() && bitTiming(b,a,t); }
    void status() { reply("status"); }
    void list() { reply("list"); }
    void monitor(const char*, bool) {}
    void filter(bool, uint32_t) {}
};
void commands() {
    Simulator sim; CHECK(loadVehicleProfile(sim));
    Host host{sim, {}}; Commands<Host> console(sim, host);
    const auto send = [&](const char* text) { while (*text) console.feed(*text++); console.feed('\n'); };
    send("SET 0x701 0 127"); CHECK(sim.find(0x701)->frame.data[0] == 127);
    send("SET 0x701 0 256"); CHECK(host.replies.back().find("ERR") == 0);
    send("SET 0x701 0 -1"); CHECK(sim.find(0x701)->frame.data[0] == 127);
    send("SET_SIGNAL NMT_STATE 5"); CHECK(sim.find(0x701)->frame.data[0] == 5);
    send("SET_SIGNAL NMT_STATE nan"); CHECK(host.replies.back().find("ERR") == 0);
    send("SET_SOC 75.5"); CHECK(host.replies.back().find("ERR signal") == 0);
    send("ADD 0x500 100 3 5 0 0"); CHECK(sim.find(0x500)->frame.dlc == 3);
    send("FRAME 0x500 3 5 1 2"); CHECK(sim.find(0x500)->frame.data[2] == 2);
    const Frame before = sim.find(0x500)->frame;
    send("FRAME 0x500 3 5 1 256"); CHECK(!std::memcmp(before.data, sim.find(0x500)->frame.data, 8));
    send("ADD 0x501 100 2 5"); CHECK(!sim.find(0x501));
    send("START extra"); CHECK(!sim.running());
    send("START"); CHECK(sim.running()); send("MODE AUTO"); CHECK(sim.mode() == Mode::Manual);
    send("STOP"); send("MODE AUTO"); CHECK(sim.mode() == Mode::Auto);
    send("INTERVAL 0x701 0"); CHECK(sim.find(0x701)->intervalMs == 1000);
    send("INTERVAL 0x701 0x64"); CHECK(sim.find(0x701)->intervalMs == 100);
    send("ENABLE 0x701 OFF"); CHECK(!sim.find(0x701)->enabled);
    const std::string huge(300, 'X'); send(huge.c_str());
    CHECK(host.replies.back().find("ERR line too long") == 0);
    send("STATUS"); CHECK(host.replies.back() == "status");
    const auto n = host.replies.size(); send("STATUS\r"); CHECK(host.replies.size() == n + 1);
    uint32_t value = 0;
    CHECK(unsignedNumber("4294967295", value) && value == 0xFFFFFFFF);
    CHECK(!unsignedNumber("4294967296", value)); CHECK(!unsignedNumber("0x", value));
    CHECK(!unsignedNumber("+1", value)); CHECK(!unsignedNumber("FF", value));
    CHECK(sim.load(recording::frames, recording::count, recording::loopGapMs));
    send("PLAY"); CHECK(sim.running() && sim.mode() == Mode::Playback);
    send("RESTART"); CHECK(host.replies.back().find("ERR") == 0);
    send("PAUSE"); send("RESTART"); send("LOOP OFF"); CHECK(!sim.loop);
    send("CONFIG 1000000 8000000 NORMAL"); CHECK(host.replies.back().find("ERR") == 0);
    send("CONFIG 500000 8000000 LOOPBACK"); CHECK(host.replies.back() == "OK");
}

int main() {
    encoding(); cyclicAndAuto(); playback(); driver(); commands(); outputQueue();
    std::printf("PASS: %u checks (encoding, scheduler, replay, MCP2515 registers, serial commands)\n", checks);
}
