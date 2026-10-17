#ifndef ENGINE_SOURCE_GXU_MSDF_H
#define ENGINE_SOURCE_GXU_MSDF_H

#include <cstdint>
#include <freetype/freetype.h>

struct MsdfGlyph {
    void* m_rgba;
    uint32_t m_size;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_pitch;
    float m_bearing;
    float m_advance;
};

bool MsdfRasterizeGlyph(FT_Face face, uint32_t pixelSize, uint32_t code, uint32_t baseline, uint32_t cellHeight, MsdfGlyph* glyph);

#endif
