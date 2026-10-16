#include "Engine/Source/ComSat/ComSatSoundManager.h"
#include "Engine/Source/ComSat/ComSatAllocator.h"
#include "Engine/Source/ComSat/ComSatSoundLevelCalculator.h"
#include "Engine/Source/Os/Processor.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

// STATIC: 0x00D43990
static uint32_t s_lastEncodeTime;

// STATIC: 0x00D43994
static int32_t s_jitterBufferSize = (ComSat::Sound::CODECIPMR6000::s_sampleRate * ComSat::Sound::CODECIPMR6000::s_bitsPerSample / 8) / ComSat::Sound::CODECIPMR6000::s_bytesPerFrame;

// STATIC: 0x00D43998
static int32_t s_frameTimeWindow = (ComSat::Sound::CODECIPMR6000::s_sampleRate * ComSat::Sound::CODECIPMR6000::s_bitsPerSample / 8 * 2) / ComSat::Sound::CODECIPMR6000::s_bytesPerFrame;

// VERIFY: 83e2f3a6 auto
// OFFSET: 0x0087F930
ComSat::Sound::Manager::Callback::~Callback() {
}

// VERIFY: 58ec21e6 auto
// OFFSET: 0x00881260
ComSat::Sound::Manager::RemoteTalker::RemoteTalker(ComSat::Allocator& allocator) : m_allocator(&allocator), m_packets(nullptr), m_frameTime(allocator) {
    this->m_packets = static_cast<VoicePacket*>(allocator.Alloc(sizeof(VoicePacket) * s_jitterBufferSize));
}

// VERIFY: 68ac5962 auto
// OFFSET: 0x008812B0
void ComSat::Sound::Manager::SetCallback(ComSat::Sound::Manager::Callback* callback) {
    this->m_callback = callback;
}

// VERIFY: 95032179 auto
// OFFSET: 0x008812C0
bool ComSat::Sound::Manager::LocalTalkerChangeOutputDevice(uint8_t player, int32_t device) {
    if (this->m_io) {
        return this->m_io->LocalPlayerChangeOutputDevice(player, device);
    }
    return false;
}

// VERIFY: f7e4bc4b auto
// OFFSET: 0x008812E0
bool ComSat::Sound::Manager::LocalTalkerChangeCaptureDevice(uint8_t player, int32_t device) {
    if (this->m_io) {
        return this->m_io->LocalPlayerChangeCaptureDevice(player, device);
    }
    return false;
}

// VERIFY: eab244f7 manual
// OFFSET: 0x00881300
int32_t ComSat::Sound::Manager::LocalTalkerGetCurrentOutputDeviceIndex(uint8_t player) {
    if (this->m_io) {
        return this->m_io->LocalPlayerGetCurrentOutputDeviceIndex(player);
    }
    return -1;
}

// VERIFY: dbc4fd0b manual
// OFFSET: 0x00881320
int32_t ComSat::Sound::Manager::LocalTalkerGetCurrentCaptureDeviceIndex(uint8_t player) {
    if (this->m_io) {
        return this->m_io->LocalPlayerGetCurrentCaptureDeviceIndex(player);
    }
    return -1;
}

// VERIFY: 6af853ca manual
// OFFSET: 0x00881340
bool ComSat::Sound::Manager::LocalTalkerThreshold(uint8_t player, float threshold) {
    if (!this->m_codec) {
        return false;
    }
    this->m_codec->SetThreshold(player, threshold);
    return true;
}

// VERIFY: 7e70ce48 auto
// OFFSET: 0x00881370
bool ComSat::Sound::Manager::CanBitpackEncodedData() {
    return ComSat::Sound::CODEC::CanBitpackFrames();
}

// VERIFY: 996b2e46 auto
// OFFSET: 0x00881370
bool ComSat::Sound::Manager::IsUsingVariableBitrateCodec() {
    return ComSat::Sound::CODEC::IsEncodedBitrateVariable();
}

// VERIFY: 5b24b547 auto
// OFFSET: 0x00881380
int32_t ComSat::Sound::Manager::GetMinEncodedFrameLength() {
    return ComSat::Sound::CODECIPMR6000::s_minEncodedFrameLength;
}

// VERIFY: b74c7db0 auto
// OFFSET: 0x00881390
int32_t ComSat::Sound::Manager::GetMaxEncodedFrameLength() {
    return ComSat::Sound::CODECIPMR6000::s_maxEncodedFrameLength;
}

// VERIFY: 7308e0a0 auto
// OFFSET: 0x00437720
bool ComSat::Sound::Manager::IsRemoteTalkerTalkingInSession(uint64_t session, uint8_t player, uint64_t talker) {
    return false;
}

// VERIFY: 2049629b auto
// OFFSET: 0x008813A0
bool ComSat::Sound::Manager::IsLocalPlayerTalking(uint8_t player) {
    return false;
}

// VERIFY: c4f9bc66 manual
// OFFSET: 0x008813B0
int32_t ComSat::Sound::Manager::EnumerateOutputDevices(ComSat::DeviceInfo* devices, int32_t maxDevices) {
    if (!this->m_io) {
        return 0;
    }
    return this->m_io->EnumerateOutputDevices(devices, maxDevices);
}

// VERIFY: e5b0261b manual
// OFFSET: 0x008813D0
int32_t ComSat::Sound::Manager::EnumerateCaptureDevices(ComSat::DeviceInfo* devices, int32_t maxDevices) {
    if (!this->m_io) {
        return 0;
    }
    return this->m_io->EnumerateCaptureDevices(devices, maxDevices);
}

// VERIFY: cf900560 manual
// OFFSET: 0x008813F0
void ComSat::Sound::Manager::AdvancePlayCursorForRemoteTalker(ComSat::Sound::Manager::RemoteTalker* talker) {
    int32_t cursor = static_cast<int32_t>(talker->m_playCursor);
    talker->m_playCursor += ComSat::Sound::CODECIPMR6000::s_samplesPerFrame * 1000.0f / ComSat::Sound::CODECIPMR6000::s_sampleRate / talker->m_frameTime.GetCurrentAverage();
    int32_t end = static_cast<int32_t>(talker->m_playCursor) % s_jitterBufferSize;
    talker->m_playCursor = fmod(talker->m_playCursor, static_cast<double>(s_jitterBufferSize));
    for (; cursor != end; cursor = (cursor + 1) % s_jitterBufferSize) {
        talker->m_packets[cursor].m_valid = false;
    }
}

