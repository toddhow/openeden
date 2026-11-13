#ifndef OPENEDEN_MWRENDER_GROUNDCOVERBUILD_H
#define OPENEDEN_MWRENDER_GROUNDCOVERBUILD_H

#include <vsg/maths/vec3.h>
#include <vsg/nodes/Node.h>

#include <components/mwanimation/context.hpp>

#include "groundcoverinstances.hpp"

namespace MWRender
{
    /**
     * @brief The context groundcover reads its models with (task 0012, V4f; task 0153 moved it here so that the neutral
     * grass shares it): a copy of `base` whose model options carry a pipeline override (the INSTANCE mode, alpha test
     * at 128/255, no blending) and a cache of their own. Main thread.
     */
    MWAnim::Context makeGroundcoverContext(const MWAnim::Context& base);

    /**
     * @brief Builds one chunk from instances whose positions are world positions: each model is read through
     * `context` (which must come from makeGroundcoverContext), instanced with its instance arrays relative to
     * `origin`, and the whole is culled as one. Null when no model could be read. Not compiled. Runs on any thread
     * that may read models (Groundcover's operation thread).
     */
    vsg::ref_ptr<vsg::Node> buildGroundcoverChunk(
        const MWAnim::Context& context, const GroundcoverInstances& instances, const vsg::vec3& origin);
}

#endif
