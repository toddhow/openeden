#include "partslots.hpp"

#include <algorithm>

#include "../mwworld/inventorystore.hpp"

namespace MWRender
{
    namespace
    {
        using Inv = MWWorld::InventoryStore;

        // FIXME (inherited): the priority is based on the number of reserved slots. There should be a better way.
        constexpr PartSlots::EquipmentSlot sEquipmentSlots[] = { { Inv::Slot_Robe, 11, false },
            { Inv::Slot_Skirt, 3, false }, { Inv::Slot_Helmet, 0, true }, { Inv::Slot_Cuirass, 0, false },
            { Inv::Slot_Greaves, 0, false }, { Inv::Slot_LeftPauldron, 0, false },
            { Inv::Slot_RightPauldron, 0, false }, { Inv::Slot_Boots, 0, false }, { Inv::Slot_LeftGauntlet, 0, false },
            { Inv::Slot_RightGauntlet, 0, false }, { Inv::Slot_Shirt, 0, false }, { Inv::Slot_Pants, 0, false },
            { Inv::Slot_CarriedLeft, 0, false }, { Inv::Slot_CarriedRight, 0, false } };

        constexpr ESM::PartReferenceType sRobeReserves[]
            = { ESM::PRT_Groin, ESM::PRT_Skirt, ESM::PRT_RLeg, ESM::PRT_LLeg, ESM::PRT_RUpperarm, ESM::PRT_LUpperarm,
                  ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RForearm, ESM::PRT_LForearm, ESM::PRT_Cuirass };
        constexpr ESM::PartReferenceType sSkirtReserves[] = { ESM::PRT_Groin, ESM::PRT_RLeg, ESM::PRT_LLeg };
    }

    PartSlots::PartSlots()
    {
        mGroups.fill(-1);
        mPriorities.fill(0);
    }

    std::span<const PartSlots::EquipmentSlot> PartSlots::equipmentSlots()
    {
        return sEquipmentSlots;
    }

    int PartSlots::wornPriority(int basePriority, Worn worn)
    {
        if (worn == Worn::Clothing)
            return ((basePriority + 1) << 1) + 0;
        if (worn == Worn::Armor)
            return ((basePriority + 1) << 1) + 1;
        return 1;
    }

    std::span<const ESM::PartReferenceType> PartSlots::reservedParts(int inventorySlot)
    {
        if (inventorySlot == Inv::Slot_Robe)
            return sRobeReserves;
        if (inventorySlot == Inv::Slot_Skirt)
            return sSkirtReserves;
        return {};
    }

    void PartSlots::remove(ESM::PartReferenceType part, Nodes& nodes)
    {
        mPriorities[part] = 0;
        mGroups[part] = -1;
        nodes.detachPart(part);
    }

    void PartSlots::reserve(ESM::PartReferenceType part, int group, int priority, Nodes& nodes)
    {
        if (priority > mPriorities[part])
        {
            remove(part, nodes);
            mPriorities[part] = priority;
            mGroups[part] = group;
        }
    }

    void PartSlots::removeGroup(int group, Nodes& nodes)
    {
        for (int i = 0; i < ESM::PRT_Count; i++)
        {
            if (mGroups[i] == group)
                remove(static_cast<ESM::PartReferenceType>(i), nodes);
        }
    }

    bool PartSlots::take(ESM::PartReferenceType part, int group, int priority, Nodes& nodes)
    {
        if (priority <= mPriorities[part])
            return false;
        remove(part, nodes);
        mGroups[part] = group;
        mPriorities[part] = priority;
        return true;
    }

    void PartSlots::wear(
        const EquipmentSlot& slot, Worn worn, std::span<const WornPart> parts, Nodes& nodes, const Attach& attach)
    {
        removeGroup(slot.mInventorySlot, nodes);
        if (worn == Worn::Nothing)
            return;
        if (slot.mHidesHair)
            remove(ESM::PRT_Hair, nodes);

        const int priority = wornPriority(slot.mBasePriority, worn);
        if (worn == Worn::Clothing || worn == Worn::Armor)
        {
            for (const WornPart& part : parts)
            {
                if (part.mBodyPart != nullptr)
                {
                    if (take(part.mPart, slot.mInventorySlot, priority, nodes))
                        attach(part.mPart, *part.mBodyPart);
                }
                else
                    reserve(part.mPart, slot.mInventorySlot, priority, nodes);
            }
        }
        for (const ESM::PartReferenceType part : reservedParts(slot.mInventorySlot))
            reserve(part, slot.mInventorySlot, priority, nodes);
    }

    void PartSlots::addBodyParts(
        std::span<const ESM::BodyPart* const> raceParts, std::span<const int> skip, Nodes& nodes, const Attach& attach)
    {
        for (int part = ESM::PRT_Neck; part < ESM::PRT_Count; ++part)
        {
            if (std::binary_search(skip.begin(), skip.end(), part))
                continue;
            const auto index = static_cast<std::size_t>(part);
            if (mPriorities[index] < 1 && index < raceParts.size() && raceParts[index] != nullptr)
            {
                const auto type = static_cast<ESM::PartReferenceType>(part);
                if (take(type, -1, 1, nodes))
                    attach(type, *raceParts[index]);
            }
        }
    }

    void PartSlots::releaseBodyParts(std::span<const int> slots, Nodes& nodes)
    {
        for (const int slot : slots)
        {
            if (slot < 0 || slot >= ESM::PRT_Count)
                continue;
            const auto type = static_cast<ESM::PartReferenceType>(slot);
            if (mGroups[type] < 0 && mPriorities[type] >= 1)
                remove(type, nodes);
        }
    }
}
