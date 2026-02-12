#include "network.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>

bool UDPSocket::bind(uint16_t port) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        perror("socket");
        return false;
    }

    int opt = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

bool UDPSocket::open() {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        perror("socket");
        return false;
    }
    return true;
}

void UDPSocket::setNonBlocking(bool enable) {
    if (fd_ < 0) return;
    int flags = fcntl(fd_, F_GETFL, 0);
    if (enable)
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    else
        fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
}

int UDPSocket::sendTo(const void* data, size_t len, const sockaddr_in& addr) {
    return ::sendto(fd_, data, len, 0, (const sockaddr*)&addr, sizeof(addr));
}

int UDPSocket::recvFrom(void* buf, size_t maxLen, sockaddr_in& fromAddr) {
    socklen_t addrLen = sizeof(fromAddr);
    return ::recvfrom(fd_, buf, maxLen, 0, (sockaddr*)&fromAddr, &addrLen);
}

void UDPSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

sockaddr_in UDPSocket::makeAddr(const char* ip, uint16_t port) {
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    return addr;
}

bool UDPSocket::addrEqual(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}
