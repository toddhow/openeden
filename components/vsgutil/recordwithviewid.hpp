#ifndef OPENEDEN_VSGUTIL_RECORDWITHVIEWID_H
#define OPENEDEN_VSGUTIL_RECORDWITHVIEWID_H

#include <vsg/commands/Commands.h>
#include <vsg/vk/CommandBuffer.h>

namespace vsgUtil
{
    /*
     * Records commands under a view ID of their own. A pass that has no view of its own (the GUI pass, the tonemap
     * pass) records into a command buffer that holds whatever view recorded last, and a pipeline only exists for the
     * view ID it was compiled for.
     */
    class RecordWithViewId : public vsg::Inherit<vsg::Command, RecordWithViewId>
    {
    public:
        RecordWithViewId(vsg::ref_ptr<vsg::Commands> inCommands, uint32_t inViewId)
            : commands(std::move(inCommands))
            , viewId(inViewId)
        {
        }
        vsg::ref_ptr<vsg::Commands> commands;
        uint32_t viewId;
        void record(vsg::CommandBuffer& commandBuffer) const override
        {
            const uint32_t previous = commandBuffer.viewID;
            commandBuffer.viewID = viewId;
            commands->record(commandBuffer);
            commandBuffer.viewID = previous;
        }
    };
}

#endif
