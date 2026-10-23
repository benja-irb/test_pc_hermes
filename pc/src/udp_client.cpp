/**
 * @file udp_client.cpp
 * @brief Cross-platform UDP client implementation.
 */

#include "udp_client.hpp"
#include "debug.hpp"

#include <stdexcept>
#include <cstring>
#include <cstdio>
#ifndef _WIN32
    #include <fcntl.h>
#endif

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
#endif

namespace hermes {

UdpClient::UdpClient(const std::string& host, uint16_t port) : fd_(INVALID_SOCK) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif

    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ == INVALID_SOCK) {
        throw std::runtime_error("socket() failed");
    }

    std::memset(&dest_, 0, sizeof(dest_));
    dest_.sin_family = AF_INET;
    dest_.sin_port   = htons(port);

#ifdef _WIN32
    if (InetPtonA(AF_INET, host.c_str(), &dest_.sin_addr) != 1)
#else
    if (::inet_pton(AF_INET, host.c_str(), &dest_.sin_addr) != 1)
#endif
    {
        closeSocket(fd_);
        fd_ = INVALID_SOCK;
        throw std::runtime_error("Invalid host address: " + host);
    }
}

UdpClient::~UdpClient() {
    if (fd_ != INVALID_SOCK) closeSocket(fd_);
#ifdef _WIN32
    WSACleanup();
#endif
}

bool UdpClient::send(const ControlPacket& pkt) {
    // sendto returns int on Windows, ssize_t on POSIX — cast to int is safe
    // because sizeof(ControlPacket) == 16 fits in any int representation
    int n = static_cast<int>(::sendto(
        fd_,
        reinterpret_cast<const char*>(&pkt),
        sizeof(pkt),
        0,
        reinterpret_cast<const sockaddr*>(&dest_),
        sizeof(dest_)
    ));
    bool ok = n == static_cast<int>(sizeof(pkt));
    DBG("[pc-udp] tx ControlPacket seq=%u thr=%+.2f str=%+.2f act=0x%02X  %s",
        pkt.seq, pkt.throttle, pkt.steer, pkt.actions, ok ? "ok" : "FAILED");
    return ok;
}

// ---------------------------------------------------------------------------

UdpFeedbackReceiver::UdpFeedbackReceiver(uint16_t port) : fd_(INVALID_SOCK) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ == INVALID_SOCK) {
        throw std::runtime_error("socket() failed for feedback receiver");
    }

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(fd_, FIONBIO, &mode);
#else
    ::fcntl(fd_, F_SETFL, O_NONBLOCK);
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket(fd_);
        fd_ = INVALID_SOCK;
        throw std::runtime_error("bind() failed for feedback receiver");
    }
}

UdpFeedbackReceiver::~UdpFeedbackReceiver() {
    if (fd_ != INVALID_SOCK) closeSocket(fd_);
}

bool UdpFeedbackReceiver::receive(FeedbackPacket& pkt) {
    uint8_t buf[sizeof(FeedbackPacket)];
    int n = static_cast<int>(::recvfrom(
        fd_, reinterpret_cast<char*>(buf), sizeof(buf), 0, nullptr, nullptr
    ));
    if (n != static_cast<int>(sizeof(FeedbackPacket))) return false;
    std::memcpy(&pkt, buf, sizeof(FeedbackPacket));
    if (!validateCrc(pkt)) {
        DBG("%s", "[pc-udp] rx FeedbackPacket CRC fail — dropped");
        return false;
    }
    DBG("[pc-udp] rx FeedbackPacket seq=%u angles=%.1f %.1f %.1f %.1f",
        pkt.seq,
        pkt.steer_angles[0], pkt.steer_angles[1],
        pkt.steer_angles[2], pkt.steer_angles[3]);
    return true;
}

} // namespace hermes