// VERIFY: ced4a441 auto
// OFFSET: 0x00881480
void ComSat::Sound::Manager::DebugSetSequenceDelay(int32_t delay) {
    this->m_sequenceDelay = this->m_jitterDelay + delay;
}

// VERIFY: de383fb8 manual
// OFFSET: 0x00881490
void ComSat::Sound::Manager::DebugSetSoundOutputDelay(uint8_t player, int32_t delay) {
    if (this->m_io) {
        this->m_io->DebugSetSoundOutputDelay(player, delay);
    }
}

// VERIFY: 7e52a2ba auto
// OFFSET: 0x008814F0
ComSat::Sound::Manager::RemoteTalker::~RemoteTalker() {
    this->m_allocator->Free(this->m_packets);
}

// VERIFY: b4173bb3 auto
// OFFSET: 0x00881C50
ComSat::Sound::Manager::LocalPlayer::~LocalPlayer() {
}

// VERIFY: 73854b3e auto
// OFFSET: 0x00881CA0
ComSat::Sound::Manager::LocalPlayer::LocalPlayer(ComSat::Allocator& allocator) : m_sessionPriorities(allocator), m_mutedSessions(allocator), m_mutedTalkers(allocator), m_talkerVolumes(allocator), m_sessionVolumes(allocator) {
}

// INLINED
ComSat::Sound::Manager::Session::Session(ComSat::Allocator& allocator) : m_id(0), m_talkerPriorities(allocator) {
}

// VERIFY: 4458ca17 manual
// OFFSET: 0x00881CF0
ComSat::Sound::Manager::Manager(const ComSat::Initialization& init, ComSat::Allocator& allocator) : m_localPlayers(allocator), m_sessions(allocator), m_remoteTalkers(allocator) {
    this->m_maxLocalPlayers = init.m_maxLocalPlayers;
    this->m_allocator = &allocator;
    this->m_callback = nullptr;
    this->m_codec = nullptr;
    this->m_io = ComSat::Sound::IO::Create(init, *this, allocator);
    this->m_jitterDelay = 5;
    this->m_sequenceDelay = 5;
    this->m_maxChannels = init.m_maxChannels;
    this->m_currentFrame = 0;
    this->m_startTime = init.m_time;
    this->m_currentTime = init.m_time;
    if (OsGetProcessorFeatures() & 4) {
        this->m_codec = ComSat::Sound::CODEC::Create(init, allocator);
        int32_t bytesPerSecond = ComSat::Sound::CODECIPMR6000::s_sampleRate * ComSat::Sound::CODECIPMR6000::s_bitsPerSample / 8;
        this->m_framesPerSecond = static_cast<float>(bytesPerSecond) / ComSat::Sound::CODECIPMR6000::s_bytesPerFrame;
        this->m_holdFrames = static_cast<int32_t>(ceil(this->m_framesPerSecond * 500.0f * 0.001f));
        this->m_frameDuration = ComSat::Sound::CODECIPMR6000::s_samplesPerFrame * 1000.0f / ComSat::Sound::CODECIPMR6000::s_sampleRate;
        this->m_jitterDelay = static_cast<int32_t>(this->m_framesPerSecond * 0.1f);
        if (!this->m_jitterDelay) {
            this->m_jitterDelay = 1;
        }
        this->m_sequenceDelay = 2 * this->m_jitterDelay;
    }
}

// VERIFY: 2e83be4f manual
// OFFSET: 0x00881E20
bool ComSat::Sound::Manager::LocalTalkerEnable(uint8_t player, bool enable) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    if (!enable && localPlayer->m_enabled && this->m_callback) {
        this->m_callback->LocalChatDataReady(player, nullptr, 0, 0);
    }
    localPlayer->m_enabled = enable;
    return true;
}

// VERIFY: ed8ef859 auto
// OFFSET: 0x00881EC0
bool ComSat::Sound::Manager::LocalTalkerEnableVoiceLevelCallback(uint8_t player, bool enable) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    localPlayer->m_voiceLevelCallback = enable;
    return true;
}

// VERIFY: c25f698a manual
// OFFSET: 0x00881F30
int32_t ComSat::Sound::Manager::SessionNumMembers(uint64_t session) {
    Session* info;
    if (!this->m_sessions.Get(ComSat::MapKey<uint64_t>(session), info)) {
        return -1;
    }
    int32_t count = info->m_talkerPriorities.m_used;
    uint32_t iterator = 0;
    ComSat::MapKey<uint8_t> key;
    LocalPlayer* localPlayer;
    while (this->m_localPlayers.Next(iterator, key, localPlayer)) {
        uint32_t index;
        if (localPlayer->m_sessionPriorities.Find(ComSat::MapKey<uint64_t>(session), index)) {
            count++;
        }
    }
    return count;
}

// VERIFY: b51edc25 manual
// OFFSET: 0x00882030
bool ComSat::Sound::Manager::SessionGetTalkerByIndex(uint64_t session, int32_t index, uint64_t& talker, uint8_t& player) {
    if (index >= this->SessionNumMembers(session)) {
        return false;
    }
    int32_t count = 0;
    LocalPlayer* localPlayer = nullptr;
    uint32_t iterator = 0;
    ComSat::MapKey<uint8_t> playerKey;
    while (this->m_localPlayers.Next(iterator, playerKey, localPlayer)) {
        uint32_t found;
        if (localPlayer->m_sessionPriorities.Find(ComSat::MapKey<uint64_t>(session), found)) {
            if (index == count) {
                talker = 0;
                player = playerKey.m_value;
                return true;
            }
            count++;
        }
    }
    player = 0xFF;
    iterator = 0;
    Session* info;
    if (!this->m_sessions.Get(ComSat::MapKey<uint64_t>(session), info)) {
        return false;
    }
    ComSat::MapKey<uint64_t> talkerKey;
    uint8_t priority;
    if (info->m_talkerPriorities.Next(iterator, talkerKey, priority)) {
        while (count < index) {
            count++;
            if (!info->m_talkerPriorities.Next(iterator, talkerKey, priority)) {
                break;
            }
        }
    }
    if (count != index) {
        return false;
    }
    talker = talkerKey.m_value;
    return true;
}

