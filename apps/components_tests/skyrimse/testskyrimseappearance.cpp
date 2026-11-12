// Task 0121, stage N5-1 of decision 0017: the actor appearance the Skyrim SE pack hands the core, made from decoded
// records held in an ActorIndex. Hand-built records; no plugin bytes, no game data.
#include <game_skyrimse/appearance.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorAppearance;
    using openeden::sdk::game::ActorPartKind;

    constexpr FormId kRaceNord = 0x10;
    constexpr FormId kRaceOther = 0x11;
    constexpr FormId kNpc = 0x100;
    constexpr FormId kNpcFemale = 0x101;
    constexpr FormId kSkinArmor = 0x200;
    constexpr FormId kOutfit = 0x300;
    constexpr FormId kCuirass = 0x400;
    constexpr FormId kBoots = 0x401;
    constexpr FormId kArmaSkinBody = 0x500;
    constexpr FormId kArmaSkinHands = 0x501;
    constexpr FormId kArmaCuirass = 0x502;
    constexpr FormId kArmaBoots = 0x503;
    constexpr FormId kArmaOtherRace = 0x504;
    constexpr FormId kHeadFace = 0x600;
    constexpr FormId kHeadHair = 0x601;
    constexpr FormId kHeadBrowExtra = 0x602;

    constexpr std::uint32_t kSlotBody = 0x4;
    constexpr std::uint32_t kSlotHands = 0x8;
    constexpr std::uint32_t kSlotFeet = 0x80;

    ArmatureRecord armature(FormId id, FormId race, std::uint32_t slots, std::string male, std::string female)
    {
        ArmatureRecord a;
        a.mCommon.mId = id;
        a.mRace = race;
        a.mSlotMask = slots;
        a.mMaleModel = std::move(male);
        a.mFemaleModel = std::move(female);
        return a;
    }

    ArmorExtra armor(FormId id, std::uint32_t slots, std::vector<FormId> armatures)
    {
        ArmorExtra a;
        a.mCommon.mId = id;
        a.mSlotMask = slots;
        a.mArmatures = std::move(armatures);
        return a;
    }

    HeadPartRecord headPart(FormId id, std::uint32_t type, std::string model, std::vector<FormId> extra = {})
    {
        HeadPartRecord h;
        h.mCommon.mId = id;
        h.mType = type;
        h.mModel = std::move(model);
        h.mExtraParts = std::move(extra);
        return h;
    }

    ActorNpc npc(FormId id, FormId race, bool female)
    {
        ActorNpc n;
        n.mCommon.mId = id;
        n.mRace = race;
        n.mHasConfig = true;
        n.mFlags = female ? NpcFlags::Female : 0u;
        return n;
    }

    // A small world: one race with a skin of body and hands addons, an outfit of a cuirass (body slot) and boots, and
    // head parts. The NPC wears the outfit.
    ActorIndex world(bool outfitCoversBody = true)
    {
        ActorRecords r;
        RaceRecord race;
        race.mCommon.mId = kRaceNord;
        race.mSkeleton[0] = "actors\\character\\character assets\\skeleton.nif";
        race.mSkeleton[1] = "actors\\character\\character assets female\\skeleton_female.nif";
        race.mSkin = kSkinArmor;
        r.mRaces.push_back(race);
        RaceRecord noSkeleton;
        noSkeleton.mCommon.mId = kRaceOther;
        r.mRaces.push_back(noSkeleton);

        r.mArmatures.push_back(armature(kArmaSkinBody, kRaceNord, kSlotBody, "m\\body.nif", "f\\body.nif"));
        r.mArmatures.push_back(armature(kArmaSkinHands, kRaceNord, kSlotHands, "m\\hands.nif", "f\\hands.nif"));
        r.mArmatures.push_back(armature(kArmaCuirass, kRaceNord, kSlotBody, "m\\cuirass.nif", "f\\cuirass.nif"));
        r.mArmatures.push_back(armature(kArmaBoots, kRaceNord, kSlotFeet, "m\\boots.nif", "f\\boots.nif"));
        r.mArmatures.push_back(armature(kArmaOtherRace, kRaceOther, kSlotBody, "o\\body.nif", "o\\body.nif"));
        r.mArmors.push_back(armor(kSkinArmor, kSlotBody, { kArmaSkinBody, kArmaSkinHands }));
        r.mArmors.push_back(armor(kCuirass, kSlotBody, { kArmaOtherRace, kArmaCuirass }));
        r.mArmors.push_back(armor(kBoots, kSlotFeet, { kArmaBoots }));

        OutfitRecord outfit;
        outfit.mCommon.mId = kOutfit;
        outfit.mItems = outfitCoversBody ? std::vector<FormId>{ kCuirass, kBoots } : std::vector<FormId>{ kBoots };
        r.mOutfits.push_back(outfit);

        r.mHeadParts.push_back(headPart(kHeadFace, HeadPartType::Face, "head\\malehead.nif", { kHeadBrowExtra }));
        r.mHeadParts.push_back(headPart(kHeadHair, HeadPartType::Hair, "head\\hair.nif"));
        r.mHeadParts.push_back(headPart(kHeadBrowExtra, HeadPartType::Brows, "head\\brow.nif"));

        ActorNpc n = npc(kNpc, kRaceNord, false);
        n.mDefaultOutfit = kOutfit;
        n.mHeadParts = { kHeadFace, kHeadHair };
        r.mNpcs.push_back(n);
        ActorNpc f = npc(kNpcFemale, kRaceNord, true);
        f.mDefaultOutfit = kOutfit;
        r.mNpcs.push_back(f);

        ActorIndex index;
        index.add(r);
        return index;
    }

    std::vector<std::string> models(const ActorAppearance& a, ActorPartKind kind)
    {
        std::vector<std::string> out;
        for (const auto& p : a.parts)
            if (p.kind == kind)
                out.push_back(p.modelPath);
        return out;
    }
}

