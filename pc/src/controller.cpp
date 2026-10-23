/**
 * @file controller.cpp
 * @brief SDL3 gamepad reader — Linux and Windows.
 */

#include "controller.hpp"

#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>
#include <cmath>

namespace hermes {

Controller::Controller() : handle_(nullptr), activeTrigger_('\0') {
    SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "1");

    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (count == 0 || !ids) {
        SDL_free(ids);
        SDL_Quit();
        throw std::runtime_error("No joystick/gamepad detected");
    }

    auto* joystick = SDL_OpenJoystick(ids[0]);
    SDL_free(ids);
    if (!joystick) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_OpenJoystick: ") + SDL_GetError());
    }

    handle_ = static_cast<void*>(joystick);
}

Controller::~Controller() {
    if (handle_) SDL_CloseJoystick(static_cast<SDL_Joystick*>(handle_));
    SDL_Quit();
}

bool Controller::poll(ControlPacket& pkt, uint8_t& seq) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) return false;
    }

    auto* joystick = static_cast<SDL_Joystick*>(handle_);
    SDL_UpdateJoysticks();

    float lt = normalizeTrigger(SDL_GetJoystickAxis(joystick, 2) / 32767.0f);
    float rt = normalizeTrigger(SDL_GetJoystickAxis(joystick, 5) / 32767.0f);

    if      (rt > 0.0f && lt == 0.0f) activeTrigger_ = 'R';
    else if (lt > 0.0f && rt == 0.0f) activeTrigger_ = 'L';
    else if (rt == 0.0f && lt == 0.0f) activeTrigger_ = '\0';
    else if (activeTrigger_ == '\0')   activeTrigger_ = (rt >= lt) ? 'R' : 'L';

    if (activeTrigger_ == 'R') lt = 0.0f;
    if (activeTrigger_ == 'L') rt = 0.0f;

    float steer = applyDeadZone(SDL_GetJoystickAxis(joystick, 0) / 32767.0f);

    uint8_t actions = 0u;
    if (SDL_GetJoystickButton(joystick, 0)) actions |= action::TOGGLE_PIVOT;
    if (SDL_GetJoystickButton(joystick, 4)) actions |= action::BOOST;
    if (SDL_GetJoystickButton(joystick, 2)) actions |= action::BRAKE; // X
    if (SDL_GetJoystickButton(joystick, 1)) actions |= action::CRAB_MODE; // B

    pkt.magic    = MAGIC;
    pkt.seq      = seq++;
    pkt.throttle = rt - lt;
    pkt.steer    = steer;
    pkt.aux      = 0.0f;
    pkt.actions  = actions;
    stampCrc(pkt);

    return true;
}

float Controller::applyDeadZone(float value) noexcept {
    return (std::fabs(value) < DEAD_ZONE) ? 0.0f : value;
}

float Controller::normalizeTrigger(float rawAxis) noexcept {
    float v = (rawAxis + 1.0f) / 2.0f;
    return (v < 0.05f) ? 0.0f : v;
}

} // namespace hermes
