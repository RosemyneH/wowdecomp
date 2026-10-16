// ʕ •ᴥ•ʔ✿ AVX2/FMA skinning. This file grows a body only when WOWDECOMP_AVX2_SKIN is set. ✿ ʕ •ᴥ•ʔ
#if defined(WOWDECOMP_AVX2_SKIN)

#include <cstdint>
#include <immintrin.h>
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"

namespace {

const float kWeightScale = 0.0039215689f;

struct Rows {
    __m128 row0;
    __m128 row1;
    __m128 row2;
    __m128 row3;
};

void BlendRows(const C44Matrix* bones, ubyte4 weights, ubyte4 boneIndices, Rows& rows) {
    const __m128 weight0 = _mm_set1_ps(weights[0] * kWeightScale);
    const float* bone = reinterpret_cast<const float*>(&bones[boneIndices[0]]);
    rows.row0 = _mm_mul_ps(_mm_load_ps(bone), weight0);
    rows.row1 = _mm_mul_ps(_mm_load_ps(bone + 4), weight0);
    rows.row2 = _mm_mul_ps(_mm_load_ps(bone + 8), weight0);
    rows.row3 = _mm_mul_ps(_mm_load_ps(bone + 12), weight0);

    for (uint32_t i = 1; i < 4 && weights[i]; ++i) {
        const __m128 weight = _mm_set1_ps(weights[i] * kWeightScale);
        bone = reinterpret_cast<const float*>(&bones[boneIndices[i]]);
        rows.row0 = _mm_fmadd_ps(_mm_load_ps(bone), weight, rows.row0);
        rows.row1 = _mm_fmadd_ps(_mm_load_ps(bone + 4), weight, rows.row1);
        rows.row2 = _mm_fmadd_ps(_mm_load_ps(bone + 8), weight, rows.row2);
        rows.row3 = _mm_fmadd_ps(_mm_load_ps(bone + 12), weight, rows.row3);
    }
}

Rows RowsFor(const C44Matrix* bones, const M2Vertex* vertex, uint32_t& cachedWeights, uint32_t& cachedIndices, int32_t& cached, Rows& cachedRows) {
    const uint32_t weights = static_cast<uint32_t>(vertex->m_weights);
    const uint32_t indices = static_cast<uint32_t>(vertex->m_indices);

    if (!cached || weights != cachedWeights || indices != cachedIndices) {
        BlendRows(bones, vertex->m_weights, vertex->m_indices, cachedRows);
        cachedWeights = weights;
        cachedIndices = indices;
        cached = 1;
    }

    return cachedRows;
}

Rows RowsFromBone(const C44Matrix& bone) {
    const float* m = reinterpret_cast<const float*>(&bone);
    Rows rows;
    rows.row0 = _mm_load_ps(m);
    rows.row1 = _mm_load_ps(m + 4);
    rows.row2 = _mm_load_ps(m + 8);
    rows.row3 = _mm_load_ps(m + 12);
    return rows;
}

__m256 PackRows(__m128 a, __m128 b) {
    return _mm256_insertf128_ps(_mm256_castps128_ps256(a), b, 1);
}

__m256 Broadcast2(float a, float b) {
    return _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_set1_ps(a)), _mm_set1_ps(b), 1);
}

void StoreVertex(float* out, __m128 position, __m128 normal, const M2Vertex* vertex, int32_t texCoord) {
    _mm_storeu_ps(out, position);
    _mm_storeu_ps(out + 3, normal);
    out[6] = vertex->m_texcoord[texCoord].x;
    out[7] = vertex->m_texcoord[texCoord].y;
}

void SkinOne(float* out, const M2Vertex* vertex, int32_t texCoord, const Rows& rows) {
    const __m128 position = _mm_fmadd_ps(rows.row2, _mm_set1_ps(vertex->m_position.z), _mm_fmadd_ps(rows.row1, _mm_set1_ps(vertex->m_position.y), _mm_fmadd_ps(rows.row0, _mm_set1_ps(vertex->m_position.x), rows.row3)));
    const __m128 normal = _mm_fmadd_ps(rows.row2, _mm_set1_ps(vertex->m_normal.z), _mm_fmadd_ps(rows.row1, _mm_set1_ps(vertex->m_normal.y), _mm_mul_ps(rows.row0, _mm_set1_ps(vertex->m_normal.x))));
    StoreVertex(out, position, normal, vertex, texCoord);
}

