#include "Engine/Source/ComSat/ComSatNetwork.h"
#include <new>

// VERIFY: 63d037b6 manual
// OFFSET: 0x00888780
ComSat::Network::Socket::~Socket() {
}

// VERIFY: 4679db72 manual
// OFFSET: 0x00888980
ComSat::Network::Socket* ComSat::Network::Socket::Create(ComSat::Allocator& allocator) {
    void* memory = allocator.Alloc(sizeof(ComSat::Network::SocketPosix));
    if (!memory) {
        return nullptr;
    }
    return new (memory) ComSat::Network::SocketPosix();
}
