#ifndef VSGOPENMW_MWRENDER_SCENE_H
#define VSGOPENMW_MWRENDER_SCENE_H

#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <string>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <components/mwanimation/context.hpp>
#include <components/mwanimation/objectanim.hpp>

#include "playerclipmeta.hpp"
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
    namespace SkyrimRagdoll
    {
        class Controller;
    }

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
        /// Task 0171 stage S5-3b: clip metas for swings with no actor (the player's), by clip name; main thread only.
        PlayerClipMetaCache mPlayerClipMeta;

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
        /// Task 0232: the object of a Skyrim door or chest whose model has an Open sequence (null when the model has
        /// none or cannot be read: the caller then builds it as before).
        std::unique_ptr<MWAnim::Object> makeNeutralSequenceObject(
            const MWWorld::Ptr& ptr, const std::string& mesh, MWAnim::Context& ctx);

    private:
        SkyrimRagdoll::Controller* findNeutralRagdoll(const MWWorld::Ptr& ptr);
        std::unique_ptr<MWAnim::Object> giveSceneryKeyframes(std::unique_ptr<MWAnim::Object> plain,
            const MWWorld::Ptr& ptr, const std::string& mesh, const ESM::Light* light, MWAnim::Context& ctx);

    public:
        void insertNPC(const MWWorld::Ptr& ptr) override;
        void insertNeutralActor(
            const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance) override;
        /// Task 0222: the drawn object of a neutral actor (nodes, pose and gait controllers, held gear), made but not
        /// placed or compiled; null when it cannot be drawn. `player` is the player's own body (see scene.cpp).
        std::unique_ptr<MWAnim::ObjectAnimation> makeNeutralActorObject(const MWWorld::Ptr& ptr,
            const openeden::sdk::game::ActorAppearance& appearance, bool player);
        /// Task 0245: the player's first-person arms: the first-person skeleton and parts of `appearance`, the arms'
        /// clip controller (a SkyrimFirstPerson::ArmsController, in keepAlive) and the held gear. Made but not placed or
        /// compiled; null when it cannot be drawn.
        std::unique_ptr<MWAnim::ObjectAnimation> makeFirstPersonArms(
            const MWWorld::Ptr& ptr, const openeden::sdk::game::ActorAppearance& appearance);
        /// The gait speed and a combat clip of an object made by makeNeutralActorObject (what the Ptr versions do).
        static void setNeutralObjectSpeed(MWAnim::Object& object, float unitsPerSecond);
        static void playNeutralObjectClip(MWAnim::Object& object, std::string_view clip, bool perSex);
        /// Task 0251 (riding): loops `clip` on a neutral object (its SkyrimPose controller, as a seated actor does); an
        /// empty name gives the idle and the gait back.
        static void setNeutralObjectPose(MWAnim::Object& object, std::string_view clip, bool perSex);
        /// Task 0267: plays one side of a paired clip ("~1/<path>") once on a neutral object and holds its last pose;
        /// an empty name gives the idle and the gait back. Unlike playNeutralObjectClip it does not draw the weapon.
        static void playNeutralObjectPaired(MWAnim::Object& object, std::string_view clip);
        /// Task 0255 (riding): the drawn neutral object of the placement with this id (empty when none is drawn). Walks
        /// every cell: call it when the id changes, not every frame.
        MWWorld::Ptr findNeutralObject(std::uint32_t placement);
        /// Task 0255: the nodes from the root of a drawn neutral object's skeleton down to the bone `bone` (the object's
        /// own transform left out), so that `vsgUtil::computePosition` of them is the bone's place in the object's frame.
        /// Empty when the object or the bone is not there. The nodes live as long as the object: ask again when
        /// `changeCounter()` has moved.
        std::vector<const Anim::Transform*> neutralObjectBonePath(const MWWorld::Ptr& ptr, const std::string& bone);
        void setNeutralMouth(const MWWorld::Ptr& ptr, const std::array<float, 16>& visemes) override;
        void setNeutralActorSpeed(const MWWorld::Ptr& ptr, float unitsPerSecond) override;
        void setNeutralActorPose(const MWWorld::Ptr& ptr, std::string_view clip, bool perSex) override;
        void playNeutralActorClip(const MWWorld::Ptr& ptr, std::string_view clip, bool perSex) override;
        void setNeutralActorClipSpeed(const MWWorld::Ptr& ptr, float multiplier) override;
        HavokPack::ClipProgress neutralActorClipProgress(const MWWorld::Ptr& ptr) override;
        HavokPack::ClipProgress neutralActorClipProgressWaiting(const MWWorld::Ptr& ptr, int waitMilliseconds) override;
        std::shared_ptr<const HavokPack::ClipMeta> neutralClipMeta(std::string_view clip, bool perSex) override;
        openeden::sdk::game::PairedStatus neutralPairedClipInfo(
            std::string_view clip, openeden::sdk::game::PairedClipInfo& out) override;
        bool neutralRagdollStartPose(const MWWorld::Ptr& ptr, MWPhysics::ActorStartPose& out) override;
        bool beginNeutralRagdoll(const MWWorld::Ptr& ptr, std::span<const std::string> names,
            std::span<const MWPhysics::RagdollBoneState> initial, std::string& reason) override;
        void setNeutralRagdollStates(
            const MWWorld::Ptr& ptr, std::span<const MWPhysics::RagdollBoneState> states) override;
        void endNeutralRagdoll(const MWWorld::Ptr& ptr) override;
        bool beginNeutralRagdollFrozen(const MWWorld::Ptr& ptr, std::span<const std::string> names,
            std::span<const MWPhysics::RagdollBoneState> states, std::string& reason) override;
        bool neutralRagdollDrivenPose(const MWWorld::Ptr& ptr, std::vector<std::string>& names,
            std::vector<MWPhysics::RagdollBoneState>& states) override;
        bool neutralObjectHasSequence(const MWWorld::Ptr& ptr) override;
        void setNeutralObjectOpen(const MWWorld::Ptr& ptr, float progress, bool closing) override;
        void setNeutralObjectTarget(const MWWorld::Ptr& ptr, bool open) override;
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
