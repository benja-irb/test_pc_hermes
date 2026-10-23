/**
 * @file controller.hpp
 * @brief Xbox gamepad reader — GCController on macOS, SDL3 elsewhere.
 *
 * Translates raw hardware inputs into the input-agnostic ControlPacket actions:
 *   throttle  — right trigger(+) / left trigger(−), merged into [-1.0, 1.0]
 *   steer     — left stick X axis, [-1.0, 1.0]
 *   aux       — reserved, always 0.0
 *   actions   — TOGGLE_PIVOT (btn A), BOOST (btn LB), BRAKE (btn X), CRAB_MODE (btn B)
 *
 * Trigger priority logic: once one trigger is active the other is locked out
 * until both return to zero, preventing unintentional simultaneous forward
 * and backward commands.
 */

#pragma once

#include "protocol.hpp"

namespace hermes {

class Controller {
public:
    /**
     * @throws std::runtime_error if the gamepad cannot be opened.
     */
    Controller();
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    /**
     * @brief Poll the gamepad and fill a ControlPacket.
     * @param[out] pkt  Populated with current input state.
     * @param[in,out] seq  Rolling sequence counter; incremented on each call.
     * @return false if the application should quit.
     */
    bool poll(ControlPacket& pkt, uint8_t& seq);

private:
    void* handle_;        // GCController* on macOS, SDL_Joystick* elsewhere
    char  activeTrigger_; // 'L', 'R', or '\0'

    static constexpr float DEAD_ZONE = 0.08f;

    static float applyDeadZone(float value) noexcept;
    static float normalizeTrigger(float rawAxis) noexcept;
};

} // namespace hermes
