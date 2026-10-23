/**
 * @file controller_macos.mm
 * @brief macOS gamepad reader using native GameController.framework (GCController).
 *
 * SDL3's joystick API cannot enumerate Bluetooth Xbox controllers on macOS Sequoia
 * because the OS gives GCController exclusive access. This implementation reads
 * the controller directly via Apple's framework, bypassing SDL entirely.
 */

#import <GameController/GameController.h>
#include "controller.hpp"

#include <stdexcept>
#include <cmath>

namespace hermes {

Controller::Controller() : handle_(nullptr), activeTrigger_('\0') {
    // GCControllerDidConnectNotification is async — spin the run loop up to 3 s
    // so the OS has time to register the controller before we take a snapshot.
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:3.0];
    while ([GCController controllers].count == 0 &&
           [[NSDate date] compare:deadline] == NSOrderedAscending) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }

    NSArray<GCController*>* controllers = [GCController controllers];
    if (controllers.count == 0) {
        throw std::runtime_error(
            "No gamepad detected — make sure the Xbox controller is paired "
            "and shows Connected in System Settings > Game Controllers");
    }

    GCController* gc = controllers[0];
    if (!gc.extendedGamepad) {
        throw std::runtime_error("Controller does not expose an extended gamepad profile");
    }

    // Retain the GCController so ARC doesn't release it between polls
    handle_ = (__bridge_retained void*)gc;
}

Controller::~Controller() {
    if (handle_) {
        // Transfer ownership back to ARC so it is released
        GCController* gc = (__bridge_transfer GCController*)handle_;
        (void)gc;
        handle_ = nullptr;
    }
}

bool Controller::poll(ControlPacket& pkt, uint8_t& seq) {
    GCExtendedGamepad* pad = ((__bridge GCController*)handle_).extendedGamepad;

    // Triggers: GCController already gives [0.0, 1.0]
    float lt = pad.leftTrigger.value;
    float rt = pad.rightTrigger.value;
    if (lt < 0.05f) lt = 0.0f;
    if (rt < 0.05f) rt = 0.0f;

    // Trigger priority
    if      (rt > 0.0f && lt == 0.0f) activeTrigger_ = 'R';
    else if (lt > 0.0f && rt == 0.0f) activeTrigger_ = 'L';
    else if (rt == 0.0f && lt == 0.0f) activeTrigger_ = '\0';
    else if (activeTrigger_ == '\0')   activeTrigger_ = (rt >= lt) ? 'R' : 'L';

    if (activeTrigger_ == 'R') lt = 0.0f;
    if (activeTrigger_ == 'L') rt = 0.0f;

    float steer = applyDeadZone(pad.leftThumbstick.xAxis.value);

    uint8_t actions = 0u;
    if (pad.buttonA.isPressed)      actions |= action::TOGGLE_PIVOT;
    if (pad.leftShoulder.isPressed)  actions |= action::BOOST;
    if (pad.buttonX.isPressed)       actions |= action::BRAKE;
    if (pad.buttonB.isPressed)       actions |= action::CRAB_MODE;

    pkt.magic    = MAGIC;
    pkt.seq      = seq++;
    pkt.throttle = rt - lt;
    pkt.steer    = steer;
    pkt.aux      = 0.0f;
    pkt.actions  = actions;
    stampCrc(pkt);

    return true; // quit is handled by SIGINT → running flag in main.cpp
}

float Controller::applyDeadZone(float value) noexcept {
    return (std::fabs(value) < DEAD_ZONE) ? 0.0f : value;
}

float Controller::normalizeTrigger(float rawAxis) noexcept {
    float v = (rawAxis + 1.0f) / 2.0f;
    return (v < 0.05f) ? 0.0f : v;
}

} // namespace hermes
