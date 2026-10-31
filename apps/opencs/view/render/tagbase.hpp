#ifndef OPENCS_VIEW_TAGBASE_H
#define OPENCS_VIEW_TAGBASE_H

#include <vsg/core/Object.h>

#include <QString>

#include "mask.hpp"

namespace CSVRender
{
    struct WorldspaceHitResult;

    class TagBase : public vsg::Object
    {
        Mask mMask;

    public:
        explicit TagBase(Mask mask)
            : mMask(mask)
        {
        }

        Mask getMask() const { return mMask; }

        virtual QString getToolTip(bool hideBasics, const WorldspaceHitResult& hit) const { return {}; }
    };
}

#endif
