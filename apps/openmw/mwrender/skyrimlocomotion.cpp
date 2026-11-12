#include "skyrimlocomotion.hpp"

namespace MWRender::SkyrimLocomotion
{
    Controller::Controller(SkyrimIdle::ClipFuture, SkyrimIdle::ClipFuture, SkyrimIdle::ClipFuture, BoneMap,
        float idlePhaseFraction, HavokPack::Gait::Params params)
        : mParams(params)
        , mIdlePhaseFraction(idlePhaseFraction)
    {
        hints.autoPlay = true;
    }

    void Controller::poll(Slot&) const {}

    void Controller::run(vsg::Object&, float) const {}
}
