/**
 * @file sidestick_common.cpp
 * @brief Platform-independent Sidestick mapping and probe loop.
 */

#include "sidestick.hpp"

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <thread>

namespace hermes {

namespace {
volatile std::sig_atomic_t probing = 1;
void stopProbe(int) { probing = 0; }
} // namespace

bool Sidestick::poll(ControlPacket& pkt, uint8_t& seq) {
    Raw raw;
    read(raw);
    fill(raw, pkt, seq);
    return true; // quit is handled by SIGINT → running flag in client_loop.hpp
}

void Sidestick::fill(const Raw& raw, ControlPacket& pkt, uint8_t& seq) {
    uint8_t actions = 0u;
    if (raw.buttons[BTN_BRAKE]) actions |= action::BRAKE;
    if (raw.buttons[BTN_PIVOT]) actions |= action::TOGGLE_PIVOT;
    if (raw.buttons[BTN_CRAB])  actions |= action::CRAB_MODE;
    if (raw.buttons[BTN_BOOST]) actions |= action::BOOST;
    trackMode(actions);

    pkt.magic = MAGIC;
    pkt.seq   = seq++;
    if (mode_ == Mode::PIVOT) {
        // Pico drives pivot from throttle; twist sets the spin duty
        pkt.throttle = TWIST_SIGN * applyDeadZone(raw.rz, TWIST_DEAD_ZONE);
        pkt.steer    = 0.0f;
    } else {
        pkt.throttle = Y_SIGN * applyDeadZone(raw.y);
        pkt.steer    = applyDeadZone(raw.x);
    }
    pkt.aux     = 0.0f;
    pkt.actions = actions;
    stampCrc(pkt);
}

void Sidestick::trackMode(uint8_t actions) noexcept {
    const uint8_t edges = actions & static_cast<uint8_t>(~prevActions_);
    prevActions_ = actions;

    // Same tie-break as the Pico: BRAKE, then pivot, then crab
    if (edges & action::BRAKE) {
        mode_ = (mode_ == Mode::BRAKE) ? Mode::ACKERMANN : Mode::BRAKE;
    } else if (edges & action::TOGGLE_PIVOT) {
        mode_ = (mode_ == Mode::PIVOT) ? Mode::ACKERMANN : Mode::PIVOT;
    } else if (edges & action::CRAB_MODE) {
        mode_ = (mode_ == Mode::CRAB) ? Mode::ACKERMANN : Mode::CRAB;
    }
}

void Sidestick::probe() {
    std::signal(SIGINT,  stopProbe);
    std::signal(SIGTERM, stopProbe);
    std::printf("[hermes-stick] probe mode — move the stick, Ctrl-C to quit\n");

    while (probing) {
        Raw raw;
        read(raw);
        std::printf("\rX=%+.2f Y=%+.2f Rz=%+.2f  buttons:", raw.x, raw.y, raw.rz);
        for (int i = 0; i < raw.buttonCount && i < MAX_BUTTONS; ++i) {
            if (raw.buttons[i]) std::printf(" %d", i);
        }
        std::printf("            ");
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::printf("\n");
}

float Sidestick::applyDeadZone(float value, float zone) noexcept {
    return (std::fabs(value) < zone) ? 0.0f : value;
}

} // namespace hermes