// VERIFY: 8f19331f manual
// OFFSET: 0x008821C0
void ComSat::Sound::Manager::RecenterJitterBufferForRemoteTalker(uint64_t talker, int32_t offset) {
    RemoteTalker* remote;
    if (!this->m_remoteTalkers.Get(ComSat::MapKey<uint64_t>(talker), remote)) {
        return;
    }
    VoicePacket* packets = static_cast<VoicePacket*>(this->m_allocator->Alloc(sizeof(VoicePacket) * s_jitterBufferSize));
    memcpy(packets, remote->m_packets, sizeof(VoicePacket) * s_jitterBufferSize);
    for (int32_t i = 0; i < s_jitterBufferSize; i++) {
        remote->m_packets[i].m_valid = false;
    }
    int32_t cursor = static_cast<int32_t>(remote->m_playCursor);
    for (int32_t i = 0; i < s_jitterBufferSize; i++) {
        if (!packets[i].m_valid) {
            continue;
        }
        int32_t slot = (offset + i) % s_jitterBufferSize;
        if (slot < 0) {
            slot += s_jitterBufferSize;
        }
        int32_t delay = this->m_jitterDelay;
        int32_t remaining = s_jitterBufferSize - cursor;
        if (slot < cursor) {
            if (remaining >= delay) {
                memcpy(&remote->m_packets[slot], &packets[i], sizeof(VoicePacket));
                continue;
            }
            delay -= s_jitterBufferSize;
        } else if (remaining < delay) {
            continue;
        }
        if (slot >= cursor + delay) {
            memcpy(&remote->m_packets[slot], &packets[i], sizeof(VoicePacket));
        }
    }
    this->m_allocator->Free(packets);
}

// VERIFY: 6b28c289 auto
// OFFSET: 0x00882330
void ComSat::Sound::Manager::SubmitIncomingFrameTimeData(uint64_t talker, uint16_t sequence) {
    RemoteTalker* remote;
    if (!this->m_remoteTalkers.Get(ComSat::MapKey<uint64_t>(talker), remote)) {
        return;
    }
    uint32_t lastTime = remote->m_lastPacketTime;
    if (lastTime && lastTime != this->m_currentTime) {
        double elapsed = static_cast<double>(this->m_currentTime) - lastTime;
        uint16_t lastSequence = remote->m_lastSequence;
        int32_t frames = sequence - lastSequence;
        if (frames < 0) {
            frames += 0x10000;
        }
        if (frames > 0 && lastSequence) {
            if (frames > s_frameTimeWindow) {
                frames = s_frameTimeWindow;
            }
            if (frames > 0) {
                double frameTime = elapsed / frames;
                for (int32_t i = frames; i; i--) {
                    remote->m_frameTime.SubmitData(frameTime);
                }
            }
        }
        remote->m_lastSequence = sequence;
    }
    remote->m_lastPacketTime = this->m_currentTime;
}

// VERIFY: 35250d18 manual
// OFFSET: 0x00882440
void ComSat::Sound::Manager::SubmitIncomingVoicePacket(uint64_t session, uint64_t talker, uint8_t* data, uint32_t size, bool reliable) {
    if (size > static_cast<uint32_t>(ComSat::Sound::CODECIPMR6000::s_maxEncodedFrameLength + 4)) {
        return;
    }
    RemoteTalker* remote;
    if (!this->m_remoteTalkers.Get(ComSat::MapKey<uint64_t>(talker), remote)) {
        return;
    }
    VoicePacket* packets = remote->m_packets;
    VoicePacket* packet = reinterpret_cast<VoicePacket*>(data);
    int32_t cursor = static_cast<int32_t>(remote->m_playCursor);
    int32_t slot = cursor;
    int32_t skipped = 0;
    int32_t target;
    while (!packets[slot].m_valid) {
        slot = (slot + 1) % s_jitterBufferSize;
        skipped++;
        if (slot == cursor) {
            target = (cursor + this->m_sequenceDelay) % s_jitterBufferSize;
            remote->m_recenterTime = this->m_currentTime;
            goto store;
        }
    }
    target = cursor + skipped + (packet->m_sequence - packets[slot].m_sequence);
    if (this->m_currentTime != remote->m_recenterTime && !reliable) {
        remote->m_recenterTime = this->m_currentTime;
        int32_t expected = cursor + this->m_sequenceDelay;
        if (abs(target - expected) > 2) {
            int32_t delta = expected - target;
            target = expected;
            this->RecenterJitterBufferForRemoteTalker(talker, delta);
        }
    }
store:
    int32_t index = (target < 0 ? 0 : target) % s_jitterBufferSize;
    memcpy(&remote->m_packets[index], data, size);
    remote->m_packets[index].m_valid = true;
    remote->m_packets[index].m_session = session;
}

// VERIFY: ff2f231b manual
// OFFSET: 0x008825D0
uint32_t ComSat::Sound::Manager::CalculateTotalVoicePriorityForRemoteTalkerInSessionForLocalPlayer(uint8_t player, uint64_t talker, uint64_t session) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return 0;
    }
    uint32_t index;
    if (localPlayer->m_mutedSessions.Find(session, index)) {
        return 0;
    }
    if (localPlayer->m_mutedTalkers.Find(talker, index)) {
        return 0;
    }
    uint8_t sessionPriority = 0;
    if (!localPlayer->m_sessionPriorities.Get(ComSat::MapKey<uint64_t>(session), sessionPriority)) {
        return 0;
    }
    Session* info;
    if (sessionPriority && this->m_sessions.Get(ComSat::MapKey<uint64_t>(session), info)) {
        uint8_t talkerPriority = 0;
        if (info->m_talkerPriorities.Get(ComSat::MapKey<uint64_t>(talker), talkerPriority) && talkerPriority) {
            return talkerPriority + (sessionPriority << 8);
        }
    }
    return 0;
}

// VERIFY: 667d951e manual
// OFFSET: 0x00882700
bool ComSat::Sound::Manager::DecodeVoicePacket(uint8_t player, int32_t channel, ComSat::Sound::Manager::VoicePacket* packet, uint32_t* size) {
    LocalPlayer* localPlayer;
    if (!this->m_codec || !this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    *size = sizeof(localPlayer->m_buffer);
    return this->m_codec->DecompressEncodedData(player, channel, packet->m_data, packet->m_bits, localPlayer->m_buffer, size);
}

// VERIFY: 1c265579 auto
// OFFSET: 0x00882780
void ComSat::Sound::Manager::WriteVoiceDataToHeadsetDeviceBuffer(uint8_t player, int32_t channel, uint32_t size, double rate) {
    if (!size) {
        return;
    }
    LocalPlayer* localPlayer;
    if (this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer) && this->m_io) {
        this->m_io->PlayDataForPlayer(player, channel, localPlayer->m_buffer, size, rate);
    }
}

