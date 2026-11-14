#ifndef VSGOPENMW_MWRENDER_SCENE_H
#define VSGOPENMW_MWRENDER_SCENE_H

#include <cstdint>
#include <map>
#include <memory>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <components/mwanimation/context.hpp>

#include "renderinginterface.hpp"

namespace MWWorld
{
    class CellStore;
}
namespace ESM
{
    struct Light;
}
namespace MWAnim
{
    class Object;
}
namespace vsgUtil
{
    class UpdateThreads;
}
namespace Render::PointShadow
{
    class MotionTracker;
}
namespace MWRender
{
    /*
     * Manages attached objects.
     */
    class Scene : public RenderingInterface
    {
        void operator=(const Scene&);
        Scene(const Scene&);
        vsg::ref_ptr<vsgUtil::UpdateThreads> mUpdateThreads;
        using Objects = std::map<MWWorld::Ptr, std::unique_ptr<MWAnim::Object>>;
        struct Cell;
        using CellMap = std::map<const MWWorld::CellStore*, Cell>;
        CellMap mObjectCells;
        CellMap mActorCells;
        unsigned int mNightDayMode = 0;
        /// Takes one registration of each of object's NightDaySwitch nodes out of from, adding it to to unless null.
        static void moveNightDaySwitches(MWAnim::Object& object, Cell& from, Cell* to);

        std::uint32_t mChangeCounter = 0;

        vsg::ref_ptr<vsg::Switch> mActorGroup;
        vsg::ref_ptr<vsg::Switch> mObjectGroup;
        Cell& getOrCreateCell(const MWWorld::Ptr& ptr, bool actor);
        Cell* getCell(const MWWorld::Ptr& ptr, bool actor);

    public:
        Scene(size_t updateThreads);
        ~Scene();

        MWAnim::Context context;
        MWAnim::Context noParticlesContext;
        vsg::ref_ptr<vsg::Node> getObjects();
        vsg::ref_ptr<vsg::Node> getActors();

        /// @param animated Attempt to load separate keyframes from a .kf file matching the model file?
        /// @param allowLight If false, no lights will be created, and particles systems will be removed.
        void insertModel(const MWWorld::Ptr& ptr, const std::string& model, bool allowLight = true) override;

    private:
        std::unique_ptr<MWAnim::Object> giveSceneryKeyframes(std::unique_ptr<MWAnim::Object> plain,
            const MWWorld::Ptr& ptr, const std::string& mesh, const ESM::Light* light, MWAnim::Context& ctx);

    public:
        void insertNPC(const MWWorld::Ptr& ptr) override;
        void insertNeutralActor(
            const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance) override;
        void setNeutralMouth(const MWWorld::Ptr& ptr, const std::array<float, 16>& visemes) override;
        void setNeutralActorSpeed(const MWWorld::Ptr& ptr, float unitsPerSecond) override;
        void setNeutralActorPose(const MWWorld::Ptr& ptr, std::string_view clip, bool perSex) override;
        void insertCreature(const MWWorld::Ptr& ptr, const std::string& model, bool weaponsShields) override;

        /// Puts a built and compiled object (an actor when `actor`) into its cell's group and update list: the end of
        /// insertNPC and insertCreature, public so that a test can fill a Scene with objects of its own (task 0074f).
        void addObject(const MWWorld::Ptr& ptr, std::unique_ptr<MWAnim::Object> anim, bool actor);

        MWAnim::Object* getObject(const MWWorld::Ptr& ptr);

        bool removeObject(const MWWorld::Ptr& ptr);
        ///< \return found?

        void removeCell(const MWWorld::CellStore* store);

        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& cur);

        MWWorld::Ptr getPtr(const vsg::Node* node);
        MWWorld::Ptr getPtr(const vsg::Node* node, CellMap& map);
        void update(float dt);

        /// Shows child mode (0 day, 1 night) of the objects' NightDaySwitch nodes, when "day night switches" is on.
        void setNightDayMode(unsigned int mode);
        vsg::ref_ptr<vsgUtil::UpdateThreads> getUpdateThreads();

        /// Goes up whenever an object or actor is added, removed or a cell is removed (point light shadow cache,
        /// task 0077): anything a shadow map may hold has then perhaps changed.
        std::uint32_t changeCounter() const { return mChangeCounter; }

        /// Reports every actor and every door to the tracker, with the sphere that holds it (task 0077). Main
        /// thread, after `update`.
        void collectMoving(Render::PointShadow::MotionTracker& tracker) const;
    };

}
#endif
