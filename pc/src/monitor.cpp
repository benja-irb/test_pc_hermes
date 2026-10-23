#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>

#include "udp_client.hpp"
#include "protocol.hpp"

using namespace hermes;

static volatile bool running = true;

static void handleSignal(int) { running = false; }

int main(int argc, char* argv[]) {
    const char* filterIp = nullptr;
    uint16_t    port     = FEEDBACK_PORT;

    if (argc == 2) {
        filterIp = argv[1];
    } else if (argc == 3) {
        filterIp = argv[1];
        port     = static_cast<uint16_t>(std::atoi(argv[2]));
    } else if (argc > 3) {
        std::fprintf(stderr, "Usage: %s [pi-ip] [port]\n", argv[0]);
        return EXIT_FAILURE;
    }

    in_addr filterAddr{};
    if (filterIp) {
#ifdef _WIN32
        if (InetPtonA(AF_INET, filterIp, &filterAddr) != 1) {
#else
        if (::inet_pton(AF_INET, filterIp, &filterAddr) != 1) {
#endif
            std::fprintf(stderr, "[hermes-monitor] invalid address: %s\n", filterIp);
            return EXIT_FAILURE;
        }
    }

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[hermes-monitor] WSAStartup failed\n");
        return EXIT_FAILURE;
    }
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);
#else
    // sigaction without SA_RESTART so recvfrom() is interrupted by Ctrl-C
    struct sigaction sa{};
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif

    socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCK) {
        std::fprintf(stderr, "[hermes-monitor] socket() failed\n");
        return EXIT_FAILURE;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "[hermes-monitor] bind() failed on port %u\n", port);
        closeSocket(fd);
        return EXIT_FAILURE;
    }

    // Set 1-second receive timeout so the loop can send keepalives periodically.
#ifdef _WIN32
    DWORD tv_ms = 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv_ms), sizeof(tv_ms));
#else
    struct timeval tv{ 1, 0 };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // Keepalive: send a zero ControlPacket to Pi:DEFAULT_PORT so the Pi learns
    // this address and routes FeedbackPackets back here.
    socket_t    kaSock = INVALID_SOCK;
    sockaddr_in kaDest{};
    if (filterIp) {
        kaSock = ::socket(AF_INET, SOCK_DGRAM, 0);
        kaDest.sin_family = AF_INET;
        kaDest.sin_port   = htons(DEFAULT_PORT);
#ifdef _WIN32
        InetPtonA(AF_INET, filterIp, &kaDest.sin_addr);
#else
        ::inet_pton(AF_INET, filterIp, &kaDest.sin_addr);
#endif
    }

    auto sendKeepalive = [&]() {
        if (kaSock == INVALID_SOCK) return;
        ControlPacket ka{};
        ka.magic = MAGIC;
        stampCrc(ka);
        ::sendto(kaSock, reinterpret_cast<const char*>(&ka), sizeof(ka), 0,
                 reinterpret_cast<const sockaddr*>(&kaDest), sizeof(kaDest));
    };

    if (filterIp)
        std::printf("[hermes-monitor] listening on :%u  filter=%s  (Ctrl-C to quit)\n", port, filterIp);
    else
        std::printf("[hermes-monitor] listening on :%u  (Ctrl-C to quit)\n", port);

    sendKeepalive();

    FeedbackPacket fb{};
    sockaddr_in    sender{};
    socklen_t      senderLen = sizeof(sender);
    while (running) {
        int n = static_cast<int>(::recvfrom(
            fd, reinterpret_cast<char*>(&fb), sizeof(fb), 0,
            reinterpret_cast<sockaddr*>(&sender), &senderLen));
        if (n < 0) {
#ifdef _WIN32
            int e = WSAGetLastError();
            if (e == WSAETIMEDOUT || e == WSAEWOULDBLOCK) { sendKeepalive(); continue; }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) { sendKeepalive(); continue; }
#endif
            break;
        }
        if (filterIp && sender.sin_addr.s_addr != filterAddr.s_addr) continue;
        if (n != static_cast<int>(sizeof(FeedbackPacket))) continue;
        if (!validateCrc(fb)) continue;

        std::printf("[rx] angles FL=%.1f FR=%.1f RL=%.1f RR=%.1f"
                    "  duty FL=%.0f FR=%.0f RL=%.0f RR=%.0f\n",
                    fb.steer_angles[0], fb.steer_angles[1],
                    fb.steer_angles[2], fb.steer_angles[3],
                    fb.traction_duty[0], fb.traction_duty[1],
                    fb.traction_duty[2], fb.traction_duty[3]);
    }

    closeSocket(fd);
    if (kaSock != INVALID_SOCK) closeSocket(kaSock);

#ifdef _WIN32
    WSACleanup();
#endif

    std::printf("[hermes-monitor] bye.\n");
    return EXIT_SUCCESS;
}
