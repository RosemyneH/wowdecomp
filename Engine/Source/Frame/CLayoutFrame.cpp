#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "Engine/Source/Frame/CLayoutFrame.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Frame/CFramePoint.h"
#include "Engine/Source/FrameXML/LoadXML.h"
#include "Engine/Source/FrameXML/XMLTree.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/CMath.h"

// STATIC: 0x009EA83C
static const FRAMEPOINT s_centerXPoints[3] = { FRAMEPOINT_TOP, FRAMEPOINT_CENTER, FRAMEPOINT_BOTTOM };

// STATIC: 0x009EA848
static const FRAMEPOINT s_centerYPoints[3] = { FRAMEPOINT_LEFT, FRAMEPOINT_CENTER, FRAMEPOINT_RIGHT };

// STATIC: 0x009EA854
static const FRAMEPOINT s_rightPoints[3] = { FRAMEPOINT_TOPRIGHT, FRAMEPOINT_RIGHT, FRAMEPOINT_BOTTOMRIGHT };

// STATIC: 0x009EA860
static const FRAMEPOINT s_bottomPoints[3] = { FRAMEPOINT_BOTTOMLEFT, FRAMEPOINT_BOTTOM, FRAMEPOINT_BOTTOMRIGHT };

// STATIC: 0x009EA86C
static const FRAMEPOINT s_leftPoints[3] = { FRAMEPOINT_TOPLEFT, FRAMEPOINT_LEFT, FRAMEPOINT_BOTTOMLEFT };

// STATIC: 0x009EA878
static const FRAMEPOINT s_topPoints[3] = { FRAMEPOINT_TOPLEFT, FRAMEPOINT_TOP, FRAMEPOINT_TOPRIGHT };

// STATIC: 0x00AC1018
static TSExplicitList<CLayoutFrame, offsetof(CLayoutFrame, m_resizeLink)> s_resizePendingList;

// VERIFY: 7e82d75f auto
// OFFSET: 0x00488BE0
static float ComputeRightBoundary(float center, float opposite, float size) {
    if (opposite != CFramePoint::UNDEFINED && size != 0.0f) {
        return size + opposite;
    }

    if (center != CFramePoint::UNDEFINED) {
        if (size != 0.0f) {
            return center + size * 0.5f;
        }

        if (opposite != CFramePoint::UNDEFINED) {
            return center + center - opposite;
        }
    }

    return CFramePoint::UNDEFINED;
}

// VERIFY: dcfdf30a auto
// OFFSET: 0x00488C60
static float ComputeCenterPosition(float first, float second, float size) {
    if (first != CFramePoint::UNDEFINED) {
        if (second != CFramePoint::UNDEFINED) {
            return (second + first) * 0.5f;
        }

        if (size != 0.0f) {
            return first + size * 0.5f;
        }
    }

    if (second != CFramePoint::UNDEFINED && size != 0.0f) {
        return second - size * 0.5f;
    }

    return CFramePoint::UNDEFINED;
}

// VERIFY: 9054e706 auto
// OFFSET: 0x00512410
void GetRectFramePoints(const CRect& rect, C2Vector* points) {
    points[FRAMEPOINT_TOPLEFT].x = rect.minX;
    points[FRAMEPOINT_TOPLEFT].y = rect.maxY;
    points[FRAMEPOINT_TOP].x = (rect.maxX - rect.minX) * 0.5f + rect.minX;
    points[FRAMEPOINT_TOP].y = rect.maxY;
    points[FRAMEPOINT_TOPRIGHT].x = rect.maxX;
    points[FRAMEPOINT_TOPRIGHT].y = rect.maxY;
    points[FRAMEPOINT_LEFT].x = rect.minX;
    points[FRAMEPOINT_LEFT].y = (rect.maxY - rect.minY) * 0.5f + rect.minY;
    points[FRAMEPOINT_CENTER].x = (rect.maxX - rect.minX) * 0.5f + rect.minX;
    points[FRAMEPOINT_CENTER].y = (rect.maxY - rect.minY) * 0.5f + rect.minY;
    points[FRAMEPOINT_RIGHT].x = rect.maxX;
    points[FRAMEPOINT_RIGHT].y = (rect.maxY - rect.minY) * 0.5f + rect.minY;
    points[FRAMEPOINT_BOTTOMLEFT].x = rect.minX;
    points[FRAMEPOINT_BOTTOMLEFT].y = rect.minY;
    points[FRAMEPOINT_BOTTOM].x = 0.5f * (rect.maxX - rect.minX) + rect.minX;
    points[FRAMEPOINT_BOTTOM].y = rect.minY;
    points[FRAMEPOINT_BOTTOMRIGHT].x = rect.maxX;
    points[FRAMEPOINT_BOTTOMRIGHT].y = rect.minY;
}

// VERIFY: 25624977 auto
// OFFSET: 0x00482AC0
int32_t CLayoutFrame::HasAnyFramepoints() {
    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        if (this->m_framePoints[i] && !(this->m_framePoints[i]->m_flags & 8)) {
            return 1;
        }
    }

    return 0;
}

// VERIFY: 831ecd27 auto
// OFFSET: 0x00488CF0
uint32_t CLayoutFrame::IsResizePending() {
    return (this->m_layoutFlags >> 8) & 4;
}

// VERIFY: b2f05c63 auto
// OFFSET: 0x00488D00
float CLayoutFrame::GetHeight() {
    return this->m_height;
}

// VERIFY: 1de866bd auto
// OFFSET: 0x00488D10
float CLayoutFrame::GetWidth() {
    return this->m_width;
}

// VERIFY: 1c8a97fb auto
// OFFSET: 0x00488D20
void CLayoutFrame::GetClampRectInsets(float& left, float& right, float& top, float& bottom) {
    bottom = 0.0f;
    top = 0.0f;
    right = 0.0f;
    left = 0.0f;
}

