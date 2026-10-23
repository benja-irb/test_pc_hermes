/**
 * @file protocol.hpp
 * @brief Hermes rover binary control protocol — shared across all three nodes.
 *
 * A single 16-byte ControlPacket is sent over both:
 *   - UDP datagrams  (PC → Raspberry Pi, port DEFAULT_PORT)
 *   - USB serial     (Raspberry Pi → Pico,  BAUD_RATE bps)
 *
 * The packet describes rover *actions*, so any
 * input source (gamepad, keyboard, autonomous planner, …) can fill it.
 *
 * Byte layout:
 *   [0]      magic    = 0xAB  (start-of-frame marker)
 *   [1]      seq      rolling counter used to detect dropped packets
 *   [2..5]   throttle IEEE-754 float [-1.0, 1.0]  forward(+) / backward(−)
 *   [6..9]   steer    IEEE-754 float [-1.0, 1.0]  right(+) / left(−)
 *   [10..13] aux      IEEE-754 float, reserved for future analog input
 *   [14]     actions  bitmask — see hermes::action::* constants
 *   [15]     crc      XOR of bytes [0..14]
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace hermes {

static constexpr uint8_t  MAGIC            = 0xAB;
static constexpr uint16_t DEFAULT_PORT     = 5005;
static constexpr uint32_t BAUD_RATE        = 115200;

/** Action bit masks within ControlPacket::actions. */
namespace action {
    static constexpr uint8_t TOGGLE_PIVOT = (1u << 0); // switch Ackermann ↔ pivot mode
    static constexpr uint8_t BOOST        = (1u << 1); // speed multiplier  (future use)
    static constexpr uint8_t BRAKE        = (1u << 2); // switch Ackermann ↔ brake mode (disables traction + steering)
    static constexpr uint8_t CRAB_MODE    = (1u << 3); // switch Ackermann ↔ crab (parallel-steer) mode
}

#pragma pack(push, 1)
struct ControlPacket {
    uint8_t magic;
    uint8_t seq;
    float   throttle; // [-1.0, 1.0] forward / backward
    float   steer;    // [-1.0, 1.0] right / left
    float   aux;      // reserved for future analog input — send 0.0
    uint8_t actions;  // bitmask of hermes::action::* flags
    uint8_t crc;
};
#pragma pack(pop)

static_assert(sizeof(ControlPacket) == 16, "ControlPacket must be exactly 16 bytes");

/** Compute XOR checksum over all bytes except the last (crc) field. */
inline uint8_t computeCrc(const ControlPacket& pkt) noexcept {
    const auto* b = reinterpret_cast<const uint8_t*>(&pkt);
    uint8_t x = 0;
    for (size_t i = 0; i < sizeof(ControlPacket) - 1u; ++i) {
        x ^= b[i];
    }
    return x;
}

inline void stampCrc(ControlPacket& pkt) noexcept {
    pkt.crc = computeCrc(pkt);
}

/** Returns true when magic matches and checksum is valid. */
inline bool validateCrc(const ControlPacket& pkt) noexcept {
    return pkt.magic == MAGIC && pkt.crc == computeCrc(pkt);
}

// ---------------------------------------------------------------------------
// Feedback protocol — Pico → Pi → PC
// ---------------------------------------------------------------------------

static constexpr uint8_t  FEEDBACK_MAGIC = 0xCD;
static constexpr uint16_t FEEDBACK_PORT  = 5006;

#pragma pack(push, 1)
struct FeedbackPacket {
    uint8_t magic;            // FEEDBACK_MAGIC = 0xCD
    uint8_t seq;
    float   steer_angles[4];  // [°] measured wheel angles — FL, FR, RL, RR
    float   traction_duty[4]; // [−100, 100] last applied PWM duty — FL, FR, RL, RR
    uint8_t crc;
};
#pragma pack(pop)

static_assert(sizeof(FeedbackPacket) == 35, "FeedbackPacket must be exactly 35 bytes");

inline uint8_t computeCrc(const FeedbackPacket& pkt) noexcept {
    const auto* b = reinterpret_cast<const uint8_t*>(&pkt);
    uint8_t x = 0;
    for (size_t i = 0; i < sizeof(FeedbackPacket) - 1u; ++i) x ^= b[i];
    return x;
}

inline void stampCrc(FeedbackPacket& pkt) noexcept {
    pkt.crc = computeCrc(pkt);
}

inline bool validateCrc(const FeedbackPacket& pkt) noexcept {
    return pkt.magic == FEEDBACK_MAGIC && pkt.crc == computeCrc(pkt);
}

} // namespace hermes
