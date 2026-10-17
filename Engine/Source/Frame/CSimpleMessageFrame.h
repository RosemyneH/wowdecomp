#ifndef ENGINE_SOURCE_FRAME_CSIMPLEMESSAGEFRAME_H
#define ENGINE_SOURCE_FRAME_CSIMPLEMESSAGEFRAME_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Base/CDataAllocator.h"
#include "Engine/Source/Base/RefCount.h"
#include "Engine/Source/Frame/CSimpleFontedFrame.h"
#include "Engine/Source/Frame/CSimpleFrame.h"
#include "Engine/Source/Frame/Types.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "Engine/Source/Tempest/CRect.h"

class CSimpleFontString;
class CStatus;
class XMLNode;

struct MessageData {
    /* 0000 */ char* m_text;
    /* 0004 */ CImVector m_color;
};

class CSimpleMessageFrameLineNode : public TRefCnt {
    public:
    /* 0008 */ CImVector m_color;
    /* 000C */ CSimpleFontString* m_string;
    /* 0010 */ float m_timeLeft;
    /* 0014 */ float m_fadeLeft;
    /* 0018 */ bool m_isVisible;

    virtual ~CSimpleMessageFrameLineNode();

    CSimpleMessageFrameLineNode();
};

class CSimpleMessageFrameLine {
    public:
    /* 0000 */ float m_offsetX;
    /* 0004 */ float m_offsetY;
    /* 0008 */ CSimpleMessageFrameLineNode* m_node;

    CSimpleMessageFrameLine();
    CSimpleMessageFrameLine(const CSimpleMessageFrameLine& source);
    ~CSimpleMessageFrameLine();
};

class CSimpleMessageFrame : public CSimpleFrame, public CSimpleFontedFrame {
    public:
    static CDataAllocator s_allocator;
    static int32_t s_metatable;
    static int32_t s_objectType;
    static const char* s_objectTypeName;

    static void operator delete(void* ptr);
    static void CreateScriptMetaTable();
    static void DestroyScriptMetaTable();
    static int32_t GetObjectType();
    static void RegisterScriptMethods(lua_State* L);

    /* 02A0 */ uint32_t m_rows;
    /* 02A4 */ uint32_t m_numVisible;
    /* 02A8 */ CRect m_messageFrameArea;
    /* 02B8 */ CRect m_messageFrameInset;
    /* 02C8 */ int32_t m_textMaxSize;
    /* 02CC */ CSimpleFontedFrameFont* m_font;
    /* 02D0 */ int32_t m_fading;
    /* 02D4 */ float m_timeVisible;
    /* 02D8 */ float m_fadeDuration;
    /* 02DC */ SimpleMessageFrameInsertMode m_insertMode;
    /* 02E0 */ TSGrowableArray<MessageData> m_pendingMessages;
    /* 02F0 */ TSGrowableArray<CSimpleMessageFrameLine> m_lines;

    virtual ~CSimpleMessageFrame();
    virtual int32_t GetScriptMetaTable();
    virtual bool IsA(int32_t type);
    virtual bool IsA(const char* typeName);
    virtual const char* GetObjectTypeName();
    virtual void OnLayerUpdate(float elapsedSec);
    virtual void LoadXML(XMLNode* node, CStatus* status);
    virtual void OnFrameSizeChanged(const CRect& rect);
    virtual void AddMessage(const char* text, const CImVector& color);
    virtual void FontUpdated(CSimpleFontedFrameFont* font, int32_t flags);

    CSimpleMessageFrame(CSimpleFrame* parent);
    void AddPendingMessage(const char* text, const CImVector& color);
    void Clear();
    void ClearPending();
    void HideLineNode(CSimpleMessageFrameLineNode* node);
    void ScrollMessages(uint32_t index);
    void SetFadeDuration(float duration);
    void SetInsertMode(SimpleMessageFrameInsertMode mode);
    void SetTimeVisible(float seconds);
    void ShowLineNode(CSimpleMessageFrameLineNode* node, float timeVisible, float fadeDuration);
    void UpdateRect();
};

#endif
