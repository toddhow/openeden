#ifndef OPENMW_MWRENDER_SKYRIMLIP_HPP
#define OPENMW_MWRENDER_SKYRIMLIP_HPP

// Task 0146 (slice 2, `[Game] neutral lip sync`): a Skyrim NPC's face head moves its mouth. The facegen head of an NPC
// is a BSDynamicTriShape whose vertex count equals the race head's `.tri` morph file (996 for the female and 898 for the
// male Nord-like heads in the data, docs/tasks/0146-skyrim-lip-sync.md 2.2). HeadMorph keeps the head's own vertices as
// the base and writes base + weighted morph deltas into the shape's vertex array, uploaded (VSG dirty) only when the
// weights change. The other shapes of the head (teeth, tongue, eyes, brows) are not touched in this slice.
//
// Threads: main thread (the array is also read by the render thread's transfer; VSG's dirty/transfer protocol covers it).
// No allocation after construction.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <vsg/core/Array.h>
#include <vsg/core/Inherit.h>
#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>

#include <components/trimorph/mouth.hpp>
#include <components/trimorph/trimorph.hpp>
#include <components/vfs/manager.hpp>

namespace MWRender::SkyrimLip
{
    /// The morphs the drive moves; nothing else is read from the `.tri` file.
    inline constexpr std::string_view sAah = "Aah";
    inline constexpr std::string_view sBigAah = "BigAah";

    /// The race head files of Skyrim SE (provenance: the listing of Skyrim - Meshes0.bsa, task 0146). The one whose
    /// vertex count equals the face shape's is the NPC's; the count is the check, the names are the candidates.
    inline constexpr std::array<std::string_view, 8> sHeadTriFiles{
        "meshes/actors/character/character assets/femalehead.tri",
        "meshes/actors/character/character assets/malehead.tri",
        "meshes/actors/character/character assets/femaleheadargonian.tri",
        "meshes/actors/character/character assets/maleheadargonian.tri",
        "meshes/actors/character/character assets/femaleheadkhajiit.tri",
        "meshes/actors/character/character assets/maleheadkhajiit.tri",
        "meshes/actors/character/character assets/childhead.tri",
        "meshes/actors/character/character assets/femaleheadwerewolf.tri",
    };

    class HeadMorph : public vsg::Inherit<vsg::Object, HeadMorph>
    {
    public:
        /// Null when the file has the wrong number of vertices, has neither morph, or `positions` is null. Marks the
        /// array DYNAMIC_DATA, so it must be called before the array is compiled.
        static vsg::ref_ptr<HeadMorph> make(
            std::shared_ptr<const TriMorph::File> file, vsg::ref_ptr<vsg::vec3Array> positions);

        /// Writes the vertices for these weights. Returns true when it did (the weights differ from the last ones
        /// written); false when nothing changed. A closed mouth (both zero) is the base vertices exactly.
        bool setWeights(const TriMorph::MouthWeights& weights);

        [[nodiscard]] const TriMorph::MouthWeights& weights() const { return mLast; }
        /// How many times the vertices were written (for tests and the log).
        [[nodiscard]] std::uint64_t uploads() const { return mUploads; }

        HeadMorph(std::shared_ptr<const TriMorph::File> file, vsg::ref_ptr<vsg::vec3Array> positions);

    private:
        std::shared_ptr<const TriMorph::File> mFile;
        vsg::ref_ptr<vsg::vec3Array> mPositions;
        std::unique_ptr<std::array<float, 3>[]> mBase;
        std::uint32_t mAah = ~0u;
        std::uint32_t mBigAah = ~0u;
        TriMorph::MouthWeights mLast;
        std::uint64_t mUploads = 0;
    };

    /// The head file whose vertex count is `vertexCount`, read once and kept (shared by every actor; only the Aah and
    /// BigAah morphs are kept). Null when no candidate matches or none can be read; the reason is logged once per file.
    /// Thread-safe; main thread in practice.
    [[nodiscard]] std::shared_ptr<const TriMorph::File> headFileFor(const VFS::Manager& vfs, std::uint32_t vertexCount);
}

#endif