// VERIFY: 9c3ab779 auto
// OFFSET: 0x00882810
void ComSat::Sound::Manager::TransferEncodedVoiceDataForLocalPlayer(uint8_t player, const uint8_t* data, uint32_t bits, uint32_t time) {
    if (!data || bits > static_cast<uint32_t>(ComSat::Sound::CODECIPMR6000::s_maxEncodedFrameLength) || bits < static_cast<uint32_t>(ComSat::Sound::CODECIPMR6000::s_minEncodedFrameLength)) {
        return;
    }
    LocalPlayer* localPlayer;
    if (this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        uint32_t bytes = bits >> 3;
        VoicePacket packet;
        packet.m_sequence = localPlayer->m_sequence;
        packet.m_bits = bits;
        if (bits & 7) {
            bytes++;
        }
        memcpy(packet.m_data, data, bytes);
        this->m_callback->LocalChatDataReady(player, reinterpret_cast<uint8_t*>(&packet), bytes + 4, time);
    }
}

// VERIFY: abf0507c auto
// OFFSET: 0x008828E0
bool ComSat::Sound::Manager::IsRemoteTalkerMuted(uint8_t player, uint64_t talker) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    uint32_t index;
    return localPlayer->m_mutedTalkers.Find(talker, index);
}

// VERIFY: e3a12132 manual
// OFFSET: 0x00882950
bool ComSat::Sound::Manager::SetTalkerMuted(uint8_t player, uint64_t talker, ComSat::MuteSetting setting) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    uint32_t index;
    bool mute = setting == ComSat::MUTE_SETTING_MUTE;
    if ((setting == ComSat::MUTE_SETTING_TOGGLE && !localPlayer->m_mutedTalkers.Find(talker, index)) || mute) {
        if (!localPlayer->m_mutedTalkers.Find(talker, index)) {
            localPlayer->m_mutedTalkers.PushBack(talker);
        }
        return true;
    }
    return localPlayer->m_mutedTalkers.Remove(talker);
}

// VERIFY: b712fdb6 auto
// OFFSET: 0x00882A00
bool ComSat::Sound::Manager::IsSessionMuted(uint8_t player, uint64_t session) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    uint32_t index;
    return localPlayer->m_mutedSessions.Find(session, index);
}

// VERIFY: d8c69071 manual
// OFFSET: 0x00882A70
bool ComSat::Sound::Manager::SetSessionMuted(uint8_t player, uint64_t session, ComSat::MuteSetting setting) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    uint32_t index;
    bool mute = setting == ComSat::MUTE_SETTING_MUTE;
    if ((setting == ComSat::MUTE_SETTING_TOGGLE && !localPlayer->m_mutedSessions.Find(session, index)) || mute) {
        if (!localPlayer->m_mutedSessions.Find(session, index)) {
            localPlayer->m_mutedSessions.PushBack(session);
        }
        return true;
    }
    return localPlayer->m_mutedSessions.Remove(session);
}

// VERIFY: df093e1a auto
// OFFSET: 0x00882B20
void ComSat::Sound::Manager::DeviceError(uint8_t player) {
    LocalPlayer* localPlayer;
    if (this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer) && !localPlayer->m_deviceError) {
        localPlayer->m_deviceError = true;
        this->m_callback->DeviceError(player);
    }
}

// VERIFY: 6fa4598d manual
// OFFSET: 0x00882CE0
void ComSat::Sound::Manager::AssignTalkersToChannelsForLocalPlayer(uint8_t player, int32_t channelCount, ComSat::Sound::Manager::TalkingTalkerInfo* infos) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return;
    }
    uint32_t priorities[4];
    if (channelCount > 0) {
        memset(priorities, 0, sizeof(uint32_t) * channelCount);
        for (int32_t i = 0; i < channelCount; i++) {
            infos[i].m_talker = 0;
            infos[i].m_session = 0;
            infos[i].m_active = false;
        }
    }
    for (int32_t c = 0; c < channelCount; c++) {
        TalkingTalkerInfo& current = localPlayer->m_channels[c];
        if (!current.m_talker) {
            continue;
        }
        uint64_t talker = current.m_talker;
        uint64_t session = current.m_session;
        RemoteTalker* remote;
        if (!this->m_remoteTalkers.Get(ComSat::MapKey<uint64_t>(talker), remote)) {
            continue;
        }
        double average = remote->m_frameTime.GetCurrentAverage();
        uint64_t best = 0;
        uint8_t bestPriority = 0;
        int32_t end = static_cast<int32_t>(ComSat::Sound::CODECIPMR6000::s_samplesPerFrame * 1000.0f / ComSat::Sound::CODECIPMR6000::s_sampleRate / average + remote->m_playCursor) % s_jitterBufferSize;
        for (int32_t slot = static_cast<int32_t>(remote->m_playCursor); slot != end; slot = (slot + 1) % s_jitterBufferSize) {
            VoicePacket& packet = remote->m_packets[slot];
            if (packet.m_valid) {
                uint8_t priority;
                if (localPlayer->m_sessionPriorities.Get(ComSat::MapKey<uint64_t>(packet.m_session), priority) && priority > bestPriority) {
                    bestPriority = priority;
                    best = remote->m_packets[slot].m_session;
                }
            }
        }
        if (best == session || !best) {
            uint32_t total = this->CalculateTotalVoicePriorityForRemoteTalkerInSessionForLocalPlayer(player, talker, session);
            if (total) {
                infos[c].m_talker = current.m_talker;
                infos[c].m_session = current.m_session;
                infos[c].m_active = best == session;
                infos[c].m_holdFrames = current.m_holdFrames;
                priorities[c] = total;
            }
        }
    }
    uint32_t iterator = 0;
    ComSat::MapKey<uint64_t> key;
    RemoteTalker* remote;
    while (this->m_remoteTalkers.Next(iterator, key, remote)) {
        uint64_t talker = remote->m_guid;
        uint64_t best = 0;
        double average = remote->m_frameTime.GetCurrentAverage();
        bool assigned = false;
        if (channelCount > 0) {
            for (int32_t i = 0; i < channelCount; i++) {
                if (infos[i].m_talker == talker) {
                    assigned = true;
                }
            }
            if (assigned) {
                continue;
            }
        }
        uint8_t bestPriority = 0;
        int32_t end = static_cast<int32_t>(ComSat::Sound::CODECIPMR6000::s_samplesPerFrame * 1000.0f / ComSat::Sound::CODECIPMR6000::s_sampleRate / average + remote->m_playCursor) % s_jitterBufferSize;
        int32_t slot = static_cast<int32_t>(remote->m_playCursor);
        if (slot == end) {
            continue;
        }
        do {
            VoicePacket& packet = remote->m_packets[slot];
            if (packet.m_valid) {
                uint8_t priority;
                if (localPlayer->m_sessionPriorities.Get(ComSat::MapKey<uint64_t>(packet.m_session), priority) && priority > bestPriority) {
                    bestPriority = priority;
                    best = remote->m_packets[slot].m_session;
                }
            }
            slot = (slot + 1) % s_jitterBufferSize;
        } while (slot != end);
        if (!best) {
            continue;
        }
        uint32_t total = this->CalculateTotalVoicePriorityForRemoteTalkerInSessionForLocalPlayer(player, talker, best);
        int32_t replace = -1;
        if (channelCount > 0) {
            for (int32_t i = 0; i < channelCount; i++) {
                if (total > priorities[i] && (replace == -1 || priorities[i] < priorities[replace])) {
                    replace = i;
                }
            }
            if (replace != -1) {
                priorities[replace] = total;
                infos[replace].m_talker = talker;
                infos[replace].m_session = best;
                infos[replace].m_active = true;
            }
        }
    }
}

