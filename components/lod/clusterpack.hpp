#ifndef OPENMW_COMPONENTS_LOD_CLUSTERPACK_H
#define OPENMW_COMPONENTS_LOD_CLUSTERPACK_H

// OpenEden world LOD (task 0059, stage S4c). A derived Eden Archive pack of baked object clusters: one keyed entry per
// tile (type 0x0011, key tag 0x11) plus the manifest entry `lod/objects.json`. DRAFT layout version 0 (not approved).

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <edenarchive/archive.hpp>
#include <edenarchive/hash.hpp>

#include "loderror.hpp"
#include "objectcluster.hpp"
#include "tilepack.hpp"

namespace Lod
{
    inline constexpr std::string_view clusterManifestPath = "lod/objects.json";
    /// Provenance tool id of every cluster entry.
    inline constexpr std::string_view clusterToolId = "openeden.lod.objects";
    /// DRAFT: the layout version of the cluster entry and its key. The final layout will have another number.
    inline constexpr std::uint32_t clusterLayoutVersion = 0;

    /// What the pack holds. Canonical JSON, one line, alphabetical keys, no spaces; the parser accepts only that text.
    struct ClusterManifest
    {
        std::uint32_t mLayoutVersion = clusterLayoutVersion;
        std::uint32_t mBakerVersion = 0;
        std::uint32_t mCellSize = 0; // world units
        std::string mWorldspace;
        std::vector<std::uint32_t> mLevels; // levels baked
        std::vector<std::uint32_t> mErrors; // world units, one per level
        bool operator==(const ClusterManifest&) const = default;
    };

    std::string writeClusterManifestJson(const ClusterManifest& manifest);
    LodError parseClusterManifestJson(std::string_view text, ClusterManifest& manifest);

    struct BakedCluster
    {
        TileId mId;
        ObjectCluster mData;
        EdenArchive::Hash mSourceDigest{};
        std::vector<std::uint8_t> mParameters;
    };

    /// BLAKE3 of the manifest text and every cluster's source digest in key order.
    EdenArchive::Hash clusterContentDigest(const ClusterManifest& manifest, const std::vector<BakedCluster>& clusters);

    LodError buildClusterPack(const ClusterManifest& manifest, const std::vector<BakedCluster>& clusters,
        const TilePackOptions& options, std::vector<std::uint8_t>& out);
    LodError writeClusterPack(const ClusterManifest& manifest, const std::vector<BakedCluster>& clusters,
        const TilePackOptions& options, const std::filesystem::path& path);

    /// An open pack of clusters. Immutable; the const functions may be called from several threads.
    class ClusterPack
    {
    public:
        ~ClusterPack();
        ClusterPack(const ClusterPack&) = delete;
        ClusterPack& operator=(const ClusterPack&) = delete;

        static std::unique_ptr<ClusterPack> open(const std::filesystem::path& path, LodError& error);
        static std::unique_ptr<ClusterPack> open(std::shared_ptr<const EdenArchive::Source> source, LodError& error);

        const ClusterManifest& manifest() const noexcept;
        std::uint64_t generation() const noexcept;
        const EdenArchive::Hash& sourceDigest() const noexcept;
        const std::vector<TileId>& tileIds() const noexcept; // key order
        bool contains(const TileId& id) const noexcept;
        LodError readCluster(
            const TileId& id, ObjectCluster& cluster, EdenArchive::DecodeBudget* budget = nullptr) const;
        const EdenArchive::Archive& archive() const noexcept;

    private:
        struct Impl;
        explicit ClusterPack(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
