#include "Engine/Source/ComSat/ComSatNetwork.h"
#include "Engine/Source/ComSat/ComSatAllocator.h"
#include <cstring>
#include <new>

// INLINED
bool ComSat::Network::Session::LocalTalker::operator==(const ComSat::Network::Session::LocalTalker& other) const {
    return this->m_talker == other.m_talker;
}

// INLINED
bool ComSat::Network::Session::RemoteTalker::operator==(const ComSat::Network::Session::RemoteTalker& other) const {
    return this->m_talker == other.m_talker;
}

// VERIFY: 5934156a auto
// OFFSET: 0x00885D70
void ComSat::Network::Session::SetNetworkId(uint16_t networkId) {
    if (this->m_networkId != networkId) {
        this->m_networkId = networkId;
    }
}

// VERIFY: b8190494 auto
// OFFSET: 0x00885D90
void ComSat::Network::Session::Recv(uint64_t talker, const uint8_t* data, uint32_t size) {
    this->m_callback->Recv(talker, data, size);
}

// VERIFY: 1281d185 auto
// OFFSET: 0x00885E80
void ComSat::Network::Session::SetTalkerNetworkIdLocal(uint64_t talker, uint8_t networkId) {
    LocalTalker key;
    key.m_talker = talker;
    key.m_networkId = networkId;
    uint32_t index = 0;
    if (this->m_localTalkers.Find(key, index)) {
        this->m_localTalkers[index].m_networkId = networkId;
    }
}

// VERIFY: 3c6357f0 auto
// OFFSET: 0x00885ED0
void ComSat::Network::Session::SetTalkerNetworkIdRemote(uint64_t talker, uint8_t networkId) {
    RemoteTalker key;
    key.m_talker = talker;
    key.m_networkId = networkId;
    key.m_endpoint = nullptr;
    uint32_t index = 0;
    if (this->m_remoteTalkers.Find(key, index)) {
        this->m_remoteTalkers[index].m_networkId = networkId;
    }
}

// VERIFY: 58a3362f manual
// OFFSET: 0x00885F20
bool ComSat::Network::Session::Send(uint64_t talker, const uint8_t* data, uint32_t size) {
    uint8_t networkId = 0;
    uint32_t i = 0;
    uint32_t count = this->m_localTalkers.Count();
    if (count) {
        for (; i < count; i++) {
            if (this->m_localTalkers[i].m_talker == talker) {
                networkId = this->m_localTalkers[i].m_networkId;
                break;
            }
        }
    }
    if (i == count) {
        return false;
    }
    for (uint32_t j = 0; j < this->m_endpoints.Count(); j++) {
        ComSat::Network::Endpoint* endpoint = this->m_endpoints[j];
        this->m_manager->Send(this->m_networkId, networkId, endpoint->m_key, endpoint->m_address, data, size);
    }
    this->m_lastSendTime = this->m_manager->m_time;
    return true;
}

// VERIFY: e3cf28b9 auto
// OFFSET: 0x00885FC0
void ComSat::Network::Session::Work() {
    if (this->m_localTalkers.Count() && static_cast<int32_t>(this->m_manager->m_time - this->m_lastSendTime) > 10000) {
        uint8_t networkId = this->m_localTalkers[0].m_networkId;
        for (uint32_t i = 0; i < this->m_endpoints.Count(); i++) {
            ComSat::Network::Endpoint* endpoint = this->m_endpoints[i];
            this->m_manager->Send(this->m_networkId, networkId, endpoint->m_key, endpoint->m_address, nullptr, 0);
        }
        this->m_lastSendTime = this->m_manager->m_time;
    }
}

// VERIFY: 22e4bf9c manual
// OFFSET: 0x00886030
bool ComSat::Network::Session::Resolve(uint8_t networkId, const ComSat::Network::Address& address, uint64_t& talker, const uint8_t (*&key)[16]) {
    for (uint32_t i = 0; i < this->m_remoteTalkers.Count(); i++) {
        RemoteTalker& remote = this->m_remoteTalkers[i];
        if (remote.m_networkId == networkId && remote.m_endpoint->m_address == address) {
            talker = this->m_remoteTalkers[i].m_talker;
            key = &this->m_remoteTalkers[i].m_endpoint->m_key;
            return true;
        }
    }
    return false;
}

