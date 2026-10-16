#if defined(WOWDECOMP_ARCH_32)

#include <winsock2.h>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/ComSat/ComSatNetwork.h"

// VERIFY: 4d04cc78 auto
// OFFSET: 0x00885C40
void ComSat::Network::Address::SetPort(int32_t port) {
    this->m_port = htons(static_cast<u_short>(port));
}

// VERIFY: f308d459 auto
// OFFSET: 0x00885C60
int32_t ComSat::Network::Address::GetPort() const {
    return ntohs(this->m_port);
}

// VERIFY: 18ffc29e manual
// OFFSET: 0x00885CD0
void ComSat::Network::Address::SetAddress(const char* address) {
    if (!address) {
        return;
    }
    const char* colon = strchr(address, ':');
    if (colon) {
        if (strlen(address) < 128) {
            char host[128];
            strcpy(host, address);
            host[colon - address] = '\0';
            uint32_t ip = inet_addr(host);
            if (ip != INADDR_NONE || strcmp(host, "255.255.255.255") == 0) {
                this->m_ip = ip;
            }
            this->m_port = htons(static_cast<u_short>(strtol(colon + 1, nullptr, 10)));
        }
    } else {
        uint32_t ip = inet_addr(address);
        if (ip != INADDR_NONE || strcmp(address, "255.255.255.255") == 0) {
            this->m_ip = ip;
        }
    }
}

#endif
