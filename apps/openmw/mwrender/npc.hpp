#ifndef VSGOPENMW_MWRENDER_NPC_H
#define VSGOPENMW_MWRENDER_NPC_H

#include <array>
#include <exception>
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

#include "equipmentupdate.hpp"
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

        // The quiver change show() asked for (weapon sheathing), and the compile that follows it. MAIN THREAD ONLY:
        // update() runs on an update worker (task 0074e, finding 3); Scene::update calls this for every actor after
        // the workers are idle (through updateOnMainThread), Player::update for the player and Preview::animate for the
        // inventory doll (task 0074f).
        void updatePendingQuiver();
        // The actor's main-thread step (MWAnim::Object::updateOnMainThread): the pending quiver change.
        void updateOnMainThread() override;

        std::set<ESM::RefId> soundsToPlay;

    protected:
        // One equipment change (task 0074e, finding 5): `nodes` attaches and removes nodes, then the actor is compiled
        // (compileTraced), also when `nodes` throws; the exception is passed on unchanged. Npc::updateEquipment.
        template <class Nodes>
        void runCompiledUpdate(Nodes&& nodes)
        {
            runEquipmentUpdate(nodes, [this] { compileTraced(); });
        }
        void compileTraced();
        // The body of updatePendingQuiver (a test passes its own `quiver`, updateQuiver needs an inventory): if a
        // change is pending, clears it, runs `quiver` and compiles, also when `quiver` throws (runEquipmentUpdate:
        // the nodes it attached before the throw are in the scene). A std::exception is logged, not passed on: this
        // runs on the main thread in the render update, where an exception would end the game session.
        template <class Quiver>
        void runPendingQuiver(Quiver&& quiver)
        {
            if (!mQuiverDirty)
                return;
            mQuiverDirty = false;
            try
            {
                runEquipmentUpdate(quiver, [this] { compile(); });
            }
            catch (const std::exception& error)
            {
                logQuiverError(error);
            }
        }
        void logQuiverError(const std::exception& error) const;

        // Task 0074d (review of 0074b, finding 4): builds no record, skeleton or part, so that a test can reach the
        // part functions below on an Npc of its own (it sets `animation` itself). Never used by the game.
        struct TestSetup
        {
        };
        Npc(const MWAnim::Context& mwctx, TestSetup);

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

        // Task 0074e (finding 1 of the review of 0074d): what a rebuild or the constructor reads and sets for the
        // actor's base (skeleton, custom bones, animations, head and hair), decided without changing the actor.
        struct BasePlan
        {
            std::string mSkeleton; // read with mContext.readActor: the new Play's bones
            std::vector<std::string> mCustomBones; // each read with readActor, its custom bones added to the bones
            std::vector<std::string> mAnimations; // read with mContext.readAnimations, added to the new Play
            std::string mHeadModel;
            std::string mHairModel;
            bool mDefaultSkeleton = false;
            // Task 0074f (finding 2 of the review of 0074e): what the plan was made for, committed by applyBasePlan
            // once every read has succeeded (unset: the actor keeps its own).
            std::optional<NpcType> mNpcType;
            std::optional<ViewMode> mViewMode;
            const MWAnim::Context* mPartContext = nullptr;
        };
        // The plan for an actor of type getNpcType() in `view`, whose parts are read with `partContext`. Virtual for
        // the tests only (they hand in a plan of their own: a real one needs the ESM store).
        virtual BasePlan makeBasePlan(ViewMode view, const MWAnim::Context* partContext);
        BasePlan makeBasePlan() { return makeBasePlan(mViewMode, mPartContext); }
        // Rebuilds for `view` and `partContext` (Player::setViewMode): they become the actor's only if the plan's reads
        // succeed. rebuild() is the same for the current ones.
        void rebuild(ViewMode view, const MWAnim::Context* partContext);
        // Gives the CharacterController the actor's new animation (MechanicsManager::forceStateUpdate). Virtual for the
        // tests only: the manager needs the running game.
        virtual void refreshController();
        // Reads every file of `plan` into locals, and only then removes the parts, commits the plan's type, view and
        // part context and gives the actor its new skeleton and animations: a read that throws leaves the actor as it
        // was: the old animation, its bones and every pointer to them still valid, and the same type, view mode and
        // part context, so that the next updateParts (or the same setViewMode) tries again.
        void applyBasePlan(const BasePlan& plan);
        // The constructor's base: applyBasePlan, then the parts. A rebuild goes through runRebuild.
        void updateNpcBase();
        // A rebuild (task 0074e, finding 1): `plan` is applied (nothing is changed if a read throws; the exception
        // reaches the caller), then `parts` inserts the parts and the actor is compiled (also when `parts` throws,
        // runEquipmentUpdate), and `refresh` gives the CharacterController the new animation, also when `parts`
        // throws, since `animation` is the new one by then (Npc::rebuild passes MechanicsManager::forceStateUpdate).
        template <class Parts, class Refresh>
        void runRebuild(const BasePlan& plan, Parts&& parts, Refresh&& refresh)
        {
            applyBasePlan(plan);
            try
            {
                runEquipmentUpdate(parts, [this] { compile(); });
            }
            catch (...)
            {
                refresh();
                throw;
            }
            refresh();
        }

        // Virtual for the tests only (a test actor has no record to ask).
        virtual NpcType getNpcType() const;

        // Reads the part's model and attaches it. False when the model could not be read (a std::exception from the
        // read is logged and caught, as OpenMW 0.51 does per part; task 0074e, finding 7); true otherwise.
        bool insertBoundedPart(ESM::PartReferenceType type, const std::string& model, const std::string& bonename,
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
        // Task 0074h: reads the ammunition model and adds it to the weapon part's placeholder (mAttachAmmo, which must
        // be set). False, with nothing changed, when the model cannot be read (a std::exception, logged once per path).
        bool attachAmmoModel(const std::string& model);

        // Sheathing ([Game] weapon sheathing, shield sheathing; off by default). Needs bones a modded skeleton adds.
        void appendCustomBoneFiles(const std::string& model, std::vector<std::string>& files) const;
        void removePath(std::vector<vsg::Node*>& path);
        void updateSheathing();
        void updateCarriedLeftVisible();
        void updateHolsteredWeapon();
        void updateHolsteredShield();
        void updateQuiver();
        // Task 0074h: the per-part reads of the sheathing code. Each reads first and attaches only on success; a
        // std::exception from a read is logged once per path (logPartReadError) and the part is left out, so a bad file
        // no longer ends a mechanics-driven session. All main thread (the part update).
        bool attachQuiverArrow(const std::string& model, const std::string& bone, std::optional<vsg::vec4> glowColor);
        bool attachHolsteredWeapon(const std::string& mesh, const std::string& scabbard, bool hasScabbard,
            const std::string& bone, bool visible, Anim::AutoPlay* clock, std::optional<vsg::vec4> glowColor,
            vsg::Switch& weaponSwitch);
        bool attachHolsteredShield(const std::string& mesh, const std::string& holstered, bool hasHolsteredModel,
            const std::string& bone, std::optional<vsg::vec4> glowColor);
        bool holsteredModelHasSheath(const std::string& holstered) const;
        // The model as a normal read or readHolstered gives it; null when the read throws a std::exception.
        vsg::ref_ptr<vsg::Node> readGuarded(const std::string& model, bool holstered) const;
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
        // Set by show() (any thread that calls it), done by updatePendingQuiver on the main thread.
        bool mQuiverDirty = false;

        static NpcType getNpcType(const MWWorld::Ptr& ptr);
        // Empties the lists updateParts fills again (Wielding::clearPartControllers and the face), not the object's
        // own auto-play groups.
        void clearControllers();
        // The node work of updateParts (equipment, head and hair, weapon, sheathing, merged body, body parts).
        void updatePartNodes();
        // One part update around `nodes` (updateParts passes updatePartNodes): the lists are emptied, `nodes` inserts
        // and removes parts, and what stays is linked again on every way out (task 0074b, finding 6).
        template <class Nodes>
        void runPartUpdate(Nodes&& nodes)
        {
            clearControllers();
            soundsToPlay.clear();
            mPartControllers.runUpdate(nodes, [this] { relinkKeptControllers(); });
        }
        // Task 0074d (review of 0074b, finding 2): every part, the merged copy, the holstered items and the quiver go,
        // the lists first, so that none of them points into a removed node even when what follows throws (a rebuild
        // reads the skeleton and the animations next). updateNpcBase and a failed compile() start with it.
        void removeAllParts();
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
        // Its second half (task 0074d, finding 4: reachable by a test): keeps the copy of `result`'s template, or makes
        // a new one, with `hidden` left out, and takes its slots from the stock parts; links nothing.
        bool attachMergedCopy(const ESM::Race& race, bool female, const MergedBodies::TemplateResult& result,
            const std::vector<int>& hidden);
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