void SkinPair(float* out, const M2Vertex* a, const M2Vertex* b, int32_t texCoord, const Rows& ra, const Rows& rb) {
    const __m256 row0 = PackRows(ra.row0, rb.row0);
    const __m256 row1 = PackRows(ra.row1, rb.row1);
    const __m256 row2 = PackRows(ra.row2, rb.row2);
    const __m256 row3 = PackRows(ra.row3, rb.row3);
    const __m256 position = _mm256_fmadd_ps(row2, Broadcast2(a->m_position.z, b->m_position.z), _mm256_fmadd_ps(row1, Broadcast2(a->m_position.y, b->m_position.y), _mm256_fmadd_ps(row0, Broadcast2(a->m_position.x, b->m_position.x), row3)));
    const __m256 normal = _mm256_fmadd_ps(row2, Broadcast2(a->m_normal.z, b->m_normal.z), _mm256_fmadd_ps(row1, Broadcast2(a->m_normal.y, b->m_normal.y), _mm256_mul_ps(row0, Broadcast2(a->m_normal.x, b->m_normal.x))));
    StoreVertex(out, _mm256_castps256_ps128(position), _mm256_castps256_ps128(normal), a, texCoord);
    StoreVertex(out + 8, _mm256_extractf128_ps(position, 1), _mm256_extractf128_ps(normal, 1), b, texCoord);
}

}

void M2BlendBoneMatricesAVX2(const C44Matrix* bones, ubyte4 weights, ubyte4 indices, C44Matrix* result) {
    Rows rows;
    BlendRows(bones, weights, indices, rows);
    float* out = reinterpret_cast<float*>(result);
    _mm_storeu_ps(out, rows.row0);
    _mm_storeu_ps(out + 4, rows.row1);
    _mm_storeu_ps(out + 8, rows.row2);
    _mm_storeu_ps(out + 12, rows.row3);
    result->a3 = 0.0f;
    result->b3 = 0.0f;
    result->c3 = 0.0f;
    result->d3 = 1.0f;
}

void CM2Model::TransformVertices_avx2(CM2Model* model, M2SkinSection* section, uint8_t* vertices, int32_t texCoord) {
    const C44Matrix* bones = model->m_boneMatrices;
    M2Vertex* vertex = &model->m_shared->m_data->m_vertices[section->m_vertexStart];
    M2Vertex* end = vertex + section->m_vertexCount;
    float* out = reinterpret_cast<float*>(vertices);
    uint32_t cachedWeights = 0;
    uint32_t cachedIndices = 0;
    int32_t cached = 0;
    Rows cachedRows;
    cachedRows.row0 = _mm_setzero_ps();
    cachedRows.row1 = _mm_setzero_ps();
    cachedRows.row2 = _mm_setzero_ps();
    cachedRows.row3 = _mm_setzero_ps();

    while (vertex + 1 < end) {
        const Rows a = RowsFor(bones, vertex, cachedWeights, cachedIndices, cached, cachedRows);
        const Rows b = RowsFor(bones, vertex + 1, cachedWeights, cachedIndices, cached, cachedRows);
        SkinPair(out, vertex, vertex + 1, texCoord, a, b);
        vertex += 2;
        out += 16;
    }

    if (vertex < end) {
        SkinOne(out, vertex, texCoord, RowsFor(bones, vertex, cachedWeights, cachedIndices, cached, cachedRows));
    }

    _mm256_zeroupper();
}

void CM2Model::TransformVerticesOneBone_avx2(CM2Model* model, M2SkinSection* section, uint8_t* vertices, int32_t texCoord) {
    const C44Matrix* bones = model->m_boneMatrices;
    M2Vertex* vertex = &model->m_shared->m_data->m_vertices[section->m_vertexStart];
    M2Vertex* end = vertex + section->m_vertexCount;
    float* out = reinterpret_cast<float*>(vertices);

    while (vertex + 1 < end) {
        SkinPair(out, vertex, vertex + 1, texCoord, RowsFromBone(bones[vertex[0].m_indices.b[0]]), RowsFromBone(bones[vertex[1].m_indices.b[0]]));
        vertex += 2;
        out += 16;
    }

    if (vertex < end) {
        SkinOne(out, vertex, texCoord, RowsFromBone(bones[vertex->m_indices.b[0]]));
    }

    _mm256_zeroupper();
}

#endif
