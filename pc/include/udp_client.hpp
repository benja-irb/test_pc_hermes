/**
 * @file udp_client.hpp
 * @brief Cross-platform UDP client for sending ControlPacket datagrams.
 *
 * Platform differences are isolated here:
 *   - POSIX (Linux/macOS): sys/socket.h, unistd.h
 *   - Windows: winsock2.h, ws2_32.lib
 */

#pragma once

#include <string>
#include <cstdint>
#include "protocol.hpp"

// ---------------------------------------------------------------------------
// Platform socket type abstraction
// ---------------------------------------------------------------------------
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socket_t = SOCKET;
    static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
    inline void closeSocket(socket_t s) { ::closesocket(s); }
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    using socket_t = int;
    static constexpr socket_t INVALID_SOCK = -1;
    inline void closeSocket(socket_t s) { ::close(s); }
#endif

namespace hermes {

class UdpClient {
public:
    /**
     * @param host Destination IP address string, e.g. "192.168.1.100".
     * @param port Destination UDP port (default: HERMES_DEFAULT_PORT).
     * @throws std::runtime_error on socket creation or address parse failure.
     */
    UdpClient(const std::string& host, uint16_t port = DEFAULT_PORT);
    ~UdpClient();

    UdpClient(const UdpClient&) = delete;
    UdpClient& operator=(const UdpClient&) = delete;

    /**
     * @brief Send a ControlPacket datagram to the configured destination.
     * @return true on success, false on send error.
     */
    bool send(const ControlPacket& pkt);

private:
    socket_t       fd_;
    sockaddr_in    dest_;
};

// ---------------------------------------------------------------------------

/**
 * @brief Non-blocking UDP receiver for FeedbackPacket datagrams from the Pi.
 *
 * Binds to FEEDBACK_PORT (5006) and receives frames sent by the Pi bridge
 * after it relays telemetry from the Pico.
 */
class UdpFeedbackReceiver {
public:
    /**
     * @param port Local UDP port to bind to (default: FEEDBACK_PORT).
     * @throws std::runtime_error on socket creation or bind failure.
     */
    explicit UdpFeedbackReceiver(uint16_t port = FEEDBACK_PORT);
    ~UdpFeedbackReceiver();

    UdpFeedbackReceiver(const UdpFeedbackReceiver&) = delete;
    UdpFeedbackReceiver& operator=(const UdpFeedbackReceiver&) = delete;

    /**
     * @brief Try to receive a validated FeedbackPacket without blocking.
     * @param[out] pkt Populated on success.
     * @return true if a valid packet was received, false otherwise.
     */
    bool receive(FeedbackPacket& pkt);

private:
    socket_t fd_;
};

} // namespace hermes
