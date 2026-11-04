#ifndef OPENMW_MWRENDER_PARTSLOTS_H
#define OPENMW_MWRENDER_PARTSLOTS_H

#include <array>
#include <functional>
#include <span>

#include <components/esm3/loadarmo.hpp>

namespace ESM
{
    struct BodyPart;
}

namespace MWRender
{
    // Task 0069 stage e review, finding 1: the part slot bookkeeping of Npc::updateParts, apart from the nodes, so that
    // a test runs the same code the game does. For each part slot (ESM::PartReferenceType) it keeps which group holds
    // it (an inventory slot, or -1 for the race's body part, the head and the hair) and at what priority (0: nothing).
    // A part takes a slot only with a higher priority than what holds it; what held it goes first. The rules and the
    // numbers are the inherited ones (OpenMW's NpcAnimation::updateParts, at 442eec6e2f), moved here unchanged.
    //
    // Npc owns one and does the node work through Nodes and the attach callbacks. Main thread only, like the Npc.
    class PartSlots
    {
    public:
        // What the bookkeeping asks of the nodes. Npc removes the part's node and forgets its controllers.
        class Nodes
        {
        public:
            // The node of `part`, if it has one, goes.
            virtual void detachPart(ESM::PartReferenceType part) = 0;
            // Second review, finding 10: called by wear just before a worn part that has a body part asks for its
            // slot, whether it gets it or not, after the slot's group went and the helmet took the hair. Npc marks a
            // skin part's model there (task 0027), where the code before PartSlots marked it.
            virtual void beforeWornPart(ESM::PartReferenceType /*part*/, const ESM::BodyPart& /*bodyPart*/) {}

        protected:
            ~Nodes() = default;
        };

        // One inventory slot of the equipment pass, in the pass's order.
        struct EquipmentSlot
        {
            int mInventorySlot; // MWWorld::InventoryStore::Slot_*
            int mBasePriority; // the inherited FIXME: "based on the number of reserved slots"
            bool mHidesHair; // the helmet
        };

        // What is worn in an inventory slot, as the equipment pass sees it.
        enum class Worn
        {
            Nothing,
            Clothing,
            Armor,
            Other // a weapon or a light, for example: no parts, priority 1
        };

        // One part of a worn item, as Npc::addPartGroup saw it: the slot, and the body part getBodyPart chose for the
        // actor's sex and view, or none (the slot is then reserved without a part).
        struct WornPart
        {
            ESM::PartReferenceType mPart;
            const ESM::BodyPart* mBodyPart;
        };

        // Attaches the node of `bodyPart` in `part` (Npc: insertBoundedPart, and the item's sound).
        using Attach = std::function<void(ESM::PartReferenceType part, const ESM::BodyPart& bodyPart)>;

        PartSlots(); // every slot empty

        // The equipment pass's inventory slots: robe, skirt, helmet, cuirass, greaves, pauldrons, boots, gauntlets,
        // shirt, pants, the carried left and right.
        static std::span<const EquipmentSlot> equipmentSlots();
        // A worn item's priority: ((base + 1) << 1) for clothing, one more for armour (a robe is 24, a skirt 8); 1 for
        // anything else.
        static int wornPriority(int basePriority, Worn worn);
        // The slots a worn robe or skirt holds without a part (none for every other inventory slot).
        static std::span<const ESM::PartReferenceType> reservedParts(int inventorySlot);

        int group(ESM::PartReferenceType part) const { return mGroups[part]; }
        int priority(ESM::PartReferenceType part) const { return mPriorities[part]; }
        std::span<const int> groups() const { return mGroups; }
        std::span<const int> priorities() const { return mPriorities; }

        // Npc::removeIndividualPart: the slot is empty again and its node goes.
        void remove(ESM::PartReferenceType part, Nodes& nodes);
        // Npc::reserveIndividualPart: `group` holds the slot without a part, if `priority` is higher.
        void reserve(ESM::PartReferenceType part, int group, int priority, Nodes& nodes);
        // Npc::removePartGroup: every slot `group` holds is emptied.
        void removeGroup(int group, Nodes& nodes);
        // The decision of Npc::addOrReplaceIndividualPart: if `priority` is higher than what holds the slot, that goes
        // and `group` holds the slot; true then, and the caller attaches the new part's node.
        bool take(ESM::PartReferenceType part, int group, int priority, Nodes& nodes);

        // The equipment pass of Npc::updateParts for one inventory slot: what that slot held goes; then, if something
        // is worn, the helmet takes the hair away, a worn item's parts take their slots (or reserve them when they have
        // no body part), and a robe or skirt reserves its slots.
        void wear(
            const EquipmentSlot& slot, Worn worn, std::span<const WornPart> parts, Nodes& nodes, const Attach& attach);

        // The last loop of Npc::updateParts: every slot from the neck down that nothing holds (priority below 1) gets
        // the race's body part, if there is one, with group -1 and priority 1. `raceParts` is indexed by slot
        // (getBodyParts); the slots in `skip` (ascending: the merged body's) are left alone.
        void addBodyParts(std::span<const ESM::BodyPart* const> raceParts, std::span<const int> skip, Nodes& nodes,
            const Attach& attach);

        // The merged body takes `slots`: a body part kept from an earlier update there (group -1, priority 1 or more)
        // goes.
        void releaseBodyParts(std::span<const int> slots, Nodes& nodes);

    private:
        std::array<int, ESM::PRT_Count> mGroups;
        std::array<int, ESM::PRT_Count> mPriorities;
    };
}

#endif
