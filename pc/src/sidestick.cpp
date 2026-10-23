/**
 * @file sidestick.cpp
 * @brief SDL3 Sidestick backend — Linux and Windows.
 */

#include "sidestick.hpp"

#include <SDL3/SDL.h>
#include <cstring>
#include <stdexcept>
#include <string>

namespace hermes {

namespace {
// SDL joystick axis indices for the TCA Sidestick — verify with --probe
constexpr int AXIS_X  = 0;
constexpr int AXIS_Y  = 1;
constexpr int AXIS_RZ = 2;
} // namespace

Sidestick::Sidestick() : handle_(nullptr), mode_(Mode::ACKERMANN), prevActions_(0u) {
    if (!SDL_Init(SDL_INIT_JOYSTICK)) {
        throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (count == 0 || !ids) {
        SDL_free(ids);
        SDL_Quit();
        throw std::runtime_error("No joystick detected — make sure the TCA Sidestick is plugged in");
    }

    // Prefer a joystick whose name mentions TCA; otherwise take the first one
    SDL_JoystickID pick = ids[0];
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        if (name && std::strstr(name, "TCA")) { pick = ids[i]; break; }
    }
    SDL_free(ids);

    auto* joystick = SDL_OpenJoystick(pick);
    if (!joystick) {
        SDL_Quit();
        throw std::runtime_error(std::string("SDL_OpenJoystick: ") + SDL_GetError());
    }

    handle_ = static_cast<void*>(joystick);
}

Sidestick::~Sidestick() {
    if (handle_) SDL_CloseJoystick(static_cast<SDL_Joystick*>(handle_));
    SDL_Quit();
}

void Sidestick::read(Raw& raw) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
    SDL_UpdateJoysticks();

    auto* joystick = static_cast<SDL_Joystick*>(handle_);
    raw.x  = SDL_GetJoystickAxis(joystick, AXIS_X)  / 32767.0f;
    raw.y  = SDL_GetJoystickAxis(joystick, AXIS_Y)  / 32767.0f;
    raw.rz = SDL_GetJoystickAxis(joystick, AXIS_RZ) / 32767.0f;

    const int n = SDL_GetNumJoystickButtons(joystick);
    raw.buttonCount = (n < MAX_BUTTONS) ? n : MAX_BUTTONS;
    for (int i = 0; i < raw.buttonCount; ++i) {
        raw.buttons[i] = SDL_GetJoystickButton(joystick, i);
    }
}

} // namespace hermes