// VERIFY: 2abf7a9a manual
// OFFSET: 0x008831C0
void ComSat::Sound::Manager::GenerateAndWriteSilenceToHeadsetDeviceBuffer(uint8_t player, int32_t channel, float frames, double rate) {
    LocalPlayer* localPlayer;
    if (!this->m_codec || !this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return;
    }
    this->m_codec->GenerateSilence(localPlayer->m_buffer, ComSat::Sound::CODECIPMR6000::s_bytesPerFrame);
    while (frames >= 1.0f) {
        this->WriteVoiceDataToHeadsetDeviceBuffer(player, channel, ComSat::Sound::CODECIPMR6000::s_bytesPerFrame, rate);
        frames -= 1.0f;
    }
}

// VERIFY: 4067ca64 auto
// OFFSET: 0x00883270
void ComSat::Sound::Manager::EncodeAvailableVoiceDataForLocalPlayer(uint8_t player, ComSat::Sound::Manager::LocalPlayer* localPlayer) {
    if (!this->m_io || !this->m_codec) {
        return;
    }
    int32_t available = this->m_io->GetAvailableDataBytes(player);
    int32_t attempts = 5;
    if (!available) {
        return;
    }
    int32_t blockSize = ComSat::Sound::CODECIPMR6000::s_samplesPerFrame * ComSat::Sound::CODECIPMR6000::s_bitsPerSample / 8;
    if (available < blockSize) {
        return;
    }
    while (attempts-- > 0) {
        int32_t read = this->m_io->ReadDataForPlayer(player, localPlayer->m_buffer, blockSize);
        s_lastEncodeTime = this->m_currentTime;
        if (read < blockSize) {
            break;
        }
        int32_t remaining = this->m_io->GetAvailableDataBytes(player);
        uint32_t bits = 1024;
        uint8_t encoded[128];
        bool transmit = localPlayer->m_enabled;
        uint32_t sequence = localPlayer->m_sequence;
        if (sequence < localPlayer->m_lastResetSequence) {
            sequence += 0x10000;
        }
        if (sequence - localPlayer->m_lastResetSequence >= static_cast<uint32_t>(ComSat::Sound::CODEC::MinimumSafeFramesBetweenResets()) && ComSat::Sound::CODEC::MinimumSafeFramesBetweenResets() && !(localPlayer->m_sequence % static_cast<int32_t>(ComSat::Sound::CODEC::MinimumSafeFramesBetweenResets()))) {
            localPlayer->m_resetEncoder = true;
        }
        if (transmit || localPlayer->m_voiceLevelCallback) {
            if (localPlayer->m_resetEncoder) {
                this->m_codec->ResetPlayerChannelEncoder(player);
                localPlayer->m_resetEncoder = false;
                localPlayer->m_lastResetSequence = localPlayer->m_sequence;
            }
            bool voice = false;
            if (!this->m_codec->CompressRawData(player, localPlayer->m_buffer, blockSize, encoded, &bits, &voice)) {
                transmit = false;
            }
            if (!voice) {
                transmit = false;
                localPlayer->m_resetEncoder = true;
            }
            if (localPlayer->m_voiceLevelCallback) {
                float level = 0.0f;
                float decibels = 0.0f;
                ComSat::Sound::LevelCalculator16Bit::CalculateLevelForFrame(localPlayer->m_buffer, blockSize, level, decibels);
                this->m_callback->LocalTalkerVoiceLevel(player, level, voice);
            }
        }
        if (transmit) {
            int32_t bytesPerSecond = ComSat::Sound::CODECIPMR6000::s_sampleRate * ComSat::Sound::CODECIPMR6000::s_bitsPerSample / 8;
            this->TransferEncodedVoiceDataForLocalPlayer(player, encoded, bits, 1000u / static_cast<uint32_t>(bytesPerSecond / blockSize));
            localPlayer->m_talking = true;
        } else if (localPlayer->m_talking && this->m_callback) {
            this->m_callback->LocalChatDataReady(player, nullptr, 0, 0);
            localPlayer->m_talking = false;
        }
        localPlayer->m_sequence++;
        if (remaining < blockSize) {
            break;
        }
    }
}