// VERIFY: 72c4914d manual
// OFFSET: 0x00488D90
CFramePoint* CLayoutFrame::GetFramePoint(FRAMEPOINT point) {
    CFramePoint* framePoint = this->m_framePoints[point];

    if (!framePoint || (framePoint->m_flags & 8)) {
        return nullptr;
    }

    return framePoint;
}

// VERIFY: 06c5e7a8 auto
// OFFSET: 0x00488DB0
int32_t CLayoutFrame::GetFramePointX(const FRAMEPOINT* points, int32_t count, float& x) {
    for (int32_t i = 0; i < count; i++) {
        CFramePoint* framePoint = this->m_framePoints[points[i]];

        if (framePoint && !(framePoint->m_flags & 8)) {
            float value = framePoint->X(this->m_layoutScale);
            x = value;

            if (framePoint->m_flags & 4) {
                x = CFramePoint::UNDEFINED;
                return 0;
            }

            if (CFramePoint::UNDEFINED != value) {
                return 1;
            }
        }
    }

    x = CFramePoint::UNDEFINED;
    return 1;
}

// VERIFY: 95667f26 auto
// OFFSET: 0x00488E40
int32_t CLayoutFrame::GetFramePointY(const FRAMEPOINT* points, int32_t count, float& y) {
    for (int32_t i = 0; i < count; i++) {
        CFramePoint* framePoint = this->m_framePoints[points[i]];

        if (framePoint && !(framePoint->m_flags & 8)) {
            float value = framePoint->Y(this->m_layoutScale);
            y = value;

            if (framePoint->m_flags & 4) {
                y = CFramePoint::UNDEFINED;
                return 0;
            }

            if (CFramePoint::UNDEFINED != value) {
                return 1;
            }
        }
    }

    y = CFramePoint::UNDEFINED;
    return 1;
}

// VERIFY: 5e4d38f8 manual
// OFFSET: 0x00488ED0
void CLayoutFrame::GetHorizontalFramePoint(const FRAMEPOINT* points, int32_t count, float& x) {
    for (int32_t i = 0; i < 10; i++) {
        if (this->GetFramePointX(points, count, x)) {
            break;
        }
    }
}

// VERIFY: acf15f93 manual
// OFFSET: 0x00488F10
void CLayoutFrame::GetVerticalFramePoint(const FRAMEPOINT* points, int32_t count, float& y) {
    for (int32_t i = 0; i < 10; i++) {
        if (this->GetFramePointY(points, count, y)) {
            break;
        }
    }
}

// VERIFY: c011784f auto
// OFFSET: 0x00488F50
float CLayoutFrame::CenterX() {
    if (this->m_guard & GUARD_CENTER_X) {
        return CFramePoint::UNDEFINED;
    }

    this->m_guard |= GUARD_CENTER_X;

    float x = CFramePoint::UNDEFINED;
    this->GetHorizontalFramePoint(s_centerXPoints, 3, x);

    if (x == CFramePoint::UNDEFINED) {
        float size = this->GetWidth() * this->m_layoutScale;
        x = ComputeCenterPosition(this->Left(), this->Right(), size);
    }

    this->m_guard &= ~GUARD_CENTER_X;

    return x;
}

// VERIFY: f498aaa3 auto
// OFFSET: 0x00488FE0
float CLayoutFrame::CenterY() {
    if (this->m_guard & GUARD_CENTER_Y) {
        return CFramePoint::UNDEFINED;
    }

    this->m_guard |= GUARD_CENTER_Y;

    float y = CFramePoint::UNDEFINED;
    this->GetVerticalFramePoint(s_centerYPoints, 3, y);

    if (y == CFramePoint::UNDEFINED) {
        float size = this->GetHeight() * this->m_layoutScale;
        y = ComputeCenterPosition(this->Bottom(), this->Top(), size);
    }

    this->m_guard &= ~GUARD_CENTER_Y;

    return y;
}

// VERIFY: 894aac46 auto
// OFFSET: 0x00489070
float CLayoutFrame::Right() {
    if (this->m_guard & GUARD_RIGHT) {
        return CFramePoint::UNDEFINED;
    }

    this->m_guard |= GUARD_RIGHT;

    float x = CFramePoint::UNDEFINED;
    this->GetHorizontalFramePoint(s_rightPoints, 3, x);

    if (x == CFramePoint::UNDEFINED) {
        float size = this->GetWidth() * this->m_layoutScale;
        x = ComputeRightBoundary(this->CenterX(), this->Left(), size);
    }

    this->m_guard &= ~GUARD_RIGHT;

    return x;
}

// VERIFY: ff84563d auto
// OFFSET: 0x00489100
float CLayoutFrame::Bottom() {
    if (this->m_guard & GUARD_BOTTOM) {
        return CFramePoint::UNDEFINED;
    }

    this->m_guard |= GUARD_BOTTOM;

    float y = CFramePoint::UNDEFINED;
    this->GetVerticalFramePoint(s_bottomPoints, 3, y);

    if (y == CFramePoint::UNDEFINED) {
        float size = -(this->GetHeight() * this->m_layoutScale);
        y = ComputeRightBoundary(this->CenterY(), this->Top(), size);
    }

    this->m_guard &= ~GUARD_BOTTOM;

    return y;
}

// VERIFY: 0129a128 auto
// OFFSET: 0x00489190
void CLayoutFrame::ProtectFramepoints(uint32_t flag) {
    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        CFramePoint* framePoint = this->m_framePoints[i];

        if (framePoint && !(framePoint->m_flags & 8) && framePoint->m_relative) {
            framePoint->m_relative->SetProtectFlag(flag);
        }
    }
}

