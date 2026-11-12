#ifndef VSGOPENMW_MWRENDER_WATER_H
#define VSGOPENMW_MWRENDER_WATER_H

#include <cstdint>
#include <vector>

#include <vsg/commands/Draw.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/Switch.h>

#include <components/misc/vsgmath.hpp>
#include <components/render/watercells.hpp>
#include <components/vsgutil/composite.hpp>

namespace Resource
{
    class ResourceSystem;
}
namespace Anim
{
    class Transform;
}
namespace MWRender
{
    class Water : public vsgUtil::Composite<vsg::Node>
    {
        vsg::ref_ptr<Anim::Transform> mTransform;
        // Task 0129: the infinite plane is one child of a switch, and the cell-bounded surfaces hang under a state group of
        // their own; Morrowind uses the first, a neutral exterior of a game pack the second.
        vsg::ref_ptr<vsg::Switch> mGlobal;
        vsg::ref_ptr<vsg::StateGroup> mCellGroup;
        vsg::ref_ptr<vsg::Draw> mCellDraw;
        Render::WaterCells mCells;
        struct CellNode
        {
            std::int32_t mX;
            std::int32_t mY;
            vsg::ref_ptr<Anim::Transform> mTransform;
        };
        std::vector<CellNode> mCellNodes;

    public:
        Water(Resource::ResourceSystem* resourceSystem);
        ~Water();

        void setHeight(float height);
        /// With cell surfaces in play the infinite plane is hidden (and shown again by setGlobalVisible(true)).
        void setGlobalVisible(bool visible);
        bool globalVisible() const { return mGlobalVisible; }
        bool isUnderwater(const vsg::vec3& pos) const;

        /// Task 0129: a surface of `size` units over the grid square (x, y) at `height`; replaces the one there.
        void setCellWater(std::int32_t x, std::int32_t y, float size, float height);
        void removeCellWater(std::int32_t x, std::int32_t y);
        std::size_t cellWaterCount() const { return mCells.size(); }

        /*
        /// adds an emitter, position will be tracked automatically using its scene node
        void addEmitter (const MWWorld::Ptr& ptr, float scale = 1.f, float force = 1.f);
        void removeEmitter (const MWWorld::Ptr& ptr);
        void updateEmitterPtr (const MWWorld::Ptr& old, const MWWorld::Ptr& ptr);
        void emitRipple(const vsg::vec3& pos);
        void removeCell(const MWWorld::CellStore* store); ///< remove all emitters in this cell
        void clearRipples();
        */
    private:
        bool mGlobalVisible = true;
    };
}

#endif