// VERIFY: a3cc71d9 manual
// OFFSET: 0x00883950
bool ComSat::Sound::Manager::InitLocalPlayer(uint8_t player, float threshold, int32_t outputDevice, int32_t captureDevice) {
    uint32_t index;
    if (this->m_localPlayers.Find(ComSat::MapKey<uint8_t>(player), index)) {
        return false;
    }
    void* memory = this->m_allocator->Alloc(sizeof(LocalPlayer));
    if (!memory) {
        return false;
    }
    LocalPlayer* localPlayer = new (memory) LocalPlayer(*this->m_allocator);
    localPlayer->m_sequence = 0;
    localPlayer->m_enabled = false;
    localPlayer->m_talking = false;
    localPlayer->m_deviceError = false;
    localPlayer->m_voiceLevelCallback = false;
    localPlayer->m_lastResetSequence = 0;
    for (int32_t i = 0; i < 4; i++) {
        localPlayer->m_channels[i].m_talker = 0;
        localPlayer->m_channels[i].m_session = 0;
        localPlayer->m_channels[i].m_active = false;
        localPlayer->m_channels[i].m_holdFrames = 0;
    }
    this->m_localPlayers.Insert(ComSat::MapKey<uint8_t>(player), localPlayer);
    if (this->m_io) {
        this->m_io->InitLocalPlayer(player, outputDevice, captureDevice, ComSat::Sound::CODECIPMR6000::s_sampleRate, ComSat::Sound::CODECIPMR6000::s_bitsPerSample, ComSat::Sound::CODECIPMR6000::s_samplesPerFrame);
    }
    if (this->m_codec && !this->m_codec->InitLocalPlayer(player, threshold)) {
        this->DestroyLocalPlayer(player);
        return false;
    }
    return true;
}

// VERIFY: d85d2efb auto
// OFFSET: 0x00883A90
bool ComSat::Sound::Manager::LocalTalkerSetRemoteTalkerVolume(uint8_t player, uint64_t talker, float volume) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    localPlayer->m_talkerVolumes[ComSat::MapKey<uint64_t>(talker)] = volume;
    return true;
}

// VERIFY: 6aaa876a auto
// OFFSET: 0x00883B30
bool ComSat::Sound::Manager::LocalTalkerSetSessionVolume(uint8_t player, uint64_t session, float volume) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    localPlayer->m_sessionVolumes[ComSat::MapKey<uint64_t>(session)] = volume;
    return true;
}

// VERIFY: 7f3bf85f manual
// OFFSET: 0x00883BD0
bool ComSat::Sound::Manager::SessionCreate(uint64_t session) {
    uint32_t index;
    if (!this->m_sessions.Find(ComSat::MapKey<uint64_t>(session), index)) {
        void* memory = this->m_allocator->Alloc(sizeof(Session));
        if (!memory) {
            return false;
        }
        Session* info = new (memory) Session(*this->m_allocator);
        info->m_id = session;
        this->m_sessions.Insert(ComSat::MapKey<uint64_t>(session), info);
    }
    return true;
}

// VERIFY: a8653a42 manual
// OFFSET: 0x00883C90
bool ComSat::Sound::Manager::RegisterLocalTalkerForSession(uint64_t session, uint8_t player) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    uint32_t index;
    if (!this->m_sessions.Find(ComSat::MapKey<uint64_t>(session), index)) {
        return false;
    }
    localPlayer->m_sessionPriorities[ComSat::MapKey<uint64_t>(session)] = 0x80;
    localPlayer->m_sessionVolumes[ComSat::MapKey<uint64_t>(session)] = 1.0f;
    localPlayer->m_mutedSessions.Remove(session);
    return true;
}

// VERIFY: 63bf831f manual
// OFFSET: 0x00883DB0
bool ComSat::Sound::Manager::RegisterRemoteTalkerForSession(uint64_t session, uint64_t talker) {
    Session* info;
    if (!this->m_sessions.Get(ComSat::MapKey<uint64_t>(session), info)) {
        return false;
    }
    uint32_t index;
    if (!this->m_remoteTalkers.Find(ComSat::MapKey<uint64_t>(talker), index)) {
        RemoteTalker* remote = new (this->m_allocator->Alloc(sizeof(RemoteTalker))) RemoteTalker(*this->m_allocator);
        remote->m_guid = talker;
        for (int32_t i = 0; i < s_jitterBufferSize; i++) {
            remote->m_packets[i].m_valid = false;
        }
        remote->m_frameTime.Init(this->m_frameDuration, s_frameTimeWindow);
        remote->m_lastPacketTime = 0;
        remote->m_lastSequence = 0;
        remote->m_playCursor = 0.0;
        remote->m_lastResetSequence = 0;
        this->m_remoteTalkers.Insert(ComSat::MapKey<uint64_t>(talker), remote);
    }
    if (!info->m_talkerPriorities.Find(ComSat::MapKey<uint64_t>(talker), index)) {
        info->m_talkerPriorities[ComSat::MapKey<uint64_t>(talker)] = 0x80;
    }
    return true;
}

// VERIFY: 0149312b auto
// OFFSET: 0x00883F90
bool ComSat::Sound::Manager::SetPlaybackPriorityForPlayerInSession(uint64_t session, uint64_t talker, uint8_t priority) {
    Session* info;
    if (!this->m_sessions.Get(ComSat::MapKey<uint64_t>(session), info)) {
        return false;
    }
    info->m_talkerPriorities[ComSat::MapKey<uint64_t>(talker)] = priority;
    return true;
}

// VERIFY: f96bfd70 auto
// OFFSET: 0x00884030
bool ComSat::Sound::Manager::SetPlaybackPriorityForSession(uint64_t session, uint8_t player, uint8_t priority) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    localPlayer->m_sessionPriorities[ComSat::MapKey<uint64_t>(session)] = priority;
    return true;
}

