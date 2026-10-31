#ifndef VSGOPENMW_MWRENDER_NPC_H
#define VSGOPENMW_MWRENDER_NPC_H

#include <array>
#include <map>
#include <optional>
#include <set>

#include <vsg/maths/vec4.h>

#include <components/animation/update.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/wielding.hpp>

#include "../mwworld/ptr.hpp"

namespace vsg
{
    class Switch;
}
namespace ESM
{
    struct Light;
    struct NPC;
}
namespace MWAnim
{
    class Face;
    struct Context;
}
namespace MWRender
{

    class Npc : public MWAnim::Wielding
    {
        MWWorld::Ptr mPtr;

    public:
        enum class ViewMode
        {
            Normal,
            FirstPerson,
            HeadOnly
        };
        Npc(const MWAnim::Context& mwctx, const MWWorld::Ptr& ptr, ViewMode viewMode = ViewMode::Normal);
        ~Npc();

        MWWorld::Ptr getPtr() { return mPtr; }

        void setFacialState(bool enable, bool talking, float loudness);

        void update(float dt) override;

        void show(Wield w, bool visible) override;
        bool hasSheathedShield() const override;
        bool canAnimateShield() const override;

        void updateParts();
        void updateEquipment() override;

        /// Rebuilds the NPC, updating their root model, animation sources, and equipment.
        void rebuild();

        void compile();

        /// Get the inventory slot that the given node path leads into, or -1 if not found.
        int getSlot(const std::vector<const vsg::Node*>& path) const;

        void setVampire(bool vampire);

        void updatePtr(const MWWorld::Ptr& updated) /*override*/;

        std::set<ESM::RefId> soundsToPlay;

    protected:
        std::vector<vsg::Node*> mParts[ESM::PRT_Count];
        const MWAnim::Context* mPartContext{};

        const ESM::NPC* mNpc;
        std::string mHeadModel;
        std::string mHairModel;
        ViewMode mViewMode;

        enum class NpcType
        {
            Normal,
            Werewolf,
            Vampire
        } mNpcType;

        int mPartslots[ESM::PRT_Count]; // Each part slot is taken by clothing, armor, or is empty
        int mPartPriorities[ESM::PRT_Count];

        std::unique_ptr<MWAnim::Face> mFace;

        void updateNpcBase();

        NpcType getNpcType() const;

        void insertBoundedPart(ESM::PartReferenceType type, const std::string& model, const std::string& bonename,
            std::optional<vsg::vec4> glowColor, const ESM::Light* light);

        void removeIndividualPart(ESM::PartReferenceType type);
        void reserveIndividualPart(ESM::PartReferenceType type, int group, int priority);

        bool addOrReplaceIndividualPart(ESM::PartReferenceType type, int group, int priority, const std::string& mesh,
            std::optional<vsg::vec4> glowColor = {}, const ESM::Light* light = {});
        void removePartGroup(int group);
        void addPartGroup(int group, int priority, const std::vector<ESM::PartReference>& parts,
            std::optional<vsg::vec4> glowColor = {});
        void addWeapon();
        void addCarriedLeft();
        void addAmmo();

        // Sheathing ([Game] weapon sheathing, shield sheathing; off by default). Needs bones a modded skeleton adds.
        void addCustomBones(const std::string& model);
        void removePath(std::vector<vsg::Node*>& path);
        void updateSheathing();
        void updateCarriedLeftVisible();
        void updateHolsteredWeapon();
        void updateHolsteredShield();
        void updateQuiver();
        std::string getSheathedShieldMesh(const MWWorld::ConstPtr& shield) const;
        std::vector<vsg::Node*> attachHolstered(vsg::ref_ptr<vsg::Node> node, MWAnim::PlaceholderResult& result,
            const std::string& bone, std::optional<vsg::vec4> glowColor, bool animate);
        vsg::ref_ptr<vsg::Node> readHolstered(const std::string& model) const;
        std::vector<vsg::Node*> mScabbard;
        std::vector<vsg::Node*> mHolsteredShield;
        std::vector<std::vector<vsg::Node*>> mQuiver;
        vsg::ref_ptr<vsg::Switch> mHolsteredWeaponSwitch;
        vsg::ref_ptr<vsg::Switch> mHolsteredShieldSwitch;
        bool mQuiverDirty = false;

        static NpcType getNpcType(const MWWorld::Ptr& ptr);
        void clearControllers();
    };

}

#endif
