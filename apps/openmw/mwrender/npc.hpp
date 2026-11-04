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

#include "mergedbody.hpp"
#include "partcontrollers.hpp"
#include "partslots.hpp"

namespace vsg
{
    class Switch;
}
namespace ESM
{
    struct Light;
    struct NPC;
}
namespace Anim
{
    class Skin;
}
namespace MWAnim
{
    class Face;
    struct Context;
}
namespace MWRender
{

    // Task 0069 stage e review: the part slot bookkeeping is PartSlots; Npc does its node work (PartSlots::Nodes).
    class Npc : public MWAnim::Wielding, private PartSlots::Nodes
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

        // The skeleton and the animation files updateNpcBase reads for an NPC (task 0060). A free-standing function so
        // that the cell preloader asks for exactly the same names: the animation cache is keyed by the name as written.
        struct AnimationSources
        {
            std::string defaultSkeleton;
            std::string skeleton;
            std::vector<std::string> sources;
        };
        static AnimationSources animationSources(const MWAnim::Context& context, bool firstPerson, bool female,
            bool beast, bool werewolf, const std::string& customModel, const ESM::RefId& race);

        // The head, hair and race body part models updateNpcBase reads for a plain NPC in normal view (task 0060),
        // with the part type, since each type has its own read options and so its own cache. The cell preloader reads
        // the same ones. Equipment is not included: it needs the inventory. Main thread: it marks skin models, as the
        // build does before it reads them.
        struct PartModel
        {
            ESM::PartReferenceType mType;
            std::string mModel;
        };
        static std::vector<PartModel> defaultPartModels(const ESM::NPC& npc);

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

        // Which inventory group holds each part slot, and at what priority (task 0069 stage e review: the bookkeeping
        // of removeIndividualPart, reserveIndividualPart, the old removePartGroup and addOrReplaceIndividualPart moved
        // there).
        PartSlots mSlots;

        std::unique_ptr<MWAnim::Face> mFace;

        void updateNpcBase();

        NpcType getNpcType() const;

        void insertBoundedPart(ESM::PartReferenceType type, const std::string& model, const std::string& bonename,
            std::optional<vsg::vec4> glowColor, const ESM::Light* light);

        void removeIndividualPart(ESM::PartReferenceType type);
        void reserveIndividualPart(ESM::PartReferenceType type, int group, int priority);

        bool addOrReplaceIndividualPart(ESM::PartReferenceType type, int group, int priority, const std::string& mesh,
            std::optional<vsg::vec4> glowColor = {}, const ESM::Light* light = {});
        // The node of a part that took its slot (the second half of addOrReplaceIndividualPart): read, attach, sound.
        void attachPart(ESM::PartReferenceType type, int group, const std::string& mesh,
            std::optional<vsg::vec4> glowColor, const ESM::Light* light);
        void detachPart(ESM::PartReferenceType type) override;
        // Marks a worn skin part's model just before it asks for its slot (task 0027; stage e second review, finding
        // 10: where the code before PartSlots marked it).
        void beforeWornPart(ESM::PartReferenceType type, const ESM::BodyPart& bodyPart) override;
        // A worn item's parts as the equipment pass sees them (getBodyPart for this sex and view), appended to `out`.
        void wornParts(const std::vector<ESM::PartReference>& parts, std::vector<PartSlots::WornPart>& out) const;
        // The equipment pass's list of one item's parts, kept between updates so that an update allocates none
        // (second review, finding 11).
        std::vector<PartSlots::WornPart> mWornParts;
        // Second review, finding 2: OPENEDEN_PARTSLOTS_TRACE (npc.cpp), decided once in the constructor: how this
        // actor's lines name it (record id, reference number, view), empty when not traced.
        std::string mPartSlotsTraced;
        void tracePartSlots() const;
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
        // Runs the node's auto-play controllers in `clock`, none without one (a holstered bow keeps still).
        std::vector<vsg::Node*> attachHolstered(vsg::ref_ptr<vsg::Node> node, MWAnim::PlaceholderResult& result,
            const std::string& bone, std::optional<vsg::vec4> glowColor, Anim::AutoPlay* clock);
        vsg::ref_ptr<vsg::Node> readHolstered(const std::string& model) const;
        // Empties the clock of a holstered item, then removes its path (task 0074b).
        void removeHolstered(std::vector<vsg::Node*>& path, Anim::AutoPlay& clock);
        std::vector<vsg::Node*> mScabbard;
        std::vector<vsg::Node*> mHolsteredShield;
        // Task 0074b: the holstered weapon's and shield's auto-play clocks, valid while their paths hold the nodes.
        Anim::AutoPlay mScabbardClock;
        Anim::AutoPlay mHolsteredShieldClock;
        std::vector<std::vector<vsg::Node*>> mQuiver;
        vsg::ref_ptr<vsg::Switch> mHolsteredWeaponSwitch;
        vsg::ref_ptr<vsg::Switch> mHolsteredShieldSwitch;
        bool mQuiverDirty = false;