// VERIFY: 7684e1bc manual
// OFFSET: 0x008840D0
void ComSat::Sound::Manager::PlayVoiceDataForLocalPlayer(uint8_t player, int32_t frames) {
    bool reset = false;
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return;
    }
    int32_t channelCount = this->m_maxChannels;
    TalkingTalkerInfo infos[4];
    for (int32_t i = 0; i < 4; i++) {
        infos[i].m_talker = 0;
        infos[i].m_session = 0;
        infos[i].m_active = false;
    }
    for (int32_t c = 0; c < channelCount; c++) {
        TalkingTalkerInfo& channel = localPlayer->m_channels[c];
        if (channel.m_talker && --channel.m_holdFrames <= 0) {
            channel.m_talker = 0;
            channel.m_session = 0;
            channel.m_holdFrames = 0;
        }
    }
    this->AssignTalkersToChannelsForLocalPlayer(player, channelCount, infos);
    for (int32_t c = 0; c < channelCount; c++) {
        TalkingTalkerInfo& info = infos[c];
        TalkingTalkerInfo& channel = localPlayer->m_channels[c];
        if (info.m_talker && info.m_active && (channel.m_talker != info.m_talker || channel.m_session != info.m_session || !channel.m_active)) {
            uint32_t index;
            if (this->m_remoteTalkers.Find(ComSat::MapKey<uint64_t>(info.m_talker), index)) {
                this->m_callback->RemotePlayerTalkNotifyStart(player, info.m_talker, info.m_session);
                reset = true;
            }
        }
    }
    for (int32_t c = 0; c < channelCount; c++) {
        TalkingTalkerInfo& info = infos[c];
        TalkingTalkerInfo& channel = localPlayer->m_channels[c];
        if (channel.m_talker && channel.m_active && (info.m_talker != channel.m_talker || info.m_session != channel.m_session || !info.m_active)) {
            uint32_t index;
            if (this->m_remoteTalkers.Find(ComSat::MapKey<uint64_t>(channel.m_talker), index)) {
                this->m_callback->RemotePlayerTalkNotifyStop(player, channel.m_talker, channel.m_session);
            }
        }
    }
    for (int32_t c = 0; c < channelCount; c++) {
        TalkingTalkerInfo& channel = localPlayer->m_channels[c];
        channel.m_talker = infos[c].m_talker;
        channel.m_session = infos[c].m_session;
        channel.m_active = infos[c].m_active;
        if (infos[c].m_active) {
            channel.m_holdFrames = this->m_holdFrames;
        }
    }
    for (int32_t c = 0; c < channelCount; c++) {
        TalkingTalkerInfo& info = infos[c];
        if (!info.m_talker) {
            continue;
        }
        if (!info.m_active) {
            RemoteTalker* remote;
            if (this->m_remoteTalkers.Get(ComSat::MapKey<uint64_t>(info.m_talker), remote)) {
                double rate = ComSat::Sound::CODECIPMR6000::s_samplesPerFrame * 1000.0f / ComSat::Sound::CODECIPMR6000::s_sampleRate / remote->m_frameTime.GetCurrentAverage();
                this->GenerateAndWriteSilenceToHeadsetDeviceBuffer(player, c, 1.0f, rate);
            }
            continue;
        }
        double buffered = 0.0;
        if (this->m_io) {
            int32_t blockSize = this->m_io->GetPlaybackBlockSize(player);
            if (blockSize) {
                buffered = this->m_io->GetRemainingPlaybackBytes(player, c) / blockSize;
            }
        }
        if (info.m_active && info.m_talker) {
            uint32_t elapsed = this->m_currentTime - this->m_startTime;
            double missing = frames - elapsed * 0.001 * this->m_framesPerSecond - buffered;
            if (missing >= 1.0 && 0.0 == buffered) {
                this->GenerateAndWriteSilenceToHeadsetDeviceBuffer(player, c, static_cast<float>(missing), 1.0);
            }
        }
        RemoteTalker* remote;
        if (!this->m_remoteTalkers.Get(ComSat::MapKey<uint64_t>(info.m_talker), remote)) {
            continue;
        }
        float sessionVolume;
        if (!localPlayer->m_sessionVolumes.Get(ComSat::MapKey<uint64_t>(info.m_session), sessionVolume)) {
            sessionVolume = 1.0f;
        }
        float talkerVolume;
        if (!localPlayer->m_talkerVolumes.Get(ComSat::MapKey<uint64_t>(info.m_talker), talkerVolume)) {
            talkerVolume = 1.0f;
        }
        if (!this->m_io) {
            return;
        }
        this->m_io->SetPlayerPlaybackVolume(player, c, talkerVolume * sessionVolume);
        double average = remote->m_frameTime.GetCurrentAverage();
        int32_t slot = static_cast<int32_t>(remote->m_playCursor);
        double rate = ComSat::Sound::CODECIPMR6000::s_samplesPerFrame * 1000.0f / ComSat::Sound::CODECIPMR6000::s_sampleRate / average;
        int32_t end = static_cast<int32_t>(remote->m_playCursor + rate) % s_jitterBufferSize;
        for (; slot != end; slot = (slot + 1) % s_jitterBufferSize) {
            VoicePacket* packet = &remote->m_packets[slot];
            if (!packet->m_valid) {
                this->GenerateAndWriteSilenceToHeadsetDeviceBuffer(player, c, 1.0f, rate);
                continue;
            }
            uint32_t sequence = packet->m_sequence;
            if (sequence < remote->m_lastResetSequence) {
                sequence += 0x10000;
            }
            if (!this->m_codec) {
                return;
            }
            if ((sequence - remote->m_lastResetSequence >= static_cast<uint32_t>(ComSat::Sound::CODEC::MinimumSafeFramesBetweenResets()) && ComSat::Sound::CODEC::MinimumSafeFramesBetweenResets() && !(packet->m_sequence % ComSat::Sound::CODEC::MinimumSafeFramesBetweenResets())) || reset) {
                this->m_codec->ResetPlayerChannelDecoder(player, c);
                remote->m_lastResetSequence = packet->m_sequence;
                reset = false;
            }
            uint32_t size;
            if (!this->DecodeVoicePacket(player, c, packet, &size)) {
                continue;
            }
            LocalPlayer* target;
            if (this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), target)) {
                this->m_codec->ApplyVoiceEffects(player, target->m_buffer, &size);
            }
            this->WriteVoiceDataToHeadsetDeviceBuffer(player, c, size, rate);
        }
    }
}

// VERIFY: 38d75a44 auto
// OFFSET: 0x00884850
void ComSat::Sound::Manager::EncodeAvailableVoiceData() {
    uint32_t iterator = 0;
    ComSat::MapKey<uint8_t> key;
    LocalPlayer* localPlayer;
    while (this->m_localPlayers.Next(iterator, key, localPlayer)) {
        this->EncodeAvailableVoiceDataForLocalPlayer(key.m_value, localPlayer);
    }
}

