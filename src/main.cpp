#include <Arduino.h>
#include <SPI.h>
#include <esp_timer.h>
#include "board_config.h"
#include "mcp2515.h"
#include "commands.h"
#include "output_queue.h"
#include "vehicle_profile.h"
#include "recording.inc"

using namespace cansim;
uint64_t nowMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }

class SpiIO {
public:
    void begin() {
        pinMode(board::Cs, OUTPUT); digitalWrite(board::Cs, HIGH);
        SPI.begin(board::Sck, board::Miso, board::Mosi, board::Cs);
    }
    void reset() { select(); SPI.transfer(0xC0); deselect(); }
    uint8_t read(uint8_t address) {
        uint8_t value; readBlock(address, &value, 1); return value;
    }
    void readBlock(uint8_t address, uint8_t* data, size_t n) {
        select(); SPI.transfer(0x03); SPI.transfer(address);
        for (size_t i = 0; i < n; ++i) data[i] = SPI.transfer(0);
        deselect();
    }
    void write(uint8_t address, uint8_t value) { writeBlock(address, &value, 1); }
    void writeBlock(uint8_t address, const uint8_t* data, size_t n) {
        select(); SPI.transfer(0x02); SPI.transfer(address);
        for (size_t i = 0; i < n; ++i) SPI.transfer(data[i]);
        deselect();
    }
    void modify(uint8_t address, uint8_t mask, uint8_t value) {
        select(); SPI.transfer(0x05); SPI.transfer(address);
        SPI.transfer(mask); SPI.transfer(value); deselect();
    }
    void waitUs(unsigned us) { delayMicroseconds(us); }
private:
    void select() {
        SPI.beginTransaction(SPISettings(board::SpiHz, MSBFIRST, SPI_MODE0));
        digitalWrite(board::Cs, LOW);
    }
    void deselect() { digitalWrite(board::Cs, HIGH); SPI.endTransaction(); }
};

SpiIO registers;
Mcp2515<SpiIO> can(registers);
Simulator sim;
OutputQueue<> output;

