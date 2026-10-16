#ifndef ENGINE_SOURCE_FRAMEXML_FRAMEXML_H
#define ENGINE_SOURCE_FRAMEXML_FRAMEXML_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Base/Types.h"
#include "Engine/Source/Storm/H/stpl.h"

class CSimpleFrame;
class XMLNode;
struct MD5_CTX;
struct XMLTree;

class CFrameXMLStatus : public CStatus {
    public:
    virtual void Add(STATUS_TYPE severity, const char* format, ...);
};

struct FrameFactoryNode : public TSHashObject<FrameFactoryNode, HASHKEY_STRI> {
    /* 0018 */ CSimpleFrame* (*m_factory)(CSimpleFrame* parent);
    /* 001C */ bool m_unique;
};

struct HashedNode : public TSHashObject<HashedNode, HASHKEY_STRI> {
    /* 0018 */ XMLNode* m_node;
    /* 001C */ const char* m_taint;
    /* 0020 */ bool m_locked;
};

CSimpleFrame* Create_SimpleButton(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleCheckButton(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleColorSelect(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleEditBox(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleFrame(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleHTML(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleMessageFrame(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleModel(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleMovieFrame(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleScrollFrame(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleScrollingMessageFrame(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleSlider(CSimpleFrame* parent);
CSimpleFrame* Create_SimpleStatusBar(CSimpleFrame* parent);
XMLNode* FrameXML_AcquireHashNode(const char* name, const char*& taint, bool& locked);
void FrameXML_ClearFactories();
CSimpleFrame* FrameXML_CreateFrame(XMLNode* node, CSimpleFrame* parent, CStatus* status);
int32_t FrameXML_CreateFrames(const char* tocFile, const char* wrapper, MD5_CTX* md5, CStatus* status);
void FrameXML_FreeHashNodes();
int32_t FrameXML_GetDebugLevel();
int32_t FrameXML_GuessNumFiles(const char* toc);
XMLTree* FrameXML_LoadXML(const char* filename, MD5_CTX* md5, CStatus* status);
int32_t FrameXML_ProcessFile(const char* filename, const char* wrapper, MD5_CTX* md5, CStatus* status);
int32_t FrameXML_ProcessFileNoHash(const char* filename, const char* wrapper, CStatus* status);
int32_t FrameXML_RegisterDefault();
int32_t FrameXML_RegisterFactory(const char* name, CSimpleFrame* (*factory)(CSimpleFrame* parent), bool unique);
void FrameXML_RegisterLoadProgressCallback(void (*callback)(float progress, void* param), void* param, uint32_t total);
void FrameXML_ReleaseHashNode(const char* name);
void FrameXML_SetDebugLevel(int32_t level);
void FrameXML_StoreHashNode(XMLNode* node, const char* name, const char* taint, CStatus* status);

#endif
