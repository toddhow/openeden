#ifndef OPENEDEN_COMPONENTS_BODY_BODYCONTRACT_H
#define OPENEDEN_COMPONENTS_BODY_BODYCONTRACT_H

// OpenEden AdvancedBodies, stage B1 (task 0063): the pieces of the body contract that carry no package data.
//
// Contract headers hold no engine, VSG, Jolt, Qt or ESM types (root CLAUDE.md, "Architecture rules").
// Units everywhere in this library: metres, kilograms, radians, Z up, right-handed (CreativeMotion's convention).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Body
{
    /// The contract version this build understands. A package whose contractVersion is newer is refused
    /// (BODY-STRUCT-001). Additive changes bump the minor number, breaking ones the major number.
    inline constexpr int sContractMajor = 1;
    inline constexpr int sContractMinor = 0;

    /// The age class of a package, or of an actor (decision 0010, design doc section 14).
    enum class AgeClass
    {
        Unspecified,
        Minor,
        Adult,
    };

    /// The age gate. A restricted tier may run only when BOTH the package and the actor say Adult explicitly.
    /// Unspecified never counts as adult. Always built, never a setting (design doc section 14).
    bool restrictedTierAllowed(AgeClass package, AgeClass actor) noexcept;

    /// What the validator needs to know about one render mesh, as plain arrays: it never opens a mesh file.
    /// Positions of the vertices are NOT part of it in contract 1.0 (B2 adds them additively).
    struct MeshSummary
    {
        std::uint32_t mVertexCount = 0;
        std::uint32_t mTriangleCount = 0;
        std::array<double, 3> mBoundsMin{};
        std::array<double, 3> mBoundsMax{};
        /// The bones the skin refers to. Empty means the mesh is not skinned.
        std::vector<std::string> mBoneNames;
        /// Per-vertex influence runs: vertex v owns mInfluences[mInfluenceOffsets[v] .. mInfluenceOffsets[v + 1]).
        /// Empty mInfluenceOffsets means "no skin data given". Otherwise it has mVertexCount + 1 entries.
        std::vector<std::uint32_t> mInfluenceOffsets;
        struct Influence
        {
            std::uint32_t mBone = 0; // index into mBoneNames
            double mWeight = 0;
            friend bool operator==(const Influence&, const Influence&) = default;
        };
        std::vector<Influence> mInfluences;
        /// Three vertex indices per triangle, or empty when not given; otherwise size 3 * mTriangleCount.
        std::vector<std::uint32_t> mTriangleIndices;
        std::uint32_t mUvSetCount = 0;
        friend bool operator==(const MeshSummary&, const MeshSummary&) = default;
    };

    /// Where the validator (and the legacy adapter's caller) gets a MeshSummary for a mesh that has no inline one.
    /// Implementations must not throw. Versioned with the contract (task 0063, risk 5).
    class MeshSource
    {
    public:
        virtual ~MeshSource() = default;
        /// @param path the package's VFS path, @param format "nif", "gltf" or "summary". nullopt: not available.
        virtual std::optional<MeshSummary> summarize(std::string_view path, std::string_view format) const = 0;
    };
}

#endif
