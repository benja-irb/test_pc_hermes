/**
 * @file stick_main.cpp
 * @brief Hermes PC client — Thrustmaster TCA Sidestick → UDP → Raspberry Pi.
 *
 * Usage:
 *   ./hermes-stick <pi-ip> [udp-port]
 *   ./hermes-stick --probe            # print raw axes/buttons, no network
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sidestick.hpp"
#include "client_loop.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <pi-ip> [udp-port]\n"
                             "       %s --probe\n", argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    hermes::Sidestick stick;

    if (std::strcmp(argv[1], "--probe") == 0) {
        stick.probe();
        return EXIT_SUCCESS;
    }

    return hermes::runClient(stick, "hermes-stick", argc, argv);
}
