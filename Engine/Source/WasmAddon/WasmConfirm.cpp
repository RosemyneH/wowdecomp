#include "Engine/Source/WasmAddon/WasmConfirm.h"

namespace WasmAddon {

std::string FormatMoney(uint32_t copper) {
    uint32_t gold = copper / 10000, silver = (copper / 100) % 100, rest = copper % 100;
    std::string out;
    if (gold) out += std::to_string(gold) + "g";
    if (silver) out += (out.empty() ? "" : " ") + std::to_string(silver) + "s";
    if (rest || out.empty()) out += (out.empty() ? "" : " ") + std::to_string(rest) + "c";
    return out;
}

const char* ChatTypeLabel(int32_t chatType) {
    switch (chatType) {
        case 1: return "SAY";
        case 2: return "PARTY";
        case 3: return "RAID";
        case 4: return "GUILD";
        case 5: return "OFFICER";
        case 6: return "YELL";
        case 7: return "WHISPER";
        case 10: return "EMOTE";
        case 17: return "CHANNEL";
        case 40: return "RAID_WARNING";
        case 44: return "BATTLEGROUND";
        default: return nullptr;
    }
}

bool IsAllowedChatType(int32_t chatType) { return ChatTypeLabel(chatType) != nullptr; }

std::vector<std::string> ComposeConfirmText(const ConfirmRequest& request, const std::string& addonDisplayName) {
    std::vector<std::string> lines;
    lines.push_back(addonDisplayName + " requests");
    if (request.action == ConfirmAction::ChatSend) {
        std::string line = std::string("Send ") + (ChatTypeLabel(request.chatType) ? ChatTypeLabel(request.chatType) : "chat") + ": \"" + request.message + "\"";
        if (!request.target.empty()) line += " to " + request.target;
        lines.push_back(line);
    } else {
        bool buyout = request.identity.buyout > 0 && request.bid >= request.identity.buyout;
        std::string line = std::string(buyout ? "Buyout " : "Bid ") + FormatMoney(buyout ? request.identity.buyout : request.bid) +
            " on item " + std::to_string(request.identity.itemId);
        if (request.identity.count > 1) line += " x" + std::to_string(request.identity.count);
        lines.push_back(line);
    }
    lines.push_back("Allow / Deny");
    return lines;
}

ConfirmQueue::Submit ConfirmQueue::Add(ConfirmRequest& request, uint64_t nowMs) {
    AddonState& state = m_addons[request.addonIndex];
    if (state.autoDeny) return Submit::AutoDenied;
    if (nowMs < state.cooldownUntilMs) return Submit::LimitExceeded;
    for (auto& pending : m_queue)
        if (pending.addonIndex == request.addonIndex) return Submit::LimitExceeded;
    if (m_queue.size() >= 4) return Submit::LimitExceeded;
    request.id = m_nextId++;
    request.createdMs = nowMs;
    bool wasEmpty = m_queue.empty();
    m_queue.push_back(std::move(request));
    if (wasEmpty) Promote(nowMs);
    return Submit::Queued;
}

void ConfirmQueue::Promote(uint64_t nowMs) {
    if (m_queue.empty() || m_queue.front().shownMs) return;
    m_queue.front().shownMs = nowMs;
}

const ConfirmRequest* ConfirmQueue::Front() const {
    return m_queue.empty() ? nullptr : &m_queue.front();
}

void ConfirmQueue::NoteDenied(size_t addonIndex, uint64_t nowMs) {
    AddonState& state = m_addons[addonIndex];
    state.cooldownUntilMs = nowMs + 10000;
    if (++state.denyCount >= 3) state.autoDeny = true;
}

bool ConfirmQueue::ResolveFront(bool accept, uint64_t nowMs, Completed& out) {
    if (m_queue.empty()) return false;
    const ConfirmRequest& front = m_queue.front();
    // ʕ ●ᴥ●ʔ✿ Arming delay: a click already in flight when the dialog appears can never confirm ✿ ʕ ●ᴥ●ʔ
    if (accept && nowMs < front.shownMs + 750) return false;
    out = {front.id, front.addonIndex, accept ? ConfirmStatus::Accepted : ConfirmStatus::Denied, front};
    size_t addonIndex = front.addonIndex;
    m_queue.pop_front();
    if (!accept) NoteDenied(addonIndex, nowMs);
    Promote(nowMs);
    return true;
}

std::vector<ConfirmQueue::Completed> ConfirmQueue::Tick(uint64_t nowMs) {
    std::vector<Completed> done;
    bool changed = true;
    while (changed && !m_queue.empty()) {
        changed = false;
        const ConfirmRequest& front = m_queue.front();
        bool expired = front.shownMs ? nowMs >= front.shownMs + 30000 : nowMs >= front.createdMs + 60000;
        if (expired) {
            done.push_back({front.id, front.addonIndex, ConfirmStatus::Expired, front});
            NoteDenied(front.addonIndex, nowMs);
            m_queue.pop_front();
            Promote(nowMs);
            changed = true;
            continue;
        }
        for (size_t i = 1; i < m_queue.size(); i++) {
            if (nowMs >= m_queue[i].createdMs + 60000) {
                done.push_back({m_queue[i].id, m_queue[i].addonIndex, ConfirmStatus::Expired, m_queue[i]});
                NoteDenied(m_queue[i].addonIndex, nowMs);
                m_queue.erase(m_queue.begin() + (ptrdiff_t)i);
                changed = true;
                break;
            }
        }
    }
    return done;
}

bool ConfirmQueue::CancelAddon(size_t addonIndex, uint64_t nowMs, uint32_t& cancelledId) {
    for (size_t i = 0; i < m_queue.size(); i++) {
        if (m_queue[i].addonIndex != addonIndex) continue;
        cancelledId = m_queue[i].id;
        bool wasFront = i == 0;
        m_queue.erase(m_queue.begin() + (ptrdiff_t)i);
        if (wasFront) Promote(nowMs);
        return true;
    }
    return false;
}

void ConfirmQueue::CancelAuctions(uint64_t nowMs) {
    bool frontRemoved = false;
    for (size_t i = 0; i < m_queue.size();) {
        if (m_queue[i].action == ConfirmAction::AuctionBid) {
            frontRemoved = frontRemoved || i == 0;
            m_queue.erase(m_queue.begin() + (ptrdiff_t)i);
        } else {
            i++;
        }
    }
    if (frontRemoved) Promote(nowMs);
}

void ConfirmQueue::ResetAddon(size_t addonIndex) { m_addons.erase(addonIndex); }

void ConfirmQueue::Rearm(uint64_t nowMs) {
    if (!m_queue.empty() && m_queue.front().shownMs) m_queue.front().shownMs = nowMs;
}

} // namespace WasmAddon