TEST(SkyrimAppearance, a_dressed_male_gets_skeleton_skin_outfit_and_head_parts_in_that_order)
{
    const ActorIndex index = world();
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error)) << error;
    EXPECT_EQ(a.skeletonPath, "actors\\character\\character assets\\skeleton.nif");
    EXPECT_FALSE(a.female);
    // The cuirass covers the body slot, so only the hands of the skin stay.
    EXPECT_EQ(models(a, ActorPartKind::Skin), (std::vector<std::string>{ "m\\hands.nif" }));
    EXPECT_EQ(models(a, ActorPartKind::Armor), (std::vector<std::string>{ "m\\cuirass.nif", "m\\boots.nif" }));
    EXPECT_EQ(models(a, ActorPartKind::HeadPart),
        (std::vector<std::string>{ "head\\malehead.nif", "head\\brow.nif", "head\\hair.nif" }));
    ASSERT_EQ(a.parts.size(), 6u);
    EXPECT_EQ(a.parts[0].kind, ActorPartKind::Skin);
    EXPECT_EQ(a.parts[1].kind, ActorPartKind::Armor);
    EXPECT_EQ(a.parts[5].kind, ActorPartKind::HeadPart);
}

TEST(SkyrimAppearance, without_a_body_covering_the_skin_body_stays)
{
    const ActorIndex index = world(false);
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error)) << error;
    EXPECT_EQ(models(a, ActorPartKind::Skin), (std::vector<std::string>{ "m\\body.nif", "m\\hands.nif" }));
}

TEST(SkyrimAppearance, a_female_npc_gets_the_female_skeleton_and_models)
{
    const ActorIndex index = world();
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpcFemale, 1, a, error)) << error;
    EXPECT_TRUE(a.female);
    EXPECT_EQ(a.skeletonPath, "actors\\character\\character assets female\\skeleton_female.nif");
    EXPECT_EQ(models(a, ActorPartKind::Armor), (std::vector<std::string>{ "f\\cuirass.nif", "f\\boots.nif" }));
}

