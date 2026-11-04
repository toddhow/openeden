#ifndef OPENMW_MWRENDER_MERGEDBODY_H
#define OPENMW_MWRENDER_MERGEDBODY_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage d): the nodes of a cached merged body.
//
// The cache is the one `openmw --bake-bodies` publishes in <user data>/openeden/bodies/
// (components/body/bodycache.hpp). It is read once per session, on the first actor that asks. A race and sex is checked
// once, when the first actor of it is built: its key is computed again from the data now, exactly as the bake computes
// it (OMW::computeStockBodyFacts: the part files and the skeleton are read and hashed, a few milliseconds), and must
// equal the manifest's; the payload must read; and the nodes must build. Any failure keeps the stock parts for that
// race and sex for the whole session (decision H15: no live swap; a load-order change makes the key stale and the
// message says to re-run the bake).
//
// The look of each submesh is the stock part's own: the part of the submesh's slot is read through the engine's reader
// with the same options the stock part gets, while a pipeline override records the pipeline options of every state
// group it creates; the state group whose material key (base texture and alpha mode, OMW::BodyCommon::materialKey)
// matches the submesh's gives the pipeline options and the descriptors (material, textures, PBR values, skin mark). To
// those the merged body adds skinning: the SKIN mode, its bone matrices and an Anim::Skin per draw, bound to the
// actor's bones by name (the engine's case-insensitive lookup) with the inverse of each bone's rest matrix, the same
// rest pose the bake placed the parts with. Vertex colours, morphs and normal maps (which need tangents) are not
// carried: Morrowind's stock body parts have none of them (replacer textures with normal maps: verify, stage e).
//
// The template node is built once per race and sex and shared; each actor takes its own copy of the controllers and
// bone matrices through MWAnim::decorate, as a stock part's node is.
//
// Threads: main thread only (the reader, the pipeline cache and the template map); a mutex guards the map anyway.

#include <filesystem>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>

#include <components/body/mergedruntime.hpp>

namespace vsg
{
    class Node;
}
namespace ESM
{
    struct Race;
}
namespace MWAnim
{
    struct Context;
}

namespace MWRender::MergedBodies
{
    /// The user data folder the cache lives under (<folder>/openeden/bodies). Set once by the engine at start.
    void setUserDataFolder(const std::filesystem::path& folder);

    struct TemplateResult
    {
        vsg::ref_ptr<vsg::Node> mNode; // null when the stock parts must be drawn
        std::vector<int> mSlots; // the part slots the merged body draws (ascending)
        Body::MergedBodyReason mReason = Body::MergedBodyReason::BuildFailed;
    };

    /// The shared template of the merged body of (race, sex), built on first use. Main thread.
    TemplateResult getTemplate(const ESM::Race& race, bool female, const MWAnim::Context& context);

    /// One log line per (race, sex, reason) and, for a cache problem, one message box per session.
    void report(const ESM::Race& race, bool female, Body::MergedBodyReason reason);
}

#endif