// VERIFY: 88709e27 auto
// OFFSET: 0x004891D0
int32_t CLayoutFrame::IsResizeDependency(CLayoutFrame* frame) {
    CLayoutFrame* checked = nullptr;

    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        CFramePoint* framePoint = this->m_framePoints[i];

        if (!framePoint || (framePoint->m_flags & 8)) {
            continue;
        }

        CLayoutFrame* relative = framePoint->m_relative;

        if (!relative || relative == checked) {
            continue;
        }

        if (relative == frame || relative->IsResizeDependency(frame)) {
            return 1;
        }

        checked = relative;
    }

    return 0;
}

// VERIFY: 98ff2ba2 auto
// OFFSET: 0x00489230
int32_t CLayoutFrame::GetRect(CRect* rect) {
    if (!(this->m_layoutFlags & 0x100)) {
        return 0;
    }

    *rect = this->m_rect;

    return 1;
}

// VERIFY: 9bc456e9 auto
// OFFSET: 0x00489270
int32_t CLayoutFrame::PtInFrameRect(const C2Vector& point) {
    return (this->m_layoutFlags & 0x100) && this->m_rect.IsPointInside(point);
}

// VERIFY: 879cc61f auto
// OFFSET: 0x004892A0
float CLayoutFrame::Left() {
    if (this->m_guard & GUARD_LEFT) {
        return CFramePoint::UNDEFINED;
    }

    this->m_guard |= GUARD_LEFT;

    float x = CFramePoint::UNDEFINED;
    this->GetHorizontalFramePoint(s_leftPoints, 3, x);

    if (x == CFramePoint::UNDEFINED) {
        float size = -(this->GetWidth() * this->m_layoutScale);
        x = ComputeRightBoundary(this->CenterX(), this->Right(), size);
    }

    this->m_guard &= ~GUARD_LEFT;

    return x;
}

// VERIFY: 14eff9b6 auto
// OFFSET: 0x00489330
float CLayoutFrame::Top() {
    if (this->m_guard & GUARD_TOP) {
        return CFramePoint::UNDEFINED;
    }

    this->m_guard |= GUARD_TOP;

    float y = CFramePoint::UNDEFINED;
    this->GetVerticalFramePoint(s_topPoints, 3, y);

    if (y == CFramePoint::UNDEFINED) {
        float size = this->GetHeight() * this->m_layoutScale;
        y = ComputeRightBoundary(this->CenterY(), this->Bottom(), size);
    }

    this->m_guard &= ~GUARD_TOP;

    return y;
}

// VERIFY: c42a053f auto
// OFFSET: 0x004893C0
int32_t CLayoutFrame::CalculateRect(CRect* rect) {
    rect->minX = this->Left();

    if (CFramePoint::UNDEFINED == rect->minX) {
        return 0;
    }

    rect->minY = this->Bottom();

    if (CFramePoint::UNDEFINED == rect->minY) {
        return 0;
    }

    rect->maxX = this->Right();

    if (CFramePoint::UNDEFINED == rect->maxX) {
        return 0;
    }

    rect->maxY = this->Top();

    if (CFramePoint::UNDEFINED == rect->maxY) {
        return 0;
    }

    if (!(this->m_layoutFlags & 0x1000)) {
        return 1;
    }

    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    this->GetClampRectInsets(left, right, top, bottom);

    float minX = -(this->m_layoutScale * left);
    float maxX = NDCToDDCWidth(1.0f) - this->m_layoutScale * right;
    float minY = -(this->m_layoutScale * bottom);
    float maxY = NDCToDDCHeight(1.0f) - this->m_layoutScale * top;

    if (minX > rect->minX) {
        rect->maxX = rect->maxX - (rect->minX - minX);
        rect->minX = minX;
    }

    if (minY > rect->minY) {
        rect->maxY = rect->maxY - (rect->minY - minY);
        rect->minY = minY;
    }

    if (maxX < rect->maxX) {
        rect->minX = rect->minX - (rect->maxX - maxX);
        rect->maxX = maxX;
    }

    if (maxY < rect->maxY) {
        rect->minY = rect->minY - (rect->maxY - maxY);
        rect->maxY = maxY;
    }

    return 1;
}

// VERIFY: cabf5ddd auto
// OFFSET: 0x00489570
int32_t CLayoutFrame::OnFrameResize() {
    CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (!this->CalculateRect(&rect)) {
        this->m_layoutFlags = (this->m_layoutFlags & ~0x900u) | 0x800;
        return 0;
    }

    this->m_layoutFlags &= ~0xC00u;

    if ((this->m_layoutFlags & 0x100) && fabs(rect.minX - this->m_rect.minX) < 0.0000099999997f && fabs(rect.maxX - this->m_rect.maxX) < 0.0000099999997f && fequalz(rect.minY, this->m_rect.minY, 0.0000099999997f) && fequalz(rect.maxY, this->m_rect.maxY, 0.0000099999997f)) {
        return 1;
    }

    CRect oldRect = this->m_rect;
    this->m_rect = rect;
    this->m_layoutFlags |= 0x100;
    this->OnFrameSizeChanged(oldRect);

    return (this->m_layoutFlags >> 8) & 1;
}

// VERIFY: ebe958b5 auto
// OFFSET: 0x00489690
void CLayoutFrame::SetProtected() {
    this->m_layoutFlags = (this->m_layoutFlags & ~0x70000u) | 0x10000;
    this->PropagateProtectFlagToParent(0x200);
    this->ProtectFramepoints(0x200);
}

// VERIFY: 194662d5 manual
// OFFSET: 0x004896C0
void CLayoutFrame::SetProtectFlag(uint32_t flag) {
    if (this->m_layoutFlags & 0x90000) {
        return;
    }

    this->m_layoutFlags = (this->m_layoutFlags & ~0x20000u) | (((flag & 0xF7FFu) | 0x800) << 8);
    this->PropagateProtectFlagToParent(flag);
    this->ProtectFramepoints(flag);
    this->m_layoutFlags &= ~0x80000u;
}

