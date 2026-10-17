#ifndef ENGINE_SOURCE_FRAME_CLAYOUTFRAME_H
#define ENGINE_SOURCE_FRAME_CLAYOUTFRAME_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Frame/Types.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Tempest/CRect.h"

class C2Vector;
class CFramePoint;
class CStatus;
class XMLNode;

class CLayoutFrame {
    public:
    enum GuardFlags : uint32_t {
        GUARD_NONE = 0,
        GUARD_LEFT = 0x01,
        GUARD_TOP = 0x02,
        GUARD_RIGHT = 0x04,
        GUARD_BOTTOM = 0x08,
        GUARD_CENTER_X = 0x10,
        GUARD_CENTER_Y = 0x20,
    };

    struct FRAMENODE : public TSLinkedNode<FRAMENODE> {
        /* 0008 */ CLayoutFrame* m_frame;
        /* 000C */ uint32_t m_dependency;
    };

    static void ResizePending();

    /* 0004 */ TSLink<CLayoutFrame> m_resizeLink;
    /* 000C */ CFramePoint* m_framePoints[FRAMEPOINT_NUMPOINTS];
    /* 0030 */ TSList<CLayoutFrame::FRAMENODE, TSGetLink<CLayoutFrame::FRAMENODE> > m_resizeList;
    /* 003C */ uint32_t m_guard;
    /* 0040 */ uint32_t m_layoutFlags;
    /* 0044 */ CRect m_rect;
    /* 0054 */ float m_width;
    /* 0058 */ float m_height;
    /* 005C */ float m_layoutScale;
    /* 0060 */ float m_layoutDepth;
    /* 0064 */ CRect m_resizeRect;

    virtual ~CLayoutFrame();
    virtual void LoadXML(XMLNode* node, CStatus* status);
    virtual CLayoutFrame* GetLayoutParent();
    virtual void PropagateProtectFlagToParent(uint32_t flag);
    virtual int32_t AreChildrenProtected(int32_t& deferred);
    virtual bool SetLayoutScale(float scale, bool force);
    virtual bool SetLayoutDepth(float depth, bool force);
    virtual void SetWidth(float width);
    virtual void SetHeight(float height);
    virtual void SetSize(float width, float height);
    virtual float GetWidth();
    virtual float GetHeight();
    virtual void GetSize(float& width, float& height, int32_t ignoreRect);
    virtual void GetClampRectInsets(float& left, float& right, float& top, float& bottom);
    virtual int32_t IsAttachmentOrigin();
    virtual int32_t CanBeAnchorFor(CLayoutFrame* frame);
    virtual CLayoutFrame* GetLayoutFrameByName(const char* name);
    virtual int32_t IsObjectLoaded();
    virtual void OnFrameSizeChanged(const CRect& rect);

    CLayoutFrame();
    void AddToResizeList();
    int32_t AreDependentsProtected(int32_t& deferred);
    float Bottom();
    int32_t CalculateRect(CRect* rect);
    float CenterX();
    float CenterY();
    void DestroyLayout();
    int32_t DragBy(CLayoutFrame* relative, float offsetX, float offsetY, FRAMEPOINT point);
    int32_t FlattenFrame(CLayoutFrame* relative, float width, float height, float offsetX, float offsetY);
    CFramePoint* GetFramePoint(FRAMEPOINT point);
    int32_t GetFramePointX(const FRAMEPOINT* points, int32_t count, float& x);
    int32_t GetFramePointY(const FRAMEPOINT* points, int32_t count, float& y);
    void GetHorizontalFramePoint(const FRAMEPOINT* points, int32_t count, float& x);
    int32_t GetRect(CRect* rect);
    int32_t HasAnyFramepoints();
    void GetVerticalFramePoint(const FRAMEPOINT* points, int32_t count, float& y);
    uint32_t IsProtected();
    int32_t IsResizeDependency(CLayoutFrame* frame);
    uint32_t IsResizePending();
    float Left();
    void MarkFramePointsUnused();
    int32_t OnFrameResize();
    void OnProtectedAttach(CLayoutFrame* frame);
    void OnProtectedDetach(CLayoutFrame* frame);
    void ProtectFramepoints(uint32_t flag);
    int32_t PtInFrameRect(const C2Vector& point);
    void RegisterResize(CLayoutFrame* frame, uint32_t dependency);
    void Resize(int32_t force);
    float Right();
    void SetAllPoints(CLayoutFrame* relative, int32_t doResize);
    void SetClampedToScreen(int32_t clamped);
    void SetDeferredResize(int32_t deferred);
    void SetPoint(FRAMEPOINT point, CLayoutFrame* relative, FRAMEPOINT relativePoint, float offsetX, float offsetY, int32_t doResize);
    void SetProtectFlag(uint32_t flag);
    void SetProtected();
    float Top();
    int32_t UnflattenFrame(CLayoutFrame* relative);
    void UnregisterResize(CLayoutFrame* frame, uint32_t dependency);
};

void GetRectFramePoints(const CRect& rect, C2Vector* points);

#endif