class App {
public:
    uint64_t clock() { return nowMs(); }
    void reply(const char* text) { output.push(text); }
    bool start() {
        if (!profileOk || !can.ready() || can.fault()) return false;
        if (sim.running()) return true;
        if (!can.activate()) return false;
        if (!sim.start(clock())) { can.listen(); return false; }
        return true;
    }
    void stop() {
        sim.stop(clock()); intentionalCancel = can.pending();
        can.abort(); listenPending = true;
    }
    void pause() { sim.stop(clock()); listenPending = true; }
    bool idle() const { return !can.pending(); }
    bool recover() {
        if (sim.running() || can.pending()) return false;
        const bool ok = can.begin(oscillator, bitrate, loopback);
        listenPending = false; return ok;
    }
    bool configure(uint32_t newBitrate, uint32_t newOscillator, bool newLoopback) {
        BitTiming timing;
        if (sim.running() || can.pending() || !bitTiming(newOscillator, newBitrate, timing)) return false;
        bitrate = newBitrate; oscillator = newOscillator; loopback = newLoopback;
        return recover();
    }
    void monitor(const char* target, bool enabled) {
        if (!std::strcmp(target, "RX") || !std::strcmp(target, "ALL")) monitorRx = enabled;
        if (!std::strcmp(target, "TX") || !std::strcmp(target, "ALL")) monitorTx = enabled;
    }
    void filter(bool enabled, uint32_t id) { filterEnabled = enabled; filterId = id; }
    void log(const char* direction, const Frame& f, uint64_t now) {
        if (filterEnabled && filterId != f.id) return;
        char bytes[25] = {};
        if (!f.rtr) for (unsigned i = 0; i < f.dlc; ++i)
            snprintf(bytes + i * 3, sizeof(bytes) - i * 3, "%02X ", f.data[i]);
        output.format(true, "[%llu ms] %s 0x%lX %s DLC:%u %sDATA: %s",
                      now, direction, static_cast<unsigned long>(f.id), f.extended ? "EXT" : "STD",
                      f.dlc, f.rtr ? "RTR " : "", bytes);
    }
    void status() {
        output.format(false, "OK mode=%s running=%u ready=%u fault=%u loopback=%u bitrate=%lu oscillator=%lu sample=%u/1000",
                      sim.mode() == Mode::Manual ? "MANUAL" : sim.mode() == Mode::Auto ? "AUTO" : "PLAYBACK",
                      sim.running(), can.ready(), can.fault(), can.loopback(),
                      static_cast<unsigned long>(bitrate), static_cast<unsigned long>(oscillator), can.timing.samplePermille);
        output.format(false, "TX_QUEUED=%llu TX_OK=%llu TX_FAILED=%llu RX=%llu RX_OVERRUN=%llu RX_INVALID=%llu pending=%u",
                      can.queued, can.sent, can.failed, can.received, can.overruns, can.invalidRx, can.pending());
        output.format(false, "EFLG=0x%02X TEC=%u REC=%u monitor_drop=%llu reply_drop=%llu cyclic_skipped=%llu replay_late=%llu",
                      can.errorFlags, can.tec, can.rec, output.droppedMonitor, output.droppedReplies, sim.skipped, sim.late);
        output.format(false, "messages=%u signals=%u playback=%u/%u complete=%u loop=%u monitor_rx=%u monitor_tx=%u filter=%s:0x%lX",
                      unsigned(sim.messageCount()), unsigned(sim.signalCount()), unsigned(sim.position()),
                      unsigned(sim.recordingCount()), sim.complete(), sim.loop, monitorRx, monitorTx,
                      filterEnabled ? "ON" : "OFF", static_cast<unsigned long>(filterId));
    }
    void list() {
        // Emit a bounded portion each loop instead of dumping all definitions.
        listMessage = 0; listSignal = 0; listing = true;
        reply("OK definitions follow; ADD entries are raw bench messages");
    }
    void emitList() {
        if (!listing || output.free() < 2048) return;
        if (listMessage < sim.messageCount()) {
            const auto& m = sim.message(listMessage++);
            output.format(false, "MSG 0x%lX DLC=%u interval_ms=%lu enabled=%u checksum=%s",
                          static_cast<unsigned long>(m.frame.id), m.frame.dlc,
                          static_cast<unsigned long>(m.intervalMs), m.enabled,
                          m.checksum == Checksum::None ? "none" : "sum_id_dlc");
            return;
        }
        if (listSignal < sim.signalCount()) {
            const auto& s = sim.definition(listSignal++);
            const auto* m = sim.find(s.id);
            output.format(false, "SIGNAL %s ID=0x%lX value=%.6g min=%.6g max=%.6g rate/s=%.6g",
                          s.name, static_cast<unsigned long>(s.id), Simulator::decode(m->frame, s),
                          s.minimum, s.maximum, s.ratePerSecond); return;
        }
        reply("OK END LIST"); listing = false;
    }
    bool profileOk = false, listenPending = false, intentionalCancel = false;
    bool monitorRx = false, monitorTx = false;
private:
    bool loopback = false, filterEnabled = false, listing = false;
    uint32_t bitrate = board::Bitrate, oscillator = board::OscillatorHz, filterId = 0;
    size_t listMessage = 0, listSignal = 0;
};
App app;
Commands<App> commands(sim, app);

void setup() {
    Serial.begin(115200);
    registers.begin();
    app.profileOk = loadVehicleProfile(sim) && sim.load(recording::frames, recording::count, recording::loopGapMs);
    const bool ready = app.recover();
    output.push("CAN simulator; stopped at boot. HELP / STATUS. Bench testing only.");
    if (!app.profileOk) output.push("ERR invalid compiled profile/recording; transmission disabled");
    if (!ready) output.push("ERR CAN init failed; check wiring, oscillator, bitrate; CONFIG / RECOVER");
}
void loop() {
    const uint64_t now = nowMs();
    Frame f;
    const int completion = can.service(now, f, board::TxTimeoutMs);
    if (completion != 0) sim.transmitResult(completion > 0, now);
    if (completion > 0 && app.monitorTx) app.log("TX", f, now);
    if (completion < 0 && !app.intentionalCancel) {
        app.stop(); output.push("ERR CAN transmission failed; simulation stopped; STATUS / RECOVER");
    }
    if (completion != 0) app.intentionalCancel = false;
    if (can.fault() && sim.running()) {
        app.stop(); output.push("ERR CAN fault; simulation stopped; STATUS / RECOVER");
    }
    for (unsigned i = 0; i < 8 && can.receive(f); ++i)
        if (app.monitorRx) app.log("RX", f, nowMs());
    if (app.listenPending && !can.pending()) {
        if (!can.listen()) output.push("ERR unable to enter listen-only mode");
        app.listenPending = false;
    }
    // Bound RX command work, and reserve output room for command responses.
    for (unsigned i = 0; i < 64 && output.free() >= 2048 && Serial.available(); ++i)
        commands.feed(static_cast<char>(Serial.read()));
    const bool wasRunning = sim.running();
    sim.tick(nowMs(), can);
    if (wasRunning && !sim.running()) app.listenPending = true;
    app.emitList(); output.drain(Serial);
    // Yield to ESP32 background tasks without an intentional delay.
    yield();
}