// VERIFY: 1461f17f manual
// OFFSET: 0x00489710
void CLayoutFrame::AddToResizeList() {
    if (s_resizePendingList.IsLinked(this)) {
        return;
    }

    CLayoutFrame* dependent = nullptr;

    for (CLayoutFrame* frame = s_resizePendingList.Head(); frame; frame = s_resizePendingList.Next(frame)) {
        for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
            CFramePoint* framePoint = frame->m_framePoints[i];

            if (framePoint && !(framePoint->m_flags & 8) && framePoint->m_relative == this) {
                dependent = frame;
            }
        }

        if (dependent) {
            s_resizePendingList.LinkNode(this, 2, dependent);
            this->m_layoutFlags = (this->m_layoutFlags & ~0xFFu) | 6;
            return;
        }
    }

    s_resizePendingList.LinkToTail(this);
    this->m_layoutFlags = (this->m_layoutFlags & ~0xFFu) | 6;
}

// VERIFY: dfeb387c manual
// OFFSET: 0x004898B0
void CLayoutFrame::ResizePending() {
    for (uint32_t loops = 0; s_resizePendingList.Head() && loops < 32; loops++) {
        CLayoutFrame* frame = s_resizePendingList.Head();

        while (frame) {
            if (!frame->IsObjectLoaded()) {
                frame = s_resizePendingList.Next(frame);
                continue;
            }

            int32_t resized = frame->OnFrameResize();
            CLayoutFrame* next = s_resizePendingList.Next(frame);

            if (resized || (--reinterpret_cast<uint8_t&>(frame->m_layoutFlags), !static_cast<uint8_t>(frame->m_layoutFlags))) {
                frame->m_layoutFlags &= ~0x400u;
                s_resizePendingList.UnlinkNode(frame);
            }

            frame = next;
        }
    }
}

// VERIFY: d89d6f35 auto
// OFFSET: 0x00489980
CLayoutFrame::CLayoutFrame() {
    this->m_layoutFlags &= 0xFF0000FF;
    this->m_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
    this->m_width = 0.0f;
    this->m_height = 0.0f;
    this->m_layoutScale = 1.0f;
    this->m_layoutDepth = 1.0f;
    this->m_resizeRect = { 0.0f, 0.0f, 0.0f, 0.0f };

    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        this->m_framePoints[i] = nullptr;
    }

    this->m_guard = GUARD_NONE;
}

// VERIFY: 1a39cb37 auto
// OFFSET: 0x004899F0
int32_t CLayoutFrame::AreChildrenProtected(int32_t& deferred) {
    return 0;
}

// VERIFY: 57f5064c auto
// OFFSET: 0x00489A00
void CLayoutFrame::OnProtectedAttach(CLayoutFrame* frame) {
    if (this->m_layoutFlags & 0x40000) {
        frame->SetProtectFlag(0x400);
    }

    if (this->m_layoutFlags & 0x30000) {
        frame->SetProtectFlag(0x200);
    }
}

// VERIFY: f675f0e3 auto
// OFFSET: 0x00489A40
void CLayoutFrame::OnProtectedDetach(CLayoutFrame* frame) {
    if (this->m_layoutFlags & 0x70000) {
        frame->SetProtectFlag(0x400);
    }
}

// VERIFY: 7f53a330 manual
// OFFSET: 0x00489B50
int32_t CLayoutFrame::AreDependentsProtected(int32_t& deferred) {
    for (CLayoutFrame::FRAMENODE* node = this->m_resizeList.Head(); node; node = this->m_resizeList.Next(node)) {
        if (node->m_frame->m_layoutFlags & 0x80000) {
            deferred = 1;
        } else if (node->m_frame->IsProtected()) {
            return 1;
        }
    }

    return 0;
}

// VERIFY: 6c195430 manual
// OFFSET: 0x00489BB0
uint32_t CLayoutFrame::IsProtected() {
    if (this->m_layoutFlags & 0x40000) {
        this->m_layoutFlags |= 0x80000;

        int32_t deferred = 0;

        if (this->AreChildrenProtected(deferred) || this->AreDependentsProtected(deferred)) {
            this->m_layoutFlags |= 0x20000;
        }

        this->m_layoutFlags &= ~0x80000u;

        if (!deferred || (this->m_layoutFlags & 0x20000)) {
            this->m_layoutFlags &= ~0x40000u;
        }
    }

    return (this->m_layoutFlags >> 8) & 0x300;
}

// VERIFY: c9d23c46 manual
// OFFSET: 0x00489C30
void CLayoutFrame::RegisterResize(CLayoutFrame* frame, uint32_t dependency) {
    for (CLayoutFrame::FRAMENODE* node = this->m_resizeList.Head(); node; node = this->m_resizeList.Next(node)) {
        if (node->m_frame == frame) {
            node->m_dependency |= dependency;
            return;
        }
    }

    CLayoutFrame::FRAMENODE* node = this->m_resizeList.NewNode(2, 0, 0);
    node->m_frame = frame;
    node->m_dependency = dependency;

    if (frame->m_layoutFlags & 0x40000) {
        this->SetProtectFlag(0x400);
    }

    if (frame->m_layoutFlags & 0x30000) {
        this->SetProtectFlag(0x200);
    }
}

// VERIFY: a0d5b2b8 manual
// OFFSET: 0x00489D70
void CLayoutFrame::UnregisterResize(CLayoutFrame* frame, uint32_t dependency) {
    CLayoutFrame::FRAMENODE* node = this->m_resizeList.Head();

    while (node) {
        if (node->m_frame != frame) {
            node = this->m_resizeList.Next(node);
            continue;
        }

        node->m_dependency &= ~dependency;

        if (node->m_dependency) {
            return;
        }

        if (frame->m_layoutFlags & 0x70000) {
            this->SetProtectFlag(0x400);
        }

        this->m_resizeList.DeleteNode(node);
        node = nullptr;
    }
}

