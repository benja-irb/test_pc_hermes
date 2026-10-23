/**
 * @file main.cpp
 * @brief Hermes PC client — Xbox controller → UDP → Raspberry Pi.
 *
 * Usage:
 *   ./hermes-pc <pi-ip> [udp-port]
 *
 * Example:
 *   ./hermes-pc 192.168.1.50 5005
 *
 * Reads the Xbox controller at 50 Hz and transmits a ControlPacket UDP
 * datagram to the Pi bridge on each iteration (see client_loop.hpp).
 */

#include <cstdio>
#include <cstdlib>

#include "controller.hpp"
#include "client_loop.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <pi-ip> [udp-port]\n", argv[0]);
        return EXIT_FAILURE;
    }

    hermes::Controller controller;
    return hermes::runClient(controller, "hermes-pc", argc, argv);
}
