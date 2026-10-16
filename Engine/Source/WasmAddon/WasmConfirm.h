#pragma once
#include "Engine/Source/WasmAddon/WasmTypes.h"
#include <deque>
#include <map>
#include <vector>

namespace WasmAddon {

std::string FormatMoney(uint32_t copper);
const char* ChatTypeLabel(int32_t chatType);
bool IsAllowedChatType(int32_t chatType);
std::vector<std::string> ComposeConfirmText(const ConfirmRequest& request, const std::string& addonDisplayName);

// ʕ •ᴥ•ʔ✿ Pure pending-request state machine; the clock is injected so tests control time ✿ ʕ •ᴥ•ʔ
class ConfirmQueue {
public:
    enum class Submit { Queued, LimitExceeded, AutoDenied };

    struct Completed {
        uint32_t id = 0;
        size_t addonIndex = 0;
        ConfirmStatus status = ConfirmStatus::Expired;
        ConfirmRequest request;
    };

    Submit Add(ConfirmRequest& request, uint64_t nowMs);
    const ConfirmRequest* Front() const;
    bool ResolveFront(bool accept, uint64_t nowMs, Completed& out);
    std::vector<Completed> Tick(uint64_t nowMs);
    bool CancelAddon(size_t addonIndex, uint64_t nowMs, uint32_t& cancelledId);
    void CancelAuctions(uint64_t nowMs);
    void ResetAddon(size_t addonIndex);
    void Rearm(uint64_t nowMs);
    size_t Size() const { return m_queue.size(); }

private:
    struct AddonState {
        uint64_t cooldownUntilMs = 0;
        uint32_t denyCount = 0;
        bool autoDeny = false;
    };
    void NoteDenied(size_t addonIndex, uint64_t nowMs);
    void Promote(uint64_t nowMs);

    std::deque<ConfirmRequest> m_queue;
    std::map<size_t, AddonState> m_addons;
    uint32_t m_nextId = 1;
};

} // namespace WasmAddon
