#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "Engine/Source/ComSat/ComSatNetwork.h"

// OFFSET: 0x008888F0
ComSat::Network::SocketPosix::SocketPosix() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    this->m_socket = static_cast<uintptr_t>(fd);
    if (fd < 0) {
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int value = 0x8000;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
}

// OFFSET: 0x00888960
ComSat::Network::SocketPosix::~SocketPosix() {
    int fd = static_cast<int>(this->m_socket);
    if (fd < 0) {
        return;
    }
    close(fd);
}

// OFFSET: 0x00888800
bool ComSat::Network::SocketPosix::Bind(int32_t port) {
    int fd = static_cast<int>(this->m_socket);
    if (fd < 0) {
        return false;
    }
    ComSat::Network::Address address;
    address.SetPort(port);
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    return bind(fd, reinterpret_cast<const sockaddr*>(&address.m_family), address.m_length) == 0;
}

// OFFSET: 0x00888860
int32_t ComSat::Network::SocketPosix::SendTo(const ComSat::Network::Address& address, const uint8_t* data, uint32_t size) {
    int fd = static_cast<int>(this->m_socket);
    if (fd < 0) {
        return -1;
    }
    return sendto(fd, data, size, 0, reinterpret_cast<const sockaddr*>(&address.m_family), address.m_length);
}

// OFFSET: 0x00888890
bool ComSat::Network::SocketPosix::RecvFrom(ComSat::Network::Address& address, uint8_t* data, uint32_t& size) {
    int fd = static_cast<int>(this->m_socket);
    if (fd < 0) {
        return false;
    }
    memset(data, 0, size);
    socklen_t length = address.m_length;
    ssize_t received = recvfrom(fd, data, size, 0, reinterpret_cast<sockaddr*>(&address.m_family), &length);
    address.m_length = static_cast<int32_t>(length);
    if (received <= 0) {
        return false;
    }
    size = static_cast<uint32_t>(received);
    return true;
}