        static NpcType getNpcType(const MWWorld::Ptr& ptr);
        // Empties the lists updateParts fills again (Wielding::clearPartControllers and the face), not the object's
        // own auto-play groups.
        void clearControllers();
        // The node work of updateParts (equipment, head and hair, weapon, sheathing, merged body, body parts).
        void updatePartNodes();
        // Task 0074: the controllers each part linked, so updateParts links again the parts it kept (clearControllers
        // empties the lists), and (task 0074b) each part's own auto-play clock. relinkKeptControllers runs at the end
        // of every updateParts, a throw included (PartControllers::runUpdate).
        PartControllers<ESM::PRT_Count> mPartControllers;
        void relinkSurvivingParts();
        void relinkKeptControllers();
        // Task 0074b: OPENEDEN_AUTOPLAY_TRACE (autoplaytrace.hpp), decided once in the constructor: the record id of
        // a traced actor, empty for every other.
        std::string mAutoPlayTraced;
        unsigned mAutoPlayFrame = 0;

        // Task 0069 stage d ([Bodies] merged body, off by default): attaches the cached merged body when it may be
        // drawn and removes the stock parts of its slots; false, with the stock parts kept, otherwise. updateParts
        // calls it after the equipment. Stage e review: the actor's copy is kept while its template is the same, and
        // only its regions change; its controllers are added again every time (clearControllers drops them), by
        // relinkKeptControllers at the end of the update (task 0074b).
        bool attachMergedBody(bool isWerewolf);
        // Leaves the hidden regions out of the copy (mMergedNode); on a refusal, the stock parts for good.
        bool applyMergedRegions(const ESM::Race& race, bool female, const MergedBodies::TemplateResult& result,
            const std::vector<int>& hidden);
        // Adds the copy's controllers to mStaticControllers, without the skins of draws nothing of is drawn.
        void relinkMergedBody();
        // Detaches the copy and forgets everything about it (a rebuild, a failed compile, the stock parts).
        void forgetMergedBody();
        std::vector<vsg::Node*> mMergedBody;
        std::vector<int> mMergedSlots;
        // The actor's copy and the template it was made from (both null without a merged body).
        vsg::ref_ptr<vsg::Node> mMergedNode;
        vsg::ref_ptr<vsg::Node> mMergedTemplate;
        std::vector<int> mMergedHidden; // the regions left out of the copy (ascending slots)
        MergedBodies::RegionChange mMergedRegions; // per draw: drawn, and its bone matrices
        // What the copy's link gave; valid while mMergedNode holds the copy (its bone matrices own the skins).
        struct MergedLink
        {
            const Anim::Controller* mController;
            vsg::Object* mTarget;
            Anim::Skin* mSkin; // the same controller when it is a skin, else null
        };
        std::vector<MergedLink> mMergedControllers;
        bool mDefaultSkeleton = false; // the actor uses its race's default skeleton, the one the bake placed parts on
        // At most one swap each way per actor (review finding 8): an actor first built dressed draws the stock parts
        // and may take the merged body once undressed; an actor that has drawn the merged body and then puts
        // equipment over a merged slot keeps the stock parts for the rest of its life, so dressing and undressing
        // never swaps the whole body back and forth.
        bool mMergedRetired = false;
        bool mMergedDrawn = false; // the actor has drawn the merged body (and its log line was written)
    };

}

#endif