// VERIFY: a25f3ffc manual
// OFFSET: [0x008848B0, 0x00884DA0]
ComSat::Sound::Manager::~Manager() {
    uint32_t iterator = 0;
    ComSat::MapKey<uint8_t> playerKey;
    LocalPlayer* localPlayer;
    for (; this->m_localPlayers.Next(iterator, playerKey, localPlayer); iterator = 0) {
        LocalPlayer* removed;
        this->m_localPlayers.Remove(playerKey, removed);
        ComSat::Delete(*this->m_allocator, localPlayer);
    }
    iterator = 0;
    ComSat::MapKey<uint64_t> key;
    Session* info;
    for (; this->m_sessions.Next(iterator, key, info); iterator = 0) {
        Session* removed;
        this->m_sessions.Remove(key, removed);
        ComSat::Delete(*this->m_allocator, info);
    }
    this->m_sessions.Clear();
    iterator = 0;
    RemoteTalker* remote;
    for (; this->m_remoteTalkers.Next(iterator, key, remote); iterator = 0) {
        RemoteTalker* removed;
        this->m_remoteTalkers.Remove(key, removed);
        ComSat::Delete(*this->m_allocator, remote);
    }
    if (this->m_io) {
        ComSat::Delete(*this->m_allocator, this->m_io);
    }
    if (this->m_codec) {
        ComSat::Delete(*this->m_allocator, this->m_codec);
    }
}

// VERIFY: 8880502e manual
// OFFSET: 0x00884AB0
bool ComSat::Sound::Manager::UnregisterRemoteTalkerFromSession(uint64_t session, uint64_t talker) {
    Session* info;
    if (!this->m_sessions.Get(ComSat::MapKey<uint64_t>(session), info)) {
        return false;
    }
    RemoteTalker* remote;
    if (!this->m_remoteTalkers.Get(ComSat::MapKey<uint64_t>(talker), remote)) {
        return false;
    }
    uint8_t priority;
    info->m_talkerPriorities.Remove(ComSat::MapKey<uint64_t>(talker), priority);
    uint32_t iterator = 0;
    ComSat::MapKey<uint64_t> key(0);
    Session* other;
    while (this->m_sessions.Next(iterator, key, other)) {
        if (key.m_value != session) {
            uint32_t index;
            if (other->m_talkerPriorities.Find(ComSat::MapKey<uint64_t>(talker), index)) {
                return true;
            }
        }
    }
    RemoteTalker* removed;
    this->m_remoteTalkers.Remove(ComSat::MapKey<uint64_t>(talker), removed);
    ComSat::Delete(*this->m_allocator, remote);
    return true;
}

// VERIFY: 3e78c7de manual
// OFFSET: 0x00884CA0
void ComSat::Sound::Manager::PlayScheduledVoiceData() {
    uint32_t elapsed = this->m_currentTime - this->m_startTime;
    double frames = elapsed * 0.001 * this->m_framesPerSecond;
    int32_t frame = static_cast<int32_t>(frames + 1.0);
    int32_t count = static_cast<int32_t>(this->m_jitterDelay - this->m_currentFrame + frames);
    if (count > this->m_jitterDelay) {
        count = this->m_jitterDelay;
    }
    frame += this->m_jitterDelay - count;
    for (; count > 0; count--) {
        uint32_t iterator = 0;
        ComSat::MapKey<uint8_t> playerKey;
        LocalPlayer* localPlayer;
        while (this->m_localPlayers.Next(iterator, playerKey, localPlayer)) {
            this->PlayVoiceDataForLocalPlayer(playerKey.m_value, frame);
        }
        iterator = 0;
        ComSat::MapKey<uint64_t> key;
        RemoteTalker* remote;
        while (this->m_remoteTalkers.Next(iterator, key, remote)) {
            this->AdvancePlayCursorForRemoteTalker(remote);
        }
        this->m_currentFrame = frame++;
    }
}

// VERIFY: ef10a638 auto
// OFFSET: 0x00884DB0
void ComSat::Sound::Manager::Work(uint32_t time) {
    this->m_currentTime = time;
    if (this->m_io) {
        this->m_io->Work(time);
    }
    this->PlayScheduledVoiceData();
    this->EncodeAvailableVoiceData();
}

// VERIFY: 28a13445 manual
// OFFSET: 0x00884DE0
bool ComSat::Sound::Manager::SessionDestroy(uint64_t session) {
    Session* info;
    if (this->m_sessions.Get(ComSat::MapKey<uint64_t>(session), info)) {
        uint32_t iterator = 0;
        ComSat::MapKey<uint64_t> key(0);
        uint8_t priority;
        while (info->m_talkerPriorities.Next(iterator, key, priority)) {
            this->UnregisterRemoteTalkerFromSession(session, key.m_value);
        }
        if (this->m_sessions.Remove(ComSat::MapKey<uint64_t>(session), info)) {
            ComSat::Delete(*this->m_allocator, info);
        }
    }
    return true;
}

// VERIFY: a1c02a9e manual
// OFFSET: 0x00884F10
bool ComSat::Sound::Manager::UnregisterLocalTalkerFromSession(uint64_t session, uint8_t player) {
    LocalPlayer* localPlayer;
    if (!this->m_localPlayers.Get(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        return false;
    }
    uint32_t index;
    if (!this->m_sessions.Find(ComSat::MapKey<uint64_t>(session), index)) {
        return false;
    }
    localPlayer->m_mutedSessions.Remove(session);
    uint8_t priority;
    localPlayer->m_sessionPriorities.Remove(ComSat::MapKey<uint64_t>(session), priority);
    float volume;
    localPlayer->m_sessionVolumes.Remove(ComSat::MapKey<uint64_t>(session), volume);
    LocalPlayer* other = nullptr;
    ComSat::MapKey<uint8_t> key(0);
    uint32_t iterator = 0;
    bool last = true;
    while (this->m_localPlayers.Next(iterator, key, other)) {
        if (player != key.m_value && other->m_sessionPriorities.Find(ComSat::MapKey<uint64_t>(session), index)) {
            last = false;
        }
    }
    if (last) {
        this->SessionDestroy(session);
    }
    return true;
}

// VERIFY: 717e19dc auto
// OFFSET: 0x008850F0
bool ComSat::Sound::Manager::DestroyLocalPlayer(uint8_t player) {
    uint32_t iterator = 0;
    ComSat::MapKey<uint64_t> key;
    Session* info;
    while (this->m_sessions.Next(iterator, key, info)) {
        this->UnregisterLocalTalkerFromSession(key.m_value, player);
    }
    LocalPlayer* localPlayer;
    if (this->m_localPlayers.Remove(ComSat::MapKey<uint8_t>(player), localPlayer)) {
        ComSat::Delete(*this->m_allocator, localPlayer);
        if (this->m_io) {
            this->m_io->DestroyLocalPlayer(player);
        }
        if (this->m_codec) {
            this->m_codec->RemoveLocalPlayer(player);
        }
    }
    return true;
}
