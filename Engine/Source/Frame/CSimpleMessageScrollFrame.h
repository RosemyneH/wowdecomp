#ifndef ENGINE_SOURCE_FRAME_CSIMPLEMESSAGESCROLLFRAME_H
#define ENGINE_SOURCE_FRAME_CSIMPLEMESSAGESCROLLFRAME_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Base/CDataAllocator.h"
#include "Engine/Source/Base/RefCount.h"
#include "Engine/Source/Frame/CSimpleFontedFrame.h"
#include "Engine/Source/Frame/CSimpleFrame.h"
#include "Engine/Source/Frame/CSimpleHyperlinkedFrame.h"
#include "Engine/Source/Frame/CSimpleRender.h"
#include "Engine/Source/Frame/Types.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "Engine/Source/Tempest/CRect.h"

class CStatus;
class XMLNode;
struct lua_State;

class CSimpleFontStringRecord : public CSimpleFontString, public TRefCnt {
    public:
    static void operator delete(void* ptr);

    virtual ~CSimpleFontStringRecord();

    CSimpleFontStringRecord(CSimpleFrame* frame, uint32_t drawLayer, bool show);
};

class CSimpleMessageScrollFrameLine {
    public:
    /* 0000 */ char* m_text;
    /* 0004 */ CImVector m_color;
    /* 0008 */ int32_t m_isVisible;
    /* 000C */ float m_timeLeft;
    /* 0010 */ float m_fadeLeft;
    /* 0014 */ int32_t m_typeID;
    /* 0018 */ int32_t m_accessID;
    /* 001C */ int32_t m_extraData;

    CSimpleMessageScrollFrameLine();
    ~CSimpleMessageScrollFrameLine();
};

class CSimpleMessageScrollFrameDisplayNode : public TRefCnt {
    public:
    /* 0008 */ CSimpleFontStringRecord* m_string;
    /* 000C */ CSimpleMessageScrollFrameLine* m_line;

    virtual ~CSimpleMessageScrollFrameDisplayNode();

    CSimpleMessageScrollFrameDisplayNode();
    CSimpleMessageScrollFrameDisplayNode(const CSimpleMessageScrollFrameDisplayNode& source);
};

class CSimpleMessageScrollFrame : public CSimpleHyperlinkedFrame, public CSimpleFontedFrame {
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

    /* 02BC */ int32_t m_numMessages;
    /* 02C0 */ int32_t m_maxMessages;
    /* 02C4 */ int32_t m_currentLine;
    /* 02C8 */ int32_t m_currentScroll;
    /* 02CC */ int32_t m_numDisplayed;
    /* 02D0 */ int32_t m_atTop;
    /* 02D4 */ int32_t m_atBottom;
    /* 02D8 */ int32_t m_textMaxSize;
    /* 02DC */ CSimpleFontedFrameFont* m_font;
    /* 02E0 */ int32_t m_fading;
    /* 02E4 */ float m_timeVisible;
    /* 02E8 */ float m_fadeDuration;
    /* 02EC */ CRect m_messageFrameArea;
    /* 02FC */ CRect m_messageFrameInset;
    /* 030C */ TSGrowableArray<CSimpleMessageScrollFrameLine> m_lines;
    /* 031C */ TSGrowableArray<CSimpleMessageScrollFrameDisplayNode> m_displayNodes;
    /* 032C */ TSExplicitList<CSimpleHyperlinkButton, offsetof(CSimpleHyperlinkButton, m_hyperlinkLink)> m_hyperlinks;
    /* 0338 */ int32_t m_hyperlinksDirty;
    /* 033C */ FrameScript_Object::ScriptIx m_onMessageScrollChanged;
    /* 0344 */ SimpleMessageFrameInsertMode m_insertMode;
    /* 0348 */ bool m_hyperlinksEnabled;

    virtual ~CSimpleMessageScrollFrame();
    virtual int32_t GetScriptMetaTable();
    virtual FrameScript_Object::ScriptIx* GetScriptByName(const char* name, FrameScript_Object::ScriptData& data);
    virtual bool IsA(int32_t type);
    virtual bool IsA(const char* typeName);
    virtual const char* GetObjectTypeName();
    virtual void GetScriptTime(lua_State* L, double* time, uint32_t* count, int32_t recurse);
    virtual void OnLayerShow();
    virtual void OnLayerUpdate(float elapsedSec);
    virtual void OnScreenSizeChanged();
    virtual void LoadXML(XMLNode* node, CStatus* status);
    virtual void OnFrameSizeChanged(const CRect& rect);
    virtual void FontUpdated(CSimpleFontedFrameFont* font, int32_t flags);

    CSimpleMessageScrollFrame(CSimpleFrame* parent, int32_t maxLines);
    void AddMessage(const char* text, const CImVector& color, int32_t typeID, int32_t addToTop, int32_t accessID, int32_t extraData);
    void Clear();
    int32_t GetCurrentScroll();
    CSimpleMessageScrollFrameLine* GetMessageInfoByAccessID(uint32_t index, int32_t accessID);
    int32_t GetNumMessages(int32_t accessID);
    void PageDown();
    void PageUp();
    void RefreshHyperlinks();
    void RefreshMessages();
    void RemoveLinesByAccessID(int32_t accessID);
    int32_t ScrollDown();
    void ScrollMessages(int32_t scroll);
    void ScrollToBottom();
    void ScrollToTop();
    int32_t ScrollUp();
    void SetFadeDuration(float duration);
    void SetInsertMode(SimpleMessageFrameInsertMode mode);
    void SetMaxLines(int32_t maxLines);
    void SetScrollOffset(int32_t offset);
    void SetTimeVisible(float seconds);
    void UpdateColorByID(int32_t typeID, const CImVector& color);
    void UpdateNode(CSimpleMessageScrollFrameDisplayNode* node, CSimpleMessageScrollFrameLine* line, int32_t reset);
    void UpdateRect();
};

#endif