// VERIFY: a894f8f5 auto
// OFFSET: 0x00886110
ComSat::Network::Session::Session(ComSat::Network::Manager& manager, uint64_t id, ComSat::Network::Session::Callback& callback) : m_localTalkers(*manager.m_allocator), m_endpoints(*manager.m_allocator), m_remoteTalkers(*manager.m_allocator) {
    this->m_id = id;
    this->m_manager = &manager;
    this->m_networkId = 0;
    this->m_lastSendTime = 0;
    this->m_callback = &callback;
}

// VERIFY: 64b74af2 manual
// OFFSET: 0x00886170
ComSat::Network::Session::~Session() {
    while (this->m_endpoints.Count()) {
        ComSat::Network::Endpoint* endpoint = this->m_endpoints[0];
        this->m_endpoints.Remove(endpoint);
        this->m_manager->m_allocator->Free(endpoint);
    }
}

// VERIFY: 924481df manual
// OFFSET: 0x00886230
bool ComSat::Network::Session::Remove(uint64_t talker) {
    RemoteTalker key;
    key.m_talker = talker;
    key.m_networkId = 0;
    key.m_endpoint = nullptr;
    uint32_t index = 0;
    if (this->m_remoteTalkers.Find(key, index)) {
        ComSat::Network::Endpoint* endpoint = this->m_remoteTalkers[index].m_endpoint;
        if (endpoint->m_refCount-- == 1) {
            this->m_endpoints.Remove(endpoint);
            this->m_manager->m_allocator->Free(endpoint);
        }
        this->m_remoteTalkers.RemoveAt(index);
        return true;
    }
    LocalTalker local;
    local.m_talker = talker;
    local.m_networkId = 0;
    return this->m_localTalkers.Remove(local);
}

// VERIFY: 3e4d527c manual
// OFFSET: 0x00886300
bool ComSat::Network::Session::LocalAdd(uint64_t talker) {
    LocalTalker key;
    key.m_talker = talker;
    key.m_networkId = 0;
    uint32_t index;
    if (this->m_localTalkers.Find(key, index)) {
        return false;
    }
    this->m_localTalkers.PushBack(key);
    return true;
}

// VERIFY: 124a5ef6 manual
// OFFSET: 0x00886380
bool ComSat::Network::Session::Add(uint64_t talker, uint8_t networkId, const uint8_t (&key)[16], const ComSat::Network::Address& address) {
    RemoteTalker remote;
    remote.m_talker = talker;
    remote.m_networkId = networkId;
    remote.m_endpoint = nullptr;
    uint32_t index;
    if (this->m_remoteTalkers.Find(remote, index)) {
        ComSat::Network::Endpoint* existing = this->m_remoteTalkers[index].m_endpoint;
        memcpy(&existing->m_address, &address, sizeof(existing->m_address));
        memcpy(existing->m_key, key, sizeof(existing->m_key));
        this->m_remoteTalkers[index].m_networkId = networkId;
        return true;
    }
    ComSat::Network::Endpoint* endpoint = nullptr;
    for (uint32_t i = 0; i < this->m_endpoints.Count(); i++) {
        if (this->m_endpoints[i]->m_address == address && !memcmp(this->m_endpoints[i]->m_key, key, sizeof(key))) {
            endpoint = this->m_endpoints[i];
            break;
        }
    }
    if (!endpoint) {
        void* memory = this->m_manager->m_allocator->Alloc(sizeof(ComSat::Network::Endpoint));
        if (!memory) {
            return false;
        }
        endpoint = new (memory) ComSat::Network::Endpoint;
        memcpy(&endpoint->m_address, &address, sizeof(endpoint->m_address));
        memcpy(endpoint->m_key, key, sizeof(endpoint->m_key));
        endpoint->m_refCount = 0;
        this->m_endpoints.PushBack(endpoint);
        for (uint32_t i = 0; i < this->m_localTalkers.Count(); i++) {
            this->m_manager->Send(this->m_networkId, this->m_localTalkers[i].m_networkId, key, address, nullptr, 0);
        }
    }
    endpoint->m_refCount++;
    remote.m_networkId = networkId;
    remote.m_endpoint = endpoint;
    this->m_remoteTalkers.PushBack(remote);
    return true;
}
