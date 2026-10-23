/**
 * @file client_loop.hpp
 * @brief Shared 50 Hz send/receive loop for the PC input clients.
 *
 * Any input class exposing `bool poll(ControlPacket&, uint8_t&)` can drive the
 * rover through runClient(): hermes-pc (Xbox gamepad) and hermes-stick
 * (Thrustmaster TCA Sidestick) both use it.
 *
 * Usage handled here:
 *   <binary> <pi-ip> [udp-port]
 */

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <csignal>
#include <chrono>
#include <thread>

#include "udp_client.hpp"
#include "protocol.hpp"

namespace hermes {

namespace detail {
inline volatile std::sig_atomic_t running = 1;
inline void handleSignal(int) { running = 0; }
} // namespace detail

template <class Input>
int runClient(Input& input, const char* tag, int argc, char* argv[]) {
    using namespace std::chrono;

    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <pi-ip> [udp-port]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char*    piIp = argv[1];
    const uint16_t port = (argc >= 3) ? static_cast<uint16_t>(std::atoi(argv[2]))
                                      : DEFAULT_PORT;

    std::signal(SIGINT,  detail::handleSignal);
    std::signal(SIGTERM, detail::handleSignal);

    UdpClient            udp(piIp, port);
    UdpFeedbackReceiver  feedback;

    std::printf("[%s] sending to %s:%u  feedback on :%u  (Ctrl-C to quit)\n",
                tag, piIp, port, FEEDBACK_PORT);

    ControlPacket  pkt{};
    FeedbackPacket fb{};
    uint8_t        seq = 0;

    constexpr auto PERIOD = milliseconds(20); // 50 Hz

    while (detail::running) {
        auto tick = steady_clock::now();

        if (!input.poll(pkt, seq)) break; // input requested quit

        if (!udp.send(pkt)) {
            std::fprintf(stderr, "[%s] UDP send failed\n", tag);
        } else {
            std::printf("\r[tx] thr=%+.2f str=%+.2f aux=%+.2f act=0x%02X seq=%3u   ",
                        pkt.throttle, pkt.steer, pkt.aux, pkt.actions, pkt.seq);
            std::fflush(stdout);
        }

        if (feedback.receive(fb)) {
            std::printf("\n[rx] angles FL=%.1f FR=%.1f RL=%.1f RR=%.1f"
                        "  duty FL=%.0f FR=%.0f RL=%.0f RR=%.0f\n",
                        fb.steer_angles[0], fb.steer_angles[1],
                        fb.steer_angles[2], fb.steer_angles[3],
                        fb.traction_duty[0], fb.traction_duty[1],
                        fb.traction_duty[2], fb.traction_duty[3]);
        }

        std::this_thread::sleep_until(tick + PERIOD);
    }

    // Best-effort final stop: the rover has no idea we've quit, so send a
    // few zero-throttle/zero-steer packets to guard against UDP packet loss
    // on this last, un-retried command.
    ControlPacket stopPkt{};
    stopPkt.magic    = MAGIC;
    stopPkt.throttle = 0.0f;
    stopPkt.steer    = 0.0f;
    stopPkt.aux      = 0.0f;
    stopPkt.actions  = 0u;
    for (int i = 0; i < 3; ++i) {
        stopPkt.seq = seq++;
        stampCrc(stopPkt);
        udp.send(stopPkt);
        std::this_thread::sleep_for(milliseconds(20));
    }

    std::printf("\n[%s] bye.\n", tag);
    return EXIT_SUCCESS;
}

} // namespace hermes
