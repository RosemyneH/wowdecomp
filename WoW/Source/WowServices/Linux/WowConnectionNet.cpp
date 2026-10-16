#if defined(WOWDECOMP_LINUX)

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "WoW/Source/WowServices/WowConnectionNet.h"

static constexpr uint32_t MAXIMUM_WAIT_OBJECTS = 64;
static int32_t s_wakePipe[2] = { -1, -1 };

static void SetWakeEvent() {
    uint8_t signal = 1;
    static_cast<void>(write(s_wakePipe[1], &signal, sizeof(signal)));
}

static void ResetWakeEvent() {
    uint8_t buffer[64];

    while (read(s_wakePipe[0], buffer, sizeof(buffer)) > 0) {
    }
}

// OFFSET: 0x004697E0
void WowConnectionNet::PlatformChangeState(WowConnection* connection, WOW_CONN_STATE state) {
    SetWakeEvent();
}

// OFFSET: 0x00469860
void WowConnectionNet::PlatformRun() {
    this->m_networkStarted = true;

    int32_t timeout = 500;

    if (this->m_stop) {
        return;
    }

    do {
        WowConnection* connections[MAXIMUM_WAIT_OBJECTS];
        pollfd handles[MAXIMUM_WAIT_OBJECTS];

        this->m_connectionsLock.Enter();

        connections[0] = nullptr;
        handles[0] = { s_wakePipe[0], POLLIN, 0 };
        uint32_t count = 1;

        for (WowConnection* connection = this->m_connections.Head(); connection; connection = this->m_connections.Next(connection)) {
            if (connection->m_serviceCount) {
                continue;
            }

            int16_t events;

            switch (connection->m_connState) {
                case WOWC_CONNECTING:
                    events = POLLOUT;
                    break;

                case WOWC_LISTENING:
                    events = POLLIN;
                    break;

                case WOWC_CONNECTED:
                    events = (connection->m_sendList.Head() || connection->m_notifyWriteReady ? POLLOUT : 0) | POLLIN;
                    break;

                case WOWC_DISCONNECTING:
                    events = 0;
                    timeout = 0;
                    break;

                default:
                    continue;
            }

            if (count < MAXIMUM_WAIT_OBJECTS) {
                connection->m_refCount.Increment();
                handles[count] = { connection->m_sock, events, 0 };
                connections[count] = connection;
                count++;

                if (connection->m_lagTestRecvList.Head()) {
                    timeout = 0;
                }
            }
        }

        this->m_connectionsLock.Leave();

        if (poll(handles, count, timeout) >= 0) {
            if (handles[0].revents & POLLIN) {
                ResetWakeEvent();
            }

            for (uint32_t i = 1; i < count; i++) {
                WowConnection* connection = connections[i];
                int16_t revents = handles[i].revents;

                uint32_t flags = 0;

                if (revents & (POLLIN | POLLHUP | POLLERR)) {
                    flags = 2;
                }

                if (revents & (POLLOUT | POLLHUP | POLLERR)) {
                    flags |= 1;
                }

                if (connection->m_connState == WOWC_DISCONNECTING) {
                    flags |= 8;
                }

                if (WowConnection::s_lagTestDelayMin || WowConnection::s_lagTestDelayMax) {
                    flags |= 3;
                    timeout = 10;
                } else {
                    timeout = 500;
                }

                if (flags) {
                    this->SignalWorker(connection, flags);
                }
            }
        }

        for (uint32_t i = 1; i < count; i++) {
            connections[i]->Release();
        }
    } while (!this->m_stop);
}

// OFFSET: 0x004696E0
void WowConnectionNet::PlatformInit(bool useEngine) {
    if (!pipe2(s_wakePipe, O_NONBLOCK | O_CLOEXEC)) {
        this->m_event = s_wakePipe;
    }
}

// OFFSET: 0x00469740
void WowConnectionNet::PlatformDestruct(WowConnection* connection) {
    connection->m_event = nullptr;
}

// OFFSET: 0x00469770
void WowConnectionNet::PlatformWorkerReady() {
    SetWakeEvent();
}

// OFFSET: 0x00469780
void WowConnectionNet::PlatformAdd(WowConnection* connection) {
    int32_t noDelay = 1;
    setsockopt(connection->m_sock, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

    SetWakeEvent();
}

// OFFSET: 0x004697D0
void WowConnectionNet::PlatformRemove(WowConnection* connection) {
    SetWakeEvent();
}

// OFFSET: 0x005EEB70
void WowConnectionNet::PlatformStop() {
}

// OFFSET: 0x00469730
void WowConnectionNet::PlatformDestroy() {
    close(s_wakePipe[0]);
    close(s_wakePipe[1]);
    s_wakePipe[0] = -1;
    s_wakePipe[1] = -1;
    this->m_event = nullptr;
}

#endif
