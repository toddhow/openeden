#ifndef OPENMW_COMPONENTS_PUDDLE_PUDDLEPARTIAL_H
#define OPENMW_COMPONENTS_PUDDLE_PUDDLEPARTIAL_H

// OpenEden puddles (task 0095, stage W-b). The work folder of a bake: every baked cell is written there as its own
// file as soon as it is done, so a bake that is killed can be started again and picks up the cells that are still
// valid (same parameter digest, same input digest). The finished pack is built from the cells, whether they were baked
// now or taken from here, so a resumed bake gives the same pack as an uninterrupted one.

#include <filesystem>
#include <optional>

#include "puddlemap.hpp"

namespace Puddle
{
    class PartialStore
    {
    public:
        explicit PartialStore(std::filesystem::path folder);

        /// The stored cell if it exists, parses, is the asked cell and carries exactly these digests.
        std::optional<CellMap> load(
            std::int32_t cellX, std::int32_t cellY, const Digest& parameters, const Digest& input) const;
        /// Writes the cell under a temporary name and renames it.
        Error store(const CellMap& cell) const;
        /// Removes the folder.
        void clear() const;
        const std::filesystem::path& folder() const { return mFolder; }

    private:
        std::filesystem::path pathOf(std::int32_t cellX, std::int32_t cellY) const;
        std::filesystem::path mFolder;
    };
}

#endif
