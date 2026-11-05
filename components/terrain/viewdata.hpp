#ifndef VSGOPENMW_TERRAIN_VIEWDATA_H
#define VSGOPENMW_TERRAIN_VIEWDATA_H

#include <compare>
#include <vector>

#include <vsg/nodes/Group.h>

#include <vsg/commands/DrawIndexed.h>

#include "bounds.hpp"
#include "chunkfade.hpp"
#include "view.hpp"

namespace Terrain
{
    struct ViewDataEntry
    {
        bool set(const Bounds& in_bounds, float in_distance)
        {
            if (bounds == in_bounds)
                return false;
            else
            {
                bounds = in_bounds;
                distance = in_distance;
                return true;
            }
        }
        Bounds bounds;
        float distance = 0;
    };

    class ViewData : public View
    {
    public:
        ViewData() { mNode = rootNode = vsg::Group::create(); }

        void add(const Bounds& bounds, float distance)
        {
            unsigned int index = numEntries++;

            if (index + 1 > entries.size())
                entries.resize(index + 1);

            ViewDataEntry& entry = entries[index];
            if (entry.set(bounds, distance))
                changed = true;
        }

        void reset()
        {
            // reset index for next frame
            numEntries = 0;
            changed = false;
        }

        Bounds getContainingChunk(const vsg::vec2& point) const
        {
            for (unsigned int i = 0; i < numEntries; ++i)
            {
                if (entries[i].bounds.contains(point))
                    return entries[i].bounds;
            }
            return {};
        }

        void clear()
        {
            numEntries = 0;
            entries.clear();
            rootNode->children.clear();
            fader.clear();
            fadeDraws.clear();
            fadeDirty = false;
            previousCoverage.clear();
        }

        // OpenEden (task 0088): the live terrain's chunk fade. Only the player's view uses it (Paging::updateView with
        // fade parameters); the preload views and the map's never do.
        struct FadeDraw
        {
            Bounds bounds;
            vsg::ref_ptr<vsg::DrawIndexed> draw;
            uint32_t baseInstance = 0;
        };
        ChunkFader fader;
        std::vector<FadeDraw> fadeDraws; // every draw of the last rebuild
        bool fadeDirty = false; // a leaving chunk was dropped: the draws must be rebuilt
        std::vector<Bounds> previousCoverage; // the squares something else drew at the last rebuild

        vsg::dvec3 viewPoint;
        // OpenEden (task 0055): the Coverage version this view was built for (0: none).
        uint64_t coverageVersion = 0;
        float viewDistance = 0;

        /// Indicates at least one chunk of mEntries has changed.
        // @note Such changes may necessitate a revalidation of cached nodes elsewhere depending
        /// on the parameters that affect the creation of node.
        bool changed = false;

        std::vector<ViewDataEntry> entries;
        unsigned int numEntries = 0;

        vsg::ref_ptr<vsg::Group> rootNode;
    };
}

#endif
