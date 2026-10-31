#ifndef OPENCS_VIEW_OBJECTMARKER_PENDING_H
#define OPENCS_VIEW_OBJECTMARKER_PENDING_H

#include <string>

namespace CSVRender
{
    /// The calls cells and worldspace widgets make on the selection marker, doing nothing, until stage C7 ports
    /// objectmarker.* and this file goes.
    class ObjectMarker
    {
    public:
        void addToSelectionHistory(const std::string&, bool) {}
        void updateSelectionMarker(const std::string& = {}) {}
        void detachMarker() {}
        void setSubMode(int) {}
        void toggleVisibility() {}
        void updateScale(double) {}
    };
}

#endif
