#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "Engine/Source/ComSat/ComSatNetwork.h"

// OFFSET: 0x008888F0
ComSat::Network::SocketPosix::SocketPosix() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    this->m_socket = static_cast<uintptr_t>(fd);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int value = 0x8000;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &value, sizeof(value));
}

// OFFSET: 0x00888960
ComSat::Network::SocketPosix::~SocketPosix() {
    close(static_cast<int>(this->m_socket));
}

// OFFSET: 0x00888800
bool ComSat::Network::SocketPosix::Bind(int32_t port) {
    ComSat::Network::Address address;
    address.SetPort(port);
    int reuse = 1;
    setsockopt(static_cast<int>(this->m_socket), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    return bind(static_cast<int>(this->m_socket), reinterpret_cast<const sockaddr*>(&address.m_family), address.m_length) == 0;
}

// OFFSET: 0x00888860
int32_t ComSat::Network::SocketPosix::SendTo(const ComSat::Network::Address& address, const uint8_t* data, uint32_t size) {
    return sendto(static_cast<int>(this->m_socket), data, size, 0, reinterpret_cast<const sockaddr*>(&address.m_family), address.m_length);
}

// OFFSET: 0x00888890
bool ComSat::Network::SocketPosix::RecvFrom(ComSat::Network::Address& address, uint8_t* data, uint32_t& size) {
    memset(data, 0, size);
    socklen_t length = address.m_length;
    ssize_t received = recvfrom(static_cast<int>(this->m_socket), data, size, 0, reinterpret_cast<sockaddr*>(&address.m_family), &length);
    address.m_length = static_cast<int32_t>(length);
    if (received <= 0) {
        return false;
    }
    size = static_cast<uint32_t>(received);
    return true;
}
