#ifndef OPENEDEN_COMPONENTS_BODY_BODYMESH_H
#define OPENEDEN_COMPONENTS_BODY_BODYMESH_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage c): the `bodymesh` payload, one merged body as bytes.
//
// LAYOUT VERSION 0 IS A DRAFT: it freezes only after toddhow approves it (like the LOD tile layout S1). Until then a
// reader refuses every other version and the version may change without a migration.
//
// What it holds: exactly the arrays of a MergedBody (bodymerge.hpp) that a renderer needs: positions, normals, UVs,
// the canonical bone list, four bone indices and weights per vertex, triangles, material keys, region ranges, submesh
// ranges and weld groups. What it leaves out: the vertex map, the part order and the report (they describe the
// merge, not the body; the cache manifest records what a bake needs).
//
// Byte layout (all integers little-endian, floats IEEE 754 binary32 little-endian, no padding):
//   header: magic "EDENBMSH" (8 bytes), u32 layoutVersion (0), u32 flags (bit 0 normals, bit 1 UVs, others 0),
//           then eight u32 counts: vertices, triangles, bones, materials, regions, submeshes, weldGroups,
//           weldMembers (the sum of the group sizes)
//   bones:       per bone u16 length, then the bytes (UTF-8, at most 128, no control characters, no duplicates)
//   materials:   per material u16 length, then the bytes (same rules; strictly increasing by byte value)
//   positions:   vertices x 3 f32
//   normals:     vertices x 3 f32 (only with flag bit 0)
//   uvs:         vertices x 2 f32 (only with flag bit 1)
//   boneIndices: vertices x 4 u16
//   boneWeights: vertices x 4 f32
//   triangles:   triangles x 3 u32
//   regions:     per region i32 slot, u32 firstTriangle, u32 triangleCount (slots strictly increasing)
//   submeshes:   per submesh u32 material, i32 slot, u32 firstTriangle, u32 triangleCount
//   weldGroups:  per group u32 size (at least 2), then weldMembers x u32 vertex indices in groups, each group
//                strictly increasing
// The file ends there: any other byte after it is an error. The writer is canonical: reading a payload and writing it
// again gives the same bytes, and the reader refuses anything the writer would not have written.
//
// Hostile input: every count is checked against BodyMeshLimits and against the bytes that remain BEFORE anything is
// allocated; floats must be finite; indices must be in range; weights must be >= 0 and sum to 1 within 1e-3 (a
// vertex with no influence is refused); regions and submeshes must lie inside the triangle list. A bad payload gives
// one located message ("positions: vertex 12: coordinate is not a finite number") and no body; nothing throws.
//
// Threading: pure functions. Ownership: the result is a value.

#include "bodymerge.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Body
{
    inline constexpr std::uint32_t bodyMeshLayoutVersion = 0; // DRAFT
    inline constexpr char bodyMeshMagic[8] = { 'E', 'D', 'E', 'N', 'B', 'M', 'S', 'H' };

    namespace BodyMeshLimits
    {
        inline constexpr std::uint32_t sMaxVertices = 500000;
        inline constexpr std::uint32_t sMaxTriangles = 1000000;
        inline constexpr std::uint32_t sMaxBones = 1024;
        inline constexpr std::uint32_t sMaxMaterials = 256;
        inline constexpr std::uint32_t sMaxRegions = 256;
        inline constexpr std::uint32_t sMaxSubmeshes = 4096;
        inline constexpr std::uint32_t sMaxWeldGroups = 65536;
        inline constexpr std::uint32_t sMaxWeldMembers = 1u << 20;
        inline constexpr std::size_t sMaxNameBytes = 128;
        /// The tolerance of the weight sum; the same as the body validator's (Thresholds::sWeightSumTolerance).
        inline constexpr double sWeightSumTolerance = 1e-3;
    }

    /// Writes the canonical payload of `body`. Returns false and sets `error` when the body does not satisfy the rules
    /// the reader enforces (the writer never writes what the reader would refuse).
    bool writeBodyMesh(const MergedBody& body, std::vector<std::uint8_t>& out, std::string& error);

    struct BodyMeshReadResult
    {
        std::optional<MergedBody> mBody; // the vertex map, part order and report are empty
        std::string mError; // meaningful only when mBody is empty
    };

    BodyMeshReadResult readBodyMesh(const std::uint8_t* data, std::size_t size);
    inline BodyMeshReadResult readBodyMesh(const std::vector<std::uint8_t>& bytes)
    {
        return readBodyMesh(bytes.data(), bytes.size());
    }
}

#endif
