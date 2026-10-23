/**
 * @file sidestick.hpp
 * @brief Thrustmaster TCA Sidestick (Airbus Edition) reader — IOKit HID on
 *        macOS, SDL3 elsewhere.
 *
 * Translates raw stick inputs into the input-agnostic ControlPacket actions:
 *   throttle  — stick Y (push forward = forward duty), [-1.0, 1.0];
 *               in pivot mode: twist (Rz) spin duty instead
 *   steer     — stick X (roll), [-1.0, 1.0]; 0.0 in pivot mode
 *   aux       — reserved, always 0.0
 *   actions   — BRAKE (trigger), TOGGLE_PIVOT (red button), CRAB_MODE,
 *               BOOST
 *
 * Twist is ignored until the red button switches the rover into pivot mode.
 * The Pico owns the drive mode, so the stick mirrors its state machine
 * (rising edges on BRAKE / TOGGLE_PIVOT / CRAB_MODE, same priority as
 * pico/src/main.cpp) to know when twist should drive the pivot.
 *
 * Run `hermes-stick --probe` to print raw axes/buttons and verify mapping.
 */

#pragma once

#include <cstddef>
#include "protocol.hpp"

namespace hermes {

class Sidestick {
public:
    /**
     * @throws std::runtime_error if no joystick can be opened.
     */
    Sidestick();
    ~Sidestick();

    Sidestick(const Sidestick&) = delete;
    Sidestick& operator=(const Sidestick&) = delete;

    /**
     * @brief Poll the stick and fill a ControlPacket.
     * @param[out] pkt  Populated with current input state.
     * @param[in,out] seq  Rolling sequence counter; incremented on each call.
     * @return false if the application should quit.
     */
    bool poll(ControlPacket& pkt, uint8_t& seq);

    /** @brief Print raw normalised axes and pressed buttons until Ctrl-C. */
    void probe();

    // Zero-based button indices (HID button N == index N-1). Same mapping as
    // the Godot ground station's Controller panel (godot/scripts/controller_panel.gd).
    static constexpr int BTN_BRAKE = 0;  // trigger
    static constexpr int BTN_PIVOT = 2;  // red button
    static constexpr int BTN_CRAB  = 3;
    static constexpr int BTN_BOOST = 10;
    static constexpr int MAX_BUTTONS = 32;

private:
    /** Raw normalised hardware state, axes in [-1.0, 1.0]. */
    struct Raw {
        float x = 0.0f;
        float y = 0.0f;
        float rz = 0.0f;
        bool  buttons[MAX_BUTTONS] = {};
        int   buttonCount = 0;
    };

    /** Mirror of the Pico's DriveMode (pico/src/main.cpp). */
    enum class Mode { ACKERMANN, PIVOT, CRAB, BRAKE };

    void*   handle_;      // backend state (IOKit device on macOS, SDL_Joystick* elsewhere)
    Mode    mode_;
    uint8_t prevActions_;

    static constexpr float DEAD_ZONE       = 0.08f;
    static constexpr float TWIST_DEAD_ZONE = 0.10f;
    static constexpr float TWIST_SIGN      = 1.0f;  // flip if pivot spins the wrong way
    static constexpr float Y_SIGN      = -1.0f; // HID Y is negative when pushed forward

    /** Backend-specific: read current hardware state. */
    void read(Raw& raw);

    /** Shared mapping from raw state to a ControlPacket. */
    void fill(const Raw& raw, ControlPacket& pkt, uint8_t& seq);

    /** Update mode_ from button rising edges, exactly like the Pico. */
    void trackMode(uint8_t actions) noexcept;

    static float applyDeadZone(float value, float zone = DEAD_ZONE) noexcept;
};

} // namespace hermes
