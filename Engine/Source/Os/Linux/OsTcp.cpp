#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "Engine/Source/Os/OsTcp.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

static_assert(sizeof(sockaddr_in) == sizeof(NETADDR), "NETADDR must hold a sockaddr_in");

// OFFSET: 0x0086DBF0
void OsNetAddrMake(uint32_t address, uint16_t port, NETADDR* addr) {
    sockaddr_in* in = reinterpret_cast<sockaddr_in*>(addr);
    in->sin_family = AF_INET;
    in->sin_port = htons(port);
    in->sin_addr.s_addr = address;
    memset(in->sin_zero, 0, sizeof(in->sin_zero));
}

// OFFSET: 0x0086DC20
uint32_t OsNetAddrGetAddress(const NETADDR* addr, uint16_t* port) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(addr);

    if (port) {
        *port = ntohs(in->sin_port);
    }

    return in->sin_addr.s_addr;
}

// OFFSET: 0x0086DC50
const char* OsNetAddrToStr(const NETADDR* addr, char* buffer, uint32_t size) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(addr);

    char address[INET_ADDRSTRLEN];
    const char* text = inet_ntop(AF_INET, &in->sin_addr, address, sizeof(address));
    if (!text) {
        text = "0.0.0.0";
    }
    SStrPrintf(buffer, size, "%s:%u", text, ntohs(in->sin_port));

    return buffer;
}

// OFFSET: 0x0086F1C0
void OsNetPump(uint32_t timeout) {
    OsSleep(timeout);
}