// VERIFY: a6f4615d manual
// OFFSET: 0x00489DE0
void CLayoutFrame::Resize(int32_t force) {
    if (force && !(this->m_layoutFlags & 0x800) && this->OnFrameResize()) {
        if (s_resizePendingList.IsLinked(this)) {
            s_resizePendingList.UnlinkNode(this);
        }

        return;
    }

    if ((this->m_layoutFlags & 0x400) && ((this->m_layoutFlags & 0x200) || this->m_resizeLink.IsLinked())) {
        this->m_layoutFlags = (this->m_layoutFlags & ~0xFFu) | 6;
        return;
    }

    this->m_layoutFlags |= 0x400;

    if (this->m_layoutFlags & 0x200) {
        for (CLayoutFrame::FRAMENODE* node = this->m_resizeList.Head(); node; node = this->m_resizeList.Next(node)) {
            node->m_frame->Resize(0);
        }
    } else {
        this->AddToResizeList();
    }
}

// VERIFY: 2a588003 manual
// OFFSET: 0x00489E90
bool CLayoutFrame::SetLayoutScale(float scale, bool force) {
    if (scale == 0.0f) {
        DebugValidate("scale");
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return false;
    }

    if ((force || fabs(scale - this->m_layoutScale) >= 0.00000023841858f) && scale > 0.00000011920929f) {
        this->m_layoutScale = scale;
        this->m_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
        this->m_layoutFlags &= ~0x100u;
        this->Resize(0);
        return true;
    }

    return false;
}

// VERIFY: f0599e98 auto
// OFFSET: 0x00489F20
bool CLayoutFrame::SetLayoutDepth(float depth, bool force) {
    if (!force && fabs(depth - this->m_layoutDepth) < 0.00000023841858f) {
        return false;
    }

    if (depth < 0.2f) {
        depth = 0.2f;
    }

    this->m_layoutFlags &= ~0x100u;
    this->m_layoutDepth = depth;
    this->Resize(0);

    return true;
}

// VERIFY: 56b128ef manual
// OFFSET: 0x00489F80
void CLayoutFrame::SetWidth(float width) {
    this->m_layoutFlags &= ~0x800u;
    this->m_width = width;
    this->Resize(0);
}

// VERIFY: cd16a710 manual
// OFFSET: 0x00489FA0
void CLayoutFrame::SetHeight(float height) {
    this->m_layoutFlags &= ~0x800u;
    this->m_height = height;
    this->Resize(0);
}

// VERIFY: a65146a7 auto
// OFFSET: 0x00489FC0
void CLayoutFrame::SetSize(float width, float height) {
    this->m_layoutFlags &= ~0x800u;
    this->m_width = width;
    this->m_height = height;
    this->Resize(0);
}

// VERIFY: da7ad489 manual
// OFFSET: 0x00489FF0
void CLayoutFrame::GetSize(float& width, float& height, int32_t ignoreRect) {
    width = this->GetWidth();
    height = this->GetHeight();

    if (ignoreRect || (height != 0.0f && width != 0.0f)) {
        return;
    }

    if (this->IsResizePending()) {
        this->Resize(1);
    }

    if (this->m_layoutFlags & 0x100) {
        width = (this->m_rect.maxX - this->m_rect.minX) / this->m_layoutScale;
        height = (this->m_rect.maxY - this->m_rect.minY) / this->m_layoutScale;
    }
}

// VERIFY: a3e1dea1 manual
// OFFSET: 0x0048A130
void CLayoutFrame::SetClampedToScreen(int32_t clamped) {
    if (clamped) {
        this->m_layoutFlags |= 0x1000;
    } else {
        this->m_layoutFlags &= ~0x1000u;
    }

    this->Resize(0);
}

// VERIFY: e34ecf75 auto
// OFFSET: 0x0048A160
int32_t CLayoutFrame::CanBeAnchorFor(CLayoutFrame* frame) {
    if (!frame->IsProtected()) {
        return 1;
    }

    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        CFramePoint* framePoint = this->m_framePoints[i];

        if (framePoint && !(framePoint->m_flags & 8) && framePoint->m_relative && !framePoint->m_relative->CanBeAnchorFor(frame)) {
            return 0;
        }
    }

    return 1;
}

// VERIFY: 60982474 manual
// OFFSET: 0x0048A1D0
void CLayoutFrame::OnFrameSizeChanged(const CRect& rect) {
    for (CLayoutFrame::FRAMENODE* node = this->m_resizeList.Head(); node; node = this->m_resizeList.Next(node)) {
        node->m_frame->Resize(0);
    }
}

// VERIFY: c3610066 auto
// OFFSET: [0x0048A200, 0x0048A510]
void CLayoutFrame::MarkFramePointsUnused() {
    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        CFramePoint* framePoint = this->m_framePoints[i];

        if (framePoint && !(framePoint->m_flags & 8)) {
            if (framePoint->m_relative) {
                framePoint->m_relative->UnregisterResize(this, 1 << i);
            }

            framePoint->MarkUnused();
        }
    }
}