TEST(SkyrimAppearance, an_addon_for_another_race_is_not_worn)
{
    const ActorIndex index = world();
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error)) << error;
    for (const auto& p : a.parts)
        EXPECT_NE(p.modelPath, "o\\body.nif");
}

TEST(SkyrimAppearance, the_slot_mask_and_head_part_type_are_carried)
{
    const ActorIndex index = world();
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error)) << error;
    EXPECT_EQ(a.parts[1].slotMask, kSlotBody);
    EXPECT_EQ(a.parts[2].slotMask, kSlotFeet);
    EXPECT_EQ(a.parts[3].headPartType, HeadPartType::Face);
    EXPECT_EQ(a.parts[5].headPartType, HeadPartType::Hair);
}

TEST(SkyrimAppearance, an_unknown_npc_is_refused_with_a_reason)
{
    const ActorIndex index = world();
    ActorAppearance a;
    std::string error;
    EXPECT_FALSE(makeActorAppearance(index, 0xdead, 1, a, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(a.parts.empty());
}

TEST(SkyrimAppearance, a_race_with_no_skeleton_is_refused)
{
    ActorRecords r;
    RaceRecord race;
    race.mCommon.mId = kRaceOther;
    r.mRaces.push_back(race);
    r.mNpcs.push_back(npc(kNpc, kRaceOther, false));
    ActorIndex index;
    index.add(r);
    ActorAppearance a;
    std::string error;
    EXPECT_FALSE(makeActorAppearance(index, kNpc, 1, a, error));
    EXPECT_NE(error.find("skeleton"), std::string::npos) << error;
}

TEST(SkyrimAppearance, a_template_supplies_the_race_and_the_head_parts_when_the_traits_flag_is_set)
{
    ActorIndex source = world();
    ActorRecords r;
    ActorNpc templated = npc(0x110, 0, false);
    templated.mTemplate = kNpc;
    templated.mTemplateFlags = TemplateFlags::Traits | TemplateFlags::Inventory;
    r.mNpcs.push_back(templated);
    source.add(r);
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(source, 0x110, 1, a, error)) << error;
    EXPECT_EQ(a.skeletonPath, "actors\\character\\character assets\\skeleton.nif");
    EXPECT_FALSE(models(a, ActorPartKind::HeadPart).empty());
    EXPECT_FALSE(models(a, ActorPartKind::Armor).empty());
}

TEST(SkyrimAppearance, the_part_count_is_capped)
{
    const ActorIndex index = world();
    AppearanceLimits limits;
    limits.mMaxParts = 3;
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error, limits)) << error;
    EXPECT_EQ(a.parts.size(), 3u);
}

TEST(SkyrimAppearance, a_head_part_cycle_through_extra_parts_ends)
{
    ActorRecords r;
    RaceRecord race;
    race.mCommon.mId = kRaceNord;
    race.mSkeleton[0] = "s.nif";
    r.mRaces.push_back(race);
    r.mHeadParts.push_back(headPart(kHeadFace, HeadPartType::Face, "a.nif", { kHeadHair }));
    r.mHeadParts.push_back(headPart(kHeadHair, HeadPartType::Hair, "b.nif", { kHeadFace }));
    ActorNpc n = npc(kNpc, kRaceNord, false);
    n.mHeadParts = { kHeadFace };
    r.mNpcs.push_back(n);
    ActorIndex index;
    index.add(r);
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error)) << error;
    EXPECT_LE(a.parts.size(), 4u);
}

TEST(SkyrimAppearance, overlong_model_paths_are_dropped)
{
    ActorIndex index = world();
    ActorRecords r;
    r.mArmatures.push_back(armature(kArmaBoots, kRaceNord, kSlotFeet, std::string(2000, 'x'), "f\\boots.nif"));
    index.add(r);
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error)) << error;
    for (const auto& p : a.parts)
        EXPECT_LE(p.modelPath.size(), 512u);
}
