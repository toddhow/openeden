#ifndef OPENMW_MWRENDER_MERGEDBODY_H
#define OPENMW_MWRENDER_MERGEDBODY_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage d): the nodes of a cached merged body.
//
// The cache is the one `openmw --bake-bodies` publishes in <user data>/openeden/bodies/
// (components/body/bodycache.hpp). It is read once per session, on the first actor that asks. A race and sex is checked
// once, when the first actor of it is built: its quick key (OMW::computeStockBodyQuickFacts: the part records, the
// part files' sizes and the skeleton file's size; no part file is read or hashed, answer 8) must equal the one the
// bake wrote to quick.txt; the payload must read; and the nodes must build. Any failure keeps the stock parts for that
// race and sex for the whole session (decision H15: no live swap; a load-order change makes the key stale and the
// message says to re-run the bake).
//
// The look of each draw is the stock part's own: the part of the draw's first slot is read through the engine's
// reader with the same options the stock part gets, while a pipeline override records the pipeline options of every
// state group it creates; the state group whose material key (base texture and alpha mode, OMW::BodyCommon::
// materialKey) matches gives the pipeline options and the descriptors (material, textures, PBR values, skin mark).
// The reader adds the MATERIAL mode after the override runs (nif.cpp createStateGroup), so the look adds it when the
// group has a material descriptor; the merged pipeline's defines are then compared with the stock pipeline's and a
// mismatch keeps the stock parts. The textures are shared with the stock parts' (the stock options' SharedObjects).
// To those the merged body adds skinning: the SKIN mode, its bone matrices and an Anim::Skin per draw (one draw per
// material key, decision H19), bound to the actor's bones by name (the engine's case-insensitive lookup) with the
// inverse of each bone's rest matrix, the same rest pose the bake placed the parts with. Vertex colours, morphs and
// normal maps (which need tangents) are not carried: Morrowind's stock body parts have none of them. An alpha-blended
// look is refused (it would need depth sorting; none of the stock parts is blended).
//
// The template node is built once per (race, sex, read options) and shared; each actor takes its own copy of the
// controllers and bone matrices through MWAnim::decorate, as a stock part's node is.
//
// Lifetime: everything lives in a Session the Engine owns (vsgengine.hpp), destroyed before the render engine and its
// device (review finding 3: no Vulkan objects in namespace-scope statics). Without a session nothing is built.
//
// Threads: main thread only (the reader, the pipeline cache and the template map); a mutex guards the state anyway.

#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <vsg/core/ref_ptr.h>

#include <components/body/mergedruntime.hpp>

namespace vsg
{
    class GraphicsPipeline;
    class Node;
    class Object;
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
    struct State;

    /// Task 0072 (review of stage f, finding 1): the requested defines each stage of `pipeline` is compiled with
    /// (vsgUtil::glslDefinesOf: the GLSL module's, or those kept beside the Slang twin that replaced it), keyed by the
    /// stage's VkShaderStageFlagBits; a stage without them is left out.
    std::map<int, std::set<std::string>> pipelineDefines(const vsg::GraphicsPipeline& pipeline);

    /// Empty when, stage by stage, `merged` has every define of `stock` and no other, except the defines the merged
    /// look changes on purpose (vertex inputs, SKIN, GBUFFER_MOTION); else the reason. A stage that only one side has
    /// defines for is a reason too: the check fails closed.
    std::string compareDefines(
        const std::map<int, std::set<std::string>>& stock, const std::map<int, std::set<std::string>>& merged);

    /// All the merged bodies keep for a session: the cache, the templates (GPU objects) and what was reported. One at a
    /// time; the Engine owns it from start until before the render engine goes.
    class Session
    {
    public:
        explicit Session(const std::filesystem::path& userDataFolder);
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

    private:
        std::unique_ptr<State> mState;
    };

    /// The bake, check and analysis runs draw stock bodies whatever the user's setting says (review finding 13).
    void disableForThisRun();

    /// The setting is on, a session exists and the run is not a bake, check or analysis.
    bool enabled();

    struct TemplateResult
    {
        vsg::ref_ptr<vsg::Node> mNode; // null when the stock parts must be drawn
        std::vector<int> mSlots; // the part slots the merged body draws (ascending)
        Body::MergedBodyReason mReason = Body::MergedBodyReason::BuildFailed;
        /// Task 0069 stage e: the slot runs of each draw, in the order of the node's children (one CullNode each).
        std::vector<std::vector<Body::SlotRun>> mDrawRuns;
    };

    /// The shared template of the merged body of (race, sex) read with `context`'s options, built on first use. Main
    /// thread.
    TemplateResult getTemplate(const ESM::Race& race, bool female, const MWAnim::Context& context);

    /// What applyHiddenRegions found and changed in an actor's copy.
    struct RegionChange
    {
        /// Per draw (the root's children, in order): whether any of it is drawn. A draw that is not needs no skin.
        std::vector<bool> mDrawn;
        /// Per draw: the copy's bone matrices (the target its Anim::Skin writes), to tell the copy's skins apart.
        std::vector<const vsg::Object*> mBoneMatrices;
        /// The draws taken out of the copy: the caller detaches them (CompileContext::detach), as a removed part.
        std::vector<vsg::ref_ptr<vsg::Node>> mRemoved;
    };

    /// Task 0069 stage e: leaves the regions of `hiddenSlots` (ascending) out of an actor's copy of a template: each
    /// draw draws the index ranges that remain, as draws that share the template's vertex and index buffers (the
    /// template's own draw when nothing of it is hidden), or nothing. It can be called again on a copy it changed
    /// before (review finding 2: an equipment change keeps the copy); a draw already as wanted is left as it is.
    /// `actorNode` must be the actor's copy made by MWAnim::decorate, whose state groups are its own (the bone matrices
    /// made them so); a copy that still shares a state group with `templateNode`, or a structure that is not the
    /// template's, is left untouched and false is returned: the caller then keeps the stock parts. Main thread, in
    /// updateParts; nothing per frame.
    bool applyHiddenRegions(vsg::Node& actorNode, const vsg::Node& templateNode,
        const std::vector<std::vector<Body::SlotRun>>& drawRuns, const std::vector<int>& hiddenSlots,
        RegionChange& change);

    /// One log line per (race, sex, reason) and, for a cache problem, one message box per session.
    void report(const ESM::Race& race, bool female, Body::MergedBodyReason reason);

    /// Review finding 4: one log line per (race, sex) whose regions could not be left out of an actor's copy.
    void reportRegionsRefused(const ESM::Race& race, bool female);

    /// One log line per race id the store does not know.
    void reportMissingRace(const std::string& raceId);
}

#endif