// VERIFY: ba8eaac3 manual
// OFFSET: 0x0048A260
void CLayoutFrame::SetPoint(FRAMEPOINT point, CLayoutFrame* relative, FRAMEPOINT relativePoint, float offsetX, float offsetY, int32_t doResize) {
    if (!relative) {
        DebugValidate("relative");
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (relative == this) {
        DebugValidate("relative != this");
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (!relative->CanBeAnchorFor(this)) {
        return;
    }

    CFramePoint* framePoint = this->m_framePoints[point];

    if (!framePoint) {
        this->m_framePoints[point] = new CFramePoint(relative, relativePoint, offsetX, offsetY);
    } else if (framePoint->m_relative && framePoint->m_relative == relative) {
        if (static_cast<int8_t>(framePoint->m_framePoint) == relativePoint && !fnotequal(framePoint->m_x, offsetX) && !fnotequal(framePoint->m_y, offsetY)) {
            return;
        }

        framePoint->SetRelative(relative, relativePoint, offsetX, offsetY);
        this->m_layoutFlags &= ~0x800u;

        if (doResize) {
            this->Resize(0);
        }

        return;
    } else {
        if (framePoint->m_relative) {
            framePoint->m_relative->UnregisterResize(this, 1 << point);
        }

        framePoint->SetRelative(relative, relativePoint, offsetX, offsetY);
    }

    this->m_layoutFlags &= ~0x800u;
    relative->RegisterResize(this, 1 << point);

    if (doResize) {
        this->Resize(0);
    }
}

// VERIFY: b715a4ff manual
// OFFSET: 0x0048A3E0
void CLayoutFrame::SetAllPoints(CLayoutFrame* relative, int32_t doResize) {
    if (!relative) {
        DebugValidate("relative");
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (relative == this) {
        DebugValidate("relative != this");
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (!relative->CanBeAnchorFor(this)) {
        return;
    }

    this->MarkFramePointsUnused();

    if (this->m_framePoints[FRAMEPOINT_TOPLEFT]) {
        this->m_framePoints[FRAMEPOINT_TOPLEFT]->SetRelative(relative, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    } else {
        this->m_framePoints[FRAMEPOINT_TOPLEFT] = new CFramePoint(relative, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    }

    if (this->m_framePoints[FRAMEPOINT_BOTTOMRIGHT]) {
        this->m_framePoints[FRAMEPOINT_BOTTOMRIGHT]->SetRelative(relative, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    } else {
        this->m_framePoints[FRAMEPOINT_BOTTOMRIGHT] = new CFramePoint(relative, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);
    }

    this->m_layoutFlags &= ~0x800u;
    relative->RegisterResize(this, (1 << FRAMEPOINT_TOPLEFT) | (1 << FRAMEPOINT_BOTTOMRIGHT));

    if (doResize) {
        this->Resize(0);
    }
}

// VERIFY: 155a7ca0 manual
// OFFSET: 0x0048A520
void CLayoutFrame::SetDeferredResize(int32_t deferred) {
    if (deferred) {
        this->m_layoutFlags |= 0x200;

        if (this->m_resizeLink.IsLinked()) {
            s_resizePendingList.UnlinkNode(this);
        }

        return;
    }

    this->m_layoutFlags &= ~0x200u;

    if (this->IsResizePending()) {
        this->Resize(1);
    }
}

// VERIFY: b8ad80b2 manual
// OFFSET: 0x0048A5F0
int32_t CLayoutFrame::FlattenFrame(CLayoutFrame* relative, float width, float height, float offsetX, float offsetY) {
    if (this->IsResizePending()) {
        this->Resize(1);
    }

    if (!(this->m_layoutFlags & 0x100)) {
        return 0;
    }

    CRect rect = this->m_rect;

    if (!(relative->m_layoutFlags & 0x100)) {
        return 0;
    }

    float relativeTop = relative->m_rect.maxY;
    float relativeLeft = relative->m_rect.minX;

    this->MarkFramePointsUnused();

    float invScale = 1.0f / this->m_layoutScale;
    float y = -(relativeTop - (rect.maxY + offsetY)) * invScale;
    float x = invScale * -(relativeLeft - (rect.minX + offsetX));
    this->SetPoint(FRAMEPOINT_TOPLEFT, relative, FRAMEPOINT_TOPLEFT, x, y, 0);

    if (width == 0.0f) {
        width = (rect.maxX - rect.minX) / this->m_layoutScale;
    }

    this->SetWidth(width);

    if (height == 0.0f) {
        height = (rect.maxY - rect.minY) / this->m_layoutScale;
    }

    this->SetHeight(height);
    this->Resize(1);

    return 1;
}

// VERIFY: 1e35598c manual
// OFFSET: 0x0048A840
int32_t CLayoutFrame::UnflattenFrame(CLayoutFrame* relative) {
    if (this->IsResizePending()) {
        this->Resize(1);
    }

    if (!(this->m_layoutFlags & 0x100)) {
        return 0;
    }

    CRect rect = this->m_rect;

    if (!(relative->m_layoutFlags & 0x100)) {
        return 0;
    }

    CRect relativeRect = relative->m_rect;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    C2Vector relativePoints[FRAMEPOINT_NUMPOINTS];
    C2Vector points[FRAMEPOINT_NUMPOINTS];
    FRAMEPOINT closest = FRAMEPOINT_TOPLEFT;

    GetRectFramePoints(relativeRect, relativePoints);
    GetRectFramePoints(rect, points);

    float closestDistance = std::numeric_limits<float>::max();

    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        float dx = relativePoints[i].x - points[i].x;
        float dy = relativePoints[i].y - points[i].y;
        float distance = dx * dx + dy * dy;

        if (distance < closestDistance) {
            closestDistance = distance;
            closest = static_cast<FRAMEPOINT>(i);
            offsetX = points[i].x - relativePoints[i].x;
            offsetY = points[i].y - relativePoints[i].y;
        }
    }

    this->MarkFramePointsUnused();

    float scale = 1.0f / this->m_layoutScale;
    this->SetPoint(closest, relative, closest, offsetX * scale, offsetY * scale, 0);
    this->Resize(1);

    return 1;
}

// VERIFY: d18d2b15 manual
// OFFSET: 0x0048ABD0
int32_t CLayoutFrame::DragBy(CLayoutFrame* relative, float offsetX, float offsetY, FRAMEPOINT point) {
    float width = this->GetWidth();
    float height = this->GetHeight();
    float newOffsetX;
    float newOffsetY;

    switch (point) {
        case FRAMEPOINT_TOPLEFT: {
            float scale = 1.0f / this->m_layoutScale;
            newOffsetX = offsetX;
            width = width - scale * offsetX;

            if (0.0f != this->m_resizeRect.minX && this->m_resizeRect.minX > width) {
                newOffsetX = newOffsetX - (this->m_resizeRect.minX - width) * this->m_layoutScale;
                width = this->m_resizeRect.minX;
            }

            if (0.0f != this->m_resizeRect.maxX && this->m_resizeRect.maxX < width) {
                newOffsetX = newOffsetX + (width - this->m_resizeRect.maxX) * this->m_layoutScale;
                width = this->m_resizeRect.maxX;
            }

            newOffsetY = offsetY;
            height = height + scale * offsetY;

            if (0.0f != this->m_resizeRect.minY && this->m_resizeRect.minY > height) {
                newOffsetY = newOffsetY + (this->m_resizeRect.minY - height) * this->m_layoutScale;
                height = this->m_resizeRect.minY;
            }

            if (0.0f != this->m_resizeRect.maxY && this->m_resizeRect.maxY < height) {
                newOffsetY = newOffsetY - (height - this->m_resizeRect.maxY) * this->m_layoutScale;
                height = this->m_resizeRect.maxY;
            }

            break;
        }

        case FRAMEPOINT_TOP: {
            newOffsetY = offsetY;
            height = height + offsetY / this->m_layoutScale;

            if (0.0f != this->m_resizeRect.minY && this->m_resizeRect.minY > height) {
                newOffsetY = newOffsetY + (this->m_resizeRect.minY - height) * this->m_layoutScale;
                height = this->m_resizeRect.minY;
            }

            if (0.0f != this->m_resizeRect.maxY && this->m_resizeRect.maxY < height) {
                newOffsetY = newOffsetY - (height - this->m_resizeRect.maxY) * this->m_layoutScale;
                height = this->m_resizeRect.maxY;
            }

            newOffsetX = 0.0f;
            break;
        }

        case FRAMEPOINT_TOPRIGHT: {
            float scale = 1.0f / this->m_layoutScale;
            width = offsetX * scale + width;

            if (0.0f != this->m_resizeRect.minX && this->m_resizeRect.minX > width) {
                width = this->m_resizeRect.minX;
            }

            if (0.0f != this->m_resizeRect.maxX && this->m_resizeRect.maxX < width) {
                width = this->m_resizeRect.maxX;
            }

            newOffsetY = offsetY;
            height = height + scale * offsetY;

            if (0.0f != this->m_resizeRect.minY && this->m_resizeRect.minY > height) {
                newOffsetY = newOffsetY + (this->m_resizeRect.minY - height) * this->m_layoutScale;
                height = this->m_resizeRect.minY;
            }

            if (0.0f != this->m_resizeRect.maxY && this->m_resizeRect.maxY < height) {
                newOffsetY = newOffsetY - (height - this->m_resizeRect.maxY) * this->m_layoutScale;
                height = this->m_resizeRect.maxY;
            }

            newOffsetX = 0.0f;
            break;
        }

        case FRAMEPOINT_LEFT: {
            newOffsetX = offsetX;
            width = width - offsetX / this->m_layoutScale;

            if (0.0f != this->m_resizeRect.minX && this->m_resizeRect.minX > width) {
                newOffsetX = newOffsetX - (this->m_resizeRect.minX - width) * this->m_layoutScale;
                width = this->m_resizeRect.minX;
            }

            if (0.0f != this->m_resizeRect.maxX && this->m_resizeRect.maxX < width) {
                newOffsetX = newOffsetX + (width - this->m_resizeRect.maxX) * this->m_layoutScale;
                width = this->m_resizeRect.maxX;
            }

            newOffsetY = 0.0f;
            break;
        }

        case FRAMEPOINT_CENTER: {
            CFramePoint* framePoint = this->GetFramePoint(FRAMEPOINT_TOPLEFT);

            if (framePoint && framePoint->m_relative) {
                framePoint->m_x = offsetX / this->m_layoutScale + framePoint->m_x;
                framePoint->m_y = offsetY / this->m_layoutScale + framePoint->m_y;
                this->Resize(1);
                return 1;
            }

            newOffsetX = offsetX;
            newOffsetY = offsetY;
            break;
        }

        case FRAMEPOINT_RIGHT: {
            width = offsetX / this->m_layoutScale + width;

            if (0.0f != this->m_resizeRect.minX && this->m_resizeRect.minX > width) {
                width = this->m_resizeRect.minX;
            }

            if (0.0f != this->m_resizeRect.maxX && this->m_resizeRect.maxX < width) {
                width = this->m_resizeRect.maxX;
            }

            newOffsetX = 0.0f;
            newOffsetY = 0.0f;
            break;
        }

        case FRAMEPOINT_BOTTOMLEFT: {
            float scale = 1.0f / this->m_layoutScale;
            newOffsetX = offsetX;
            width = width - scale * offsetX;

            if (0.0f != this->m_resizeRect.minX && this->m_resizeRect.minX > width) {
                newOffsetX = newOffsetX - (this->m_resizeRect.minX - width) * this->m_layoutScale;
                width = this->m_resizeRect.minX;
            }

            if (0.0f != this->m_resizeRect.maxX && this->m_resizeRect.maxX < width) {
                newOffsetX = newOffsetX + (width - this->m_resizeRect.maxX) * this->m_layoutScale;
                width = this->m_resizeRect.maxX;
            }

            newOffsetY = 0.0f;
            height = height - scale * offsetY;

            if (0.0f != this->m_resizeRect.minY && this->m_resizeRect.minY > height) {
                height = this->m_resizeRect.minY;
            }

            if (0.0f != this->m_resizeRect.maxY && this->m_resizeRect.maxY < height) {
                height = this->m_resizeRect.maxY;
            }

            break;
        }

        case FRAMEPOINT_BOTTOM: {
            height = height - offsetY / this->m_layoutScale;
            newOffsetY = 0.0f;

            if (0.0f != this->m_resizeRect.minY && this->m_resizeRect.minY > height) {
                height = this->m_resizeRect.minY;
            }

            if (0.0f != this->m_resizeRect.maxY && this->m_resizeRect.maxY < height) {
                height = this->m_resizeRect.maxY;
            }

            newOffsetX = 0.0f;
            break;
        }

        case FRAMEPOINT_BOTTOMRIGHT: {
            float scale = 1.0f / this->m_layoutScale;
            width = offsetX * scale + width;

            if (0.0f != this->m_resizeRect.minX && this->m_resizeRect.minX > width) {
                width = this->m_resizeRect.minX;
            }

            if (0.0f != this->m_resizeRect.maxX && this->m_resizeRect.maxX < width) {
                width = this->m_resizeRect.maxX;
            }

            newOffsetY = 0.0f;
            height = height - scale * offsetY;

            if (0.0f != this->m_resizeRect.minY && this->m_resizeRect.minY > height) {
                height = this->m_resizeRect.minY;
            }

            if (0.0f != this->m_resizeRect.maxY && this->m_resizeRect.maxY < height) {
                height = this->m_resizeRect.maxY;
            }

            newOffsetX = 0.0f;
            break;
        }

        default: {
            newOffsetX = offsetX;
            newOffsetY = offsetY;
            break;
        }
    }

    return this->FlattenFrame(relative, width, height, newOffsetX, newOffsetY);
}

// VERIFY: 6d735449 manual
// OFFSET: 0x0048B130
void CLayoutFrame::DestroyLayout() {
    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        CFramePoint* framePoint = this->m_framePoints[i];

        if (framePoint) {
            if (!(framePoint->m_flags & 8) && framePoint->m_relative) {
                framePoint->m_relative->UnregisterResize(this, 1 << i);
            }

            delete framePoint;
            this->m_framePoints[i] = nullptr;
        }
    }

    for (CLayoutFrame::FRAMENODE* node = this->m_resizeList.Head(); node; node = this->m_resizeList.Next(node)) {
        CLayoutFrame* frame = node->m_frame;

        for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
            CFramePoint* framePoint = frame->m_framePoints[i];

            if (framePoint && framePoint->m_relative == this) {
                framePoint->MarkUnused();
            }
        }

        frame->m_layoutFlags &= ~0x800u;
    }

    this->m_resizeList.Clear();
    s_resizePendingList.UnlinkNode(this);
}

// VERIFY: 1cfa0c14 manual
// OFFSET: [0x0048B240, 0x0048B510]
CLayoutFrame::~CLayoutFrame() {
    this->DestroyLayout();
}

// VERIFY: 9b1f7ce5 auto
// OFFSET: 0x0048B2D0
void CLayoutFrame::LoadXML(XMLNode* node, CStatus* status) {
    XMLNode* sizeNode = node->GetChildByName("Size");
    float x;
    float y;

    if (sizeNode && LoadXML_Dimensions(sizeNode, x, y, status)) {
        this->SetWidth(x);
        this->SetHeight(y);
    }

    CLayoutFrame* parent = this->GetLayoutParent();
    int32_t setAllPoints = 0;

    const char* setAllPointsAttr = node->GetAttributeByName("setAllPoints");

    if (setAllPointsAttr && !SStrCmpI(setAllPointsAttr, "true", STORM_MAX_STR)) {
        setAllPoints = 1;
    }

    XMLNode* anchorsNode = node->GetChildByName("Anchors");

    if (!anchorsNode) {
        if (setAllPoints) {
            this->SetAllPoints(parent, 1);
        }

        return;
    }

    if (setAllPoints) {
        status->Add(STATUS_WARNING, "SETALLPOINTS set to true in frame with anchors (ignored)");
    }

    for (XMLNode* anchor = anchorsNode->m_child; anchor; anchor = anchor->m_next) {
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        CLayoutFrame* relative = parent;
        FRAMEPOINT point;
        FRAMEPOINT relativePoint;

        const char* pointAttr = anchor->GetAttributeByName("point");

        if (!pointAttr || !StringToFramePoint(pointAttr, point)) {
            status->Add(STATUS_WARNING, "Invalid anchor point in frame: %s", pointAttr);
            continue;
        }

        const char* relativePointAttr = anchor->GetAttributeByName("relativePoint");

        if (relativePointAttr && *relativePointAttr) {
            if (!StringToFramePoint(relativePointAttr, relativePoint)) {
                status->Add(STATUS_WARNING, "Invalid anchor point in frame: %s", relativePointAttr);
                continue;
            }
        } else {
            relativePoint = point;
        }

        const char* relativeTo = anchor->GetAttributeByName("relativeTo");

        if (relativeTo && *relativeTo) {
            relative = this->GetLayoutFrameByName(relativeTo);

            if (!relative) {
                status->Add(STATUS_WARNING, "Couldn't find relative frame: %s", relativeTo);
                continue;
            }

            if (relative == this) {
                status->Add(STATUS_WARNING, "Frame anchored to itself: %s", relativeTo);
                continue;
            }
        }

        XMLNode* offsetNode = anchor->GetChildByName("Offset");

        if (offsetNode) {
            LoadXML_Dimensions(offsetNode, offsetX, offsetY, status);
        } else {
            LoadXML_Dimensions(anchor, offsetX, offsetY, status);
        }

        this->SetPoint(point, relative, relativePoint, offsetX, offsetY, 0);
    }

    this->Resize(0);
}

CLayoutFrame* CLayoutFrame::GetLayoutParent() {
    return nullptr;
}

void CLayoutFrame::PropagateProtectFlagToParent(uint32_t flag) {
}

int32_t CLayoutFrame::IsAttachmentOrigin() {
    return 0;
}

CLayoutFrame* CLayoutFrame::GetLayoutFrameByName(const char* name) {
    return nullptr;
}

int32_t CLayoutFrame::IsObjectLoaded() {
    return 1;
}
