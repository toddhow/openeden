// Tests for MWRender::PartSlots (task 0069 stage e review, finding 1): the part slot bookkeeping of Npc::updateParts,
// and the merged body's hidden regions (Body::hiddenMergedSlots) checked against what that same bookkeeping makes the
// stock parts draw, over whole update sequences. Written before the bookkeeping moved out of Npc and run red against
// stubs. Synthetic records only; the item part lists are made up, only their shape matters.

#include "apps/openmw/mwrender/partslots.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include <components/body/mergedruntime.hpp>
#include <components/esm3/loadbody.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using MWRender::PartSlots;
    using Inv = MWWorld::InventoryStore;
    using Worn = PartSlots::Worn;

    enum class Drawn
    {
        Nothing,
        Skin, // the race's body part
        Item // a part of a worn item
    };

    // What each slot draws, as the nodes would show it.
    struct FakeNodes final : PartSlots::Nodes
    {
        std::array<Drawn, ESM::PRT_Count> mDrawn{};
        std::vector<ESM::PartReferenceType> mDetached;

        void detachPart(ESM::PartReferenceType part) override
        {
            mDrawn[part] = Drawn::Nothing;
            mDetached.push_back(part);
        }
    };

    ESM::BodyPart makeBodyPart(int type)
    {
        ESM::BodyPart part;
        part.mData.mType = static_cast<unsigned char>(type);
        return part;
    }

    const ESM::BodyPart sSkin = makeBodyPart(ESM::BodyPart::MT_Skin);
    const ESM::BodyPart sCloth = makeBodyPart(ESM::BodyPart::MT_Clothing);
    const ESM::BodyPart sArmour = makeBodyPart(ESM::BodyPart::MT_Armor);

    // The race's skin slots (bodyparts.cpp, getBodyParts' map), which are the merged body's slots too.
    const std::vector<int> sMergedSlots{ ESM::PRT_Neck, ESM::PRT_Cuirass, ESM::PRT_Groin, ESM::PRT_RHand,
        ESM::PRT_LHand, ESM::PRT_RWrist, ESM::PRT_LWrist, ESM::PRT_RForearm, ESM::PRT_LForearm, ESM::PRT_RUpperarm,
        ESM::PRT_LUpperarm, ESM::PRT_RFoot, ESM::PRT_LFoot, ESM::PRT_RAnkle, ESM::PRT_LAnkle, ESM::PRT_RKnee,
        ESM::PRT_LKnee, ESM::PRT_RLeg, ESM::PRT_LLeg };

    std::array<const ESM::BodyPart*, ESM::PRT_Count> raceParts()
    {
        std::array<const ESM::BodyPart*, ESM::PRT_Count> parts{};
        for (const int slot : sMergedSlots)
            parts[static_cast<std::size_t>(slot)] = &sSkin;
        return parts;
    }

    struct Item
    {
        std::string mName;
        int mInventorySlot;
        Worn mWorn;
        std::vector<PartSlots::WornPart> mParts;
    };

    PartSlots::WornPart with(ESM::PartReferenceType part, const ESM::BodyPart& bodyPart)
    {
        return { part, &bodyPart };
    }

    PartSlots::WornPart without(ESM::PartReferenceType part)
    {
        return { part, nullptr };
    }

    // A catalogue of items, one or more per inventory slot.
    const std::vector<Item>& catalogue()
    {
        static const std::vector<Item> items{
            { "shirt", Inv::Slot_Shirt, Worn::Clothing,
                { with(ESM::PRT_Cuirass, sCloth), with(ESM::PRT_RUpperarm, sCloth), with(ESM::PRT_LUpperarm, sCloth),
                    with(ESM::PRT_RForearm, sCloth), with(ESM::PRT_LForearm, sCloth) } },
            // A shirt whose sleeves have no part for this sex: the forearms are reserved, not drawn.
            { "shirt without sleeves", Inv::Slot_Shirt, Worn::Clothing,
                { with(ESM::PRT_Cuirass, sCloth), without(ESM::PRT_RForearm), without(ESM::PRT_LForearm) } },
            { "pants", Inv::Slot_Pants, Worn::Clothing,
                { with(ESM::PRT_Groin, sCloth), with(ESM::PRT_RLeg, sCloth), with(ESM::PRT_LLeg, sCloth),
                    with(ESM::PRT_RKnee, sCloth), with(ESM::PRT_LKnee, sCloth) } },
            { "robe", Inv::Slot_Robe, Worn::Clothing,
                { with(ESM::PRT_Skirt, sCloth), with(ESM::PRT_Cuirass, sCloth), with(ESM::PRT_RUpperarm, sCloth),
                    with(ESM::PRT_LUpperarm, sCloth) } },
            { "skirt", Inv::Slot_Skirt, Worn::Clothing, { with(ESM::PRT_Skirt, sCloth) } },
            { "armour skirt", Inv::Slot_Skirt, Worn::Armor, { with(ESM::PRT_Skirt, sArmour) } },
            { "cuirass", Inv::Slot_Cuirass, Worn::Armor,
                { with(ESM::PRT_Cuirass, sArmour), with(ESM::PRT_Groin, sArmour) } },
            { "greaves", Inv::Slot_Greaves, Worn::Armor,
                { with(ESM::PRT_Groin, sArmour), with(ESM::PRT_RLeg, sArmour), with(ESM::PRT_LLeg, sArmour),
                    with(ESM::PRT_RKnee, sArmour), with(ESM::PRT_LKnee, sArmour) } },
            { "boots", Inv::Slot_Boots, Worn::Armor,
                { with(ESM::PRT_RFoot, sArmour), with(ESM::PRT_LFoot, sArmour), with(ESM::PRT_RAnkle, sArmour),
                    with(ESM::PRT_LAnkle, sArmour) } },
            { "shoes", Inv::Slot_Boots, Worn::Clothing,
                { with(ESM::PRT_RFoot, sCloth), with(ESM::PRT_LFoot, sCloth) } },
            { "left gauntlet", Inv::Slot_LeftGauntlet, Worn::Armor,
                { with(ESM::PRT_LHand, sArmour), with(ESM::PRT_LWrist, sArmour), with(ESM::PRT_LForearm, sArmour) } },
            { "right gauntlet", Inv::Slot_RightGauntlet, Worn::Armor,
                { with(ESM::PRT_RHand, sArmour), with(ESM::PRT_RWrist, sArmour), with(ESM::PRT_RForearm, sArmour) } },
            { "left glove", Inv::Slot_LeftGauntlet, Worn::Clothing, { with(ESM::PRT_LHand, sCloth) } },
            // A glove with no part for this sex: the hand is reserved and nothing is drawn there.
            { "right glove without part", Inv::Slot_RightGauntlet, Worn::Clothing, { without(ESM::PRT_RHand) } },
            { "left pauldron", Inv::Slot_LeftPauldron, Worn::Armor,
                { with(ESM::PRT_LPauldron, sArmour), with(ESM::PRT_LUpperarm, sArmour) } },
            { "right pauldron", Inv::Slot_RightPauldron, Worn::Armor, { with(ESM::PRT_RPauldron, sArmour) } },
            { "helmet", Inv::Slot_Helmet, Worn::Armor, { with(ESM::PRT_Head, sArmour) } },
            { "torch", Inv::Slot_CarriedLeft, Worn::Other, {} },
            { "sword", Inv::Slot_CarriedRight, Worn::Other, {} },
        };
        return items;
    }

    const Item& item(const std::string& name)
    {
        for (const Item& entry : catalogue())
        {
            if (entry.mName == name)
                return entry;
        }
        throw std::runtime_error("no item " + name);
    }

    using Outfit = std::map<int, const Item*>; // inventory slot -> item

    Outfit outfit(const std::vector<std::string>& names)
    {
        Outfit result;
        for (const std::string& name : names)
        {
            const Item& entry = item(name);
            result[entry.mInventorySlot] = &entry;
        }
        return result;
    }

    // One Npc::updateParts as far as the part slots go: the equipment pass in its order, then (merged) the hidden
    // regions and the merged body taking its slots, then the race's body parts. Returns the hidden regions.
    std::vector<int> update(PartSlots& slots, FakeNodes& nodes, const Outfit& worn, bool merged)
    {
        const PartSlots::Attach attachItem
            = [&](ESM::PartReferenceType part, const ESM::BodyPart&) { nodes.mDrawn[part] = Drawn::Item; };
        for (const PartSlots::EquipmentSlot& slot : PartSlots::equipmentSlots())
        {
            const auto found = worn.find(slot.mInventorySlot);
            if (found == worn.end())
                slots.wear(slot, Worn::Nothing, {}, nodes, attachItem);
            else
                slots.wear(slot, found->second->mWorn, found->second->mParts, nodes, attachItem);
        }
        std::vector<int> hidden;
        std::vector<int> skip;
        if (merged)
        {
            hidden = Body::hiddenMergedSlots(sMergedSlots, slots.groups(), slots.priorities());
            slots.releaseBodyParts(sMergedSlots, nodes);
            skip = sMergedSlots;
        }
        const auto parts = raceParts();
        slots.addBodyParts(parts, skip, nodes,
            [&](ESM::PartReferenceType part, const ESM::BodyPart&) { nodes.mDrawn[part] = Drawn::Skin; });
        return hidden;
    }

    // The same outfits on a stock actor and on a merged one: after every update, the merged body hides exactly the
    // merged slots where the stock actor draws no skin, draws no stock skin there itself, and draws the same items.
    void expectSameAsStock(const std::vector<Outfit>& outfits, bool mergedStartsStock = false)
    {
        PartSlots stock;
        FakeNodes stockNodes;
        PartSlots merged;
        FakeNodes mergedNodes;
        bool firstMergedUpdate = true;
        for (std::size_t i = 0; i < outfits.size(); ++i)
        {
            update(stock, stockNodes, outfits[i], false);
            const bool mergedNow = !(mergedStartsStock && firstMergedUpdate);
            firstMergedUpdate = false;
            const std::vector<int> hidden = update(merged, mergedNodes, outfits[i], mergedNow);
            if (!mergedNow)
                continue;
            std::vector<int> expected;
            for (const int slot : sMergedSlots)
            {
                if (stockNodes.mDrawn[static_cast<std::size_t>(slot)] != Drawn::Skin)
                    expected.push_back(slot);
            }
            EXPECT_EQ(hidden, expected) << "update " << i;
            for (int slot = 0; slot < ESM::PRT_Count; ++slot)
            {
                const auto index = static_cast<std::size_t>(slot);
                const bool mergedSlot = std::binary_search(sMergedSlots.begin(), sMergedSlots.end(), slot);
                if (mergedSlot)
                {
                    EXPECT_NE(mergedNodes.mDrawn[index], Drawn::Skin) << "update " << i << " slot " << slot;
                    EXPECT_EQ(mergedNodes.mDrawn[index] == Drawn::Item, stockNodes.mDrawn[index] == Drawn::Item)
                        << "update " << i << " slot " << slot;
                }
                else
                {
                    EXPECT_EQ(mergedNodes.mDrawn[index], stockNodes.mDrawn[index])
                        << "update " << i << " slot " << slot;
                }
            }
        }
    }

    TEST(PartSlots, the_equipment_pass_is_the_inherited_slot_list)
    {
        // npc.cpp (Npc::updateParts at 188e00596f, and OpenMW's NpcAnimation::updateParts): robe 11, skirt 3, every
        // other slot 0, in this order; the helmet takes the hair.
        const std::vector<std::pair<int, int>> expected{ { Inv::Slot_Robe, 11 }, { Inv::Slot_Skirt, 3 },
            { Inv::Slot_Helmet, 0 }, { Inv::Slot_Cuirass, 0 }, { Inv::Slot_Greaves, 0 }, { Inv::Slot_LeftPauldron, 0 },
            { Inv::Slot_RightPauldron, 0 }, { Inv::Slot_Boots, 0 }, { Inv::Slot_LeftGauntlet, 0 },
            { Inv::Slot_RightGauntlet, 0 }, { Inv::Slot_Shirt, 0 }, { Inv::Slot_Pants, 0 },
            { Inv::Slot_CarriedLeft, 0 }, { Inv::Slot_CarriedRight, 0 } };
        const std::span<const PartSlots::EquipmentSlot> slots = PartSlots::equipmentSlots();
        ASSERT_EQ(slots.size(), expected.size());
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            EXPECT_EQ(slots[i].mInventorySlot, expected[i].first) << i;
            EXPECT_EQ(slots[i].mBasePriority, expected[i].second) << i;
            EXPECT_EQ(slots[i].mHidesHair, slots[i].mInventorySlot == Inv::Slot_Helmet) << i;
        }
    }

    TEST(PartSlots, worn_priorities_are_the_inherited_formula)
    {
        EXPECT_EQ(PartSlots::wornPriority(11, Worn::Clothing), 24); // a robe
        EXPECT_EQ(PartSlots::wornPriority(11, Worn::Armor), 25);
        EXPECT_EQ(PartSlots::wornPriority(3, Worn::Clothing), 8); // a skirt
        EXPECT_EQ(PartSlots::wornPriority(3, Worn::Armor), 9);
        EXPECT_EQ(PartSlots::wornPriority(0, Worn::Clothing), 2);
        EXPECT_EQ(PartSlots::wornPriority(0, Worn::Armor), 3);
        EXPECT_EQ(PartSlots::wornPriority(11, Worn::Other), 1);
        EXPECT_EQ(PartSlots::wornPriority(0, Worn::Other), 1);
    }

    TEST(PartSlots, a_robe_and_a_skirt_reserve_the_inherited_slots)
    {
        const std::vector<ESM::PartReferenceType> robe{ ESM::PRT_Groin, ESM::PRT_Skirt, ESM::PRT_RLeg, ESM::PRT_LLeg,
            ESM::PRT_RUpperarm, ESM::PRT_LUpperarm, ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RForearm,
            ESM::PRT_LForearm, ESM::PRT_Cuirass };
        const std::vector<ESM::PartReferenceType> skirt{ ESM::PRT_Groin, ESM::PRT_RLeg, ESM::PRT_LLeg };
        const auto robeParts = PartSlots::reservedParts(Inv::Slot_Robe);
        const auto skirtParts = PartSlots::reservedParts(Inv::Slot_Skirt);
        EXPECT_EQ(std::vector<ESM::PartReferenceType>(robeParts.begin(), robeParts.end()), robe);
        EXPECT_EQ(std::vector<ESM::PartReferenceType>(skirtParts.begin(), skirtParts.end()), skirt);
        for (const int slot : { Inv::Slot_Helmet, Inv::Slot_Cuirass, Inv::Slot_Shirt, Inv::Slot_Pants,
                 Inv::Slot_CarriedLeft, Inv::Slot_Amulet })
            EXPECT_TRUE(PartSlots::reservedParts(slot).empty()) << slot;
    }

    TEST(PartSlots, a_new_slot_list_is_empty)
    {
        const PartSlots slots;
        for (int part = 0; part < ESM::PRT_Count; ++part)
        {
            EXPECT_EQ(slots.group(static_cast<ESM::PartReferenceType>(part)), -1);
            EXPECT_EQ(slots.priority(static_cast<ESM::PartReferenceType>(part)), 0);
        }
    }

    TEST(PartSlots, a_part_takes_a_slot_only_with_a_higher_priority)
    {
        PartSlots slots;
        FakeNodes nodes;
        EXPECT_TRUE(slots.take(ESM::PRT_Cuirass, -1, 1, nodes));
        EXPECT_EQ(slots.group(ESM::PRT_Cuirass), -1);
        EXPECT_EQ(slots.priority(ESM::PRT_Cuirass), 1);
        EXPECT_FALSE(slots.take(ESM::PRT_Cuirass, Inv::Slot_Shirt, 1, nodes)); // equal: no
        EXPECT_TRUE(slots.take(ESM::PRT_Cuirass, Inv::Slot_Shirt, 2, nodes));
        EXPECT_EQ(slots.group(ESM::PRT_Cuirass), Inv::Slot_Shirt);
        EXPECT_EQ(slots.priority(ESM::PRT_Cuirass), 2);
        // What held the slot went first, each time.
        EXPECT_EQ(nodes.mDetached, (std::vector<ESM::PartReferenceType>{ ESM::PRT_Cuirass, ESM::PRT_Cuirass }));

        slots.reserve(ESM::PRT_Cuirass, Inv::Slot_Robe, 2, nodes); // equal: no
        EXPECT_EQ(slots.group(ESM::PRT_Cuirass), Inv::Slot_Shirt);
        slots.reserve(ESM::PRT_Cuirass, Inv::Slot_Robe, 24, nodes);
        EXPECT_EQ(slots.group(ESM::PRT_Cuirass), Inv::Slot_Robe);
        EXPECT_EQ(slots.priority(ESM::PRT_Cuirass), 24);
        EXPECT_EQ(nodes.mDetached.size(), 3u);
        EXPECT_FALSE(slots.take(ESM::PRT_Cuirass, Inv::Slot_Cuirass, 3, nodes));

        slots.remove(ESM::PRT_Cuirass, nodes);
        EXPECT_EQ(slots.group(ESM::PRT_Cuirass), -1);
        EXPECT_EQ(slots.priority(ESM::PRT_Cuirass), 0);
        EXPECT_EQ(nodes.mDetached.size(), 4u);
    }

    TEST(PartSlots, removing_a_group_empties_only_its_slots)
    {
        PartSlots slots;
        FakeNodes nodes;
        slots.reserve(ESM::PRT_Groin, Inv::Slot_Skirt, 8, nodes);
        slots.reserve(ESM::PRT_RLeg, Inv::Slot_Skirt, 8, nodes);
        EXPECT_TRUE(slots.take(ESM::PRT_Cuirass, Inv::Slot_Shirt, 2, nodes));
        EXPECT_TRUE(slots.take(ESM::PRT_Neck, -1, 1, nodes));
        nodes.mDetached.clear();
        slots.removeGroup(Inv::Slot_Skirt, nodes);
        EXPECT_EQ(slots.priority(ESM::PRT_Groin), 0);
        EXPECT_EQ(slots.priority(ESM::PRT_RLeg), 0);
        EXPECT_EQ(slots.group(ESM::PRT_Cuirass), Inv::Slot_Shirt);
        EXPECT_EQ(slots.group(ESM::PRT_Neck), -1);
        EXPECT_EQ(slots.priority(ESM::PRT_Neck), 1);
        EXPECT_EQ(nodes.mDetached, (std::vector<ESM::PartReferenceType>{ ESM::PRT_Groin, ESM::PRT_RLeg }));
    }

    TEST(PartSlots, a_stock_actor_draws_skin_where_nothing_is_worn)
    {
        PartSlots slots;
        FakeNodes nodes;
        update(slots, nodes, outfit({ "shirt", "boots", "torch" }), false);
        const std::vector<int> covered{ ESM::PRT_Cuirass, ESM::PRT_RForearm, ESM::PRT_LForearm, ESM::PRT_RUpperarm,
            ESM::PRT_LUpperarm, ESM::PRT_RFoot, ESM::PRT_LFoot, ESM::PRT_RAnkle, ESM::PRT_LAnkle };
        for (const int slot : sMergedSlots)
        {
            const bool isCovered = std::find(covered.begin(), covered.end(), slot) != covered.end();
            EXPECT_EQ(nodes.mDrawn[static_cast<std::size_t>(slot)], isCovered ? Drawn::Item : Drawn::Skin) << slot;
        }
        // The body parts stay over the next update (group -1, priority 1), and nothing detaches them.
        nodes.mDetached.clear();
        update(slots, nodes, outfit({ "shirt", "boots", "torch" }), false);
        EXPECT_EQ(slots.group(ESM::PRT_Neck), -1);
        EXPECT_EQ(slots.priority(ESM::PRT_Neck), 1);
        EXPECT_EQ(std::count(nodes.mDetached.begin(), nodes.mDetached.end(), ESM::PRT_Neck), 0);
    }

    TEST(PartSlots, a_robe_hides_its_reservations_and_blocks_a_cuirass)
    {
        // The robe (24) holds the chest before the cuirass (3) comes: the cuirass draws nothing, as stock.
        expectSameAsStock(
            { outfit({ "robe" }), outfit({ "robe", "cuirass", "greaves" }), outfit({ "cuirass" }), outfit({}) });
        PartSlots slots;
        FakeNodes nodes;
        const std::vector<int> hidden = update(slots, nodes, outfit({ "robe", "cuirass" }), true);
        const std::vector<int> expected{ ESM::PRT_Cuirass, ESM::PRT_Groin, ESM::PRT_RForearm, ESM::PRT_LForearm,
            ESM::PRT_RUpperarm, ESM::PRT_LUpperarm, ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RLeg, ESM::PRT_LLeg };
        EXPECT_EQ(hidden, expected);
        EXPECT_EQ(slots.group(ESM::PRT_Cuirass), Inv::Slot_Robe);
    }

    TEST(PartSlots, a_skirt_and_a_robe_in_any_order_hide_as_stock)
    {
        expectSameAsStock({ outfit({ "skirt" }), outfit({ "skirt", "robe" }), outfit({ "robe" }), outfit({ "skirt" }),
            outfit({ "armour skirt", "pants" }), outfit({}) });
    }

    TEST(PartSlots, parts_without_a_body_part_reserve_their_slots_as_stock)
    {
        expectSameAsStock({ outfit({ "shirt without sleeves", "right glove without part" }), outfit({ "shirt" }),
            outfit({ "right glove without part" }), outfit({}) });
        PartSlots slots;
        FakeNodes nodes;
        const std::vector<int> hidden = update(slots, nodes, outfit({ "right glove without part" }), true);
        EXPECT_EQ(hidden, (std::vector<int>{ ESM::PRT_RHand }));
        EXPECT_EQ(nodes.mDrawn[ESM::PRT_RHand], Drawn::Nothing);
    }

    TEST(PartSlots, held_items_and_the_helmet_hide_no_merged_region)
    {
        PartSlots slots;
        FakeNodes nodes;
        EXPECT_TRUE(update(slots, nodes, outfit({ "torch", "sword", "helmet" }), true).empty());
        expectSameAsStock({ outfit({ "torch" }), outfit({}), outfit({ "torch", "sword", "helmet" }), outfit({}) });
    }

    TEST(PartSlots, kept_body_parts_go_when_the_merged_body_takes_their_slots)
    {
        // An actor drawn stock first keeps its body parts at (-1, 1); the merged body's first update does not hide
        // them (they are skin) and then removes them, so no stock skin is drawn under the merged body.
        PartSlots slots;
        FakeNodes nodes;
        update(slots, nodes, outfit({ "pants" }), false);
        nodes.mDetached.clear();
        const std::vector<int> hidden = update(slots, nodes, outfit({ "pants" }), true);
        EXPECT_EQ(
            hidden, (std::vector<int>{ ESM::PRT_Groin, ESM::PRT_RKnee, ESM::PRT_LKnee, ESM::PRT_RLeg, ESM::PRT_LLeg }));
        for (const int slot : sMergedSlots)
        {
            EXPECT_NE(nodes.mDrawn[static_cast<std::size_t>(slot)], Drawn::Skin) << slot;
            EXPECT_EQ(slots.group(static_cast<ESM::PartReferenceType>(slot)) == -1,
                slots.priority(static_cast<ESM::PartReferenceType>(slot)) == 0)
                << slot;
        }
        EXPECT_EQ(std::count(nodes.mDetached.begin(), nodes.mDetached.end(), ESM::PRT_Neck), 1);
        expectSameAsStock({ outfit({ "pants" }), outfit({ "pants", "shirt" }), outfit({}) }, true);
    }

    TEST(PartSlots, random_outfit_sequences_hide_as_stock)
    {
        // 3,000 seeded sequences of 5 updates; each update wears, per inventory slot, nothing or one of that slot's
        // items, so robes, skirts, reservations and kept body parts meet in every order.
        std::map<int, std::vector<const Item*>> bySlot;
        for (const Item& entry : catalogue())
            bySlot[entry.mInventorySlot].push_back(&entry);
        std::mt19937 random(20261104);
        for (int sequence = 0; sequence < 3000; ++sequence)
        {
            std::vector<Outfit> outfits;
            for (int i = 0; i < 5; ++i)
            {
                Outfit worn;
                for (const auto& [slot, items] : bySlot)
                {
                    std::uniform_int_distribution<int> pick(-1, static_cast<int>(items.size()) - 1);
                    const int choice = pick(random);
                    if (choice >= 0)
                        worn[slot] = items[static_cast<std::size_t>(choice)];
                }
                outfits.push_back(worn);
            }
            SCOPED_TRACE("sequence " + std::to_string(sequence));
            expectSameAsStock(outfits, sequence % 2 == 1);
            if (::testing::Test::HasFailure())
                return;
        }
    }
}
