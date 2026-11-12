// Task 0121, stage N5-1 of decision 0017: the actor appearance the Skyrim SE pack hands the core, made from decoded
// records held in an ActorIndex. Hand-built records; no plugin bytes, no game data.
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

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

// ---- through the world view and the cell source (task 0121, N5-1) -----------------------------------------------

TEST(SkyrimAppearanceSource, a_view_with_no_plugins_knows_no_npc_and_says_so)
{
    SkyrimWorldView view;
    view.finish();
    ActorAppearance a;
    std::string error;
    EXPECT_FALSE(view.describeActor(0x100, 1, a, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(a.parts.empty());
}

TEST(SkyrimAppearanceSource, the_cell_source_forwards_to_its_view_and_is_found_by_dynamic_cast)
{
    SkyrimWorldView view;
    view.finish();
    SkyrimCellSource source(view);
    openeden::sdk::game::WorldCellSource& generic = source;
    auto* appearance = dynamic_cast<openeden::sdk::game::ActorAppearanceSource*>(&generic);
    ASSERT_NE(appearance, nullptr);
    ActorAppearance a;
    std::string error;
    EXPECT_FALSE(appearance->describeActor(0x100, 1, a, error));
    EXPECT_NE(error.find("NPC"), std::string::npos) << error;
}

// ---- Task 0125: the NPC's facegen head ---------------------------------------------------------------------------

namespace
{
    LoadOrder loadOrder()
    {
        LoadOrder order;
        std::string error;
        EXPECT_TRUE(order.add("Skyrim.esm", false, error));
        EXPECT_TRUE(order.add("Dawnguard.esm", false, error));
        EXPECT_TRUE(order.add("Dragonborn.esm", false, error));
        EXPECT_TRUE(order.add("Light.esl", true, error));
        return order;
    }
}

TEST(SkyrimFacegenPath, a_form_of_the_first_master_names_its_folder_and_eight_hex_digits)
{
    const LoadOrder order = loadOrder();
    EXPECT_EQ(faceGeometryPath(0x00013255, order),
        "meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\00013255.nif");
}

TEST(SkyrimFacegenPath, the_load_order_byte_is_not_part_of_the_file_name)
{
    const LoadOrder order = loadOrder();
    // The data holds meshes\...\facegeom\dragonborn.esm\0001773a.nif for the form 0x0201773a of this load order.
    EXPECT_EQ(faceGeometryPath(0x0201773a, order),
        "meshes\\actors\\character\\facegendata\\facegeom\\dragonborn.esm\\0001773a.nif");
}

TEST(SkyrimFacegenPath, a_light_plugin_form_uses_its_twelve_bit_local_id)
{
    const LoadOrder order = loadOrder();
    EXPECT_EQ(faceGeometryPath(0xFE000abc, order),
        "meshes\\actors\\character\\facegendata\\facegeom\\light.esl\\00000abc.nif");
}

TEST(SkyrimFacegenPath, an_id_no_plugin_owns_has_no_path)
{
    const LoadOrder order = loadOrder();
    EXPECT_EQ(faceGeometryPath(0x0F000001, order), "");
    EXPECT_EQ(faceGeometryPath(kNullFormId, order), "");
}

TEST(SkyrimLoadOrder, nameOfId_finds_normal_and_light_owners)
{
    const LoadOrder order = loadOrder();
    EXPECT_EQ(order.nameOfId(0x01000001), std::optional<std::string>("dawnguard.esm"));
    EXPECT_EQ(order.nameOfId(0xFE000123), std::optional<std::string>("light.esl"));
    EXPECT_EQ(order.nameOfId(0x05000000), std::nullopt);
}

namespace
{
    FaceFileProbe probeFor(const LoadOrder& order, std::vector<std::string> existing, std::vector<std::string>* asked)
    {
        FaceFileProbe probe;
        probe.mOrder = &order;
        probe.mExists = [existing = std::move(existing), asked](const std::string& path) {
            if (asked != nullptr)
                asked->push_back(path);
            for (const std::string& e : existing)
                if (e == path)
                    return true;
            return false;
        };
        return probe;
    }
}

TEST(SkyrimAppearance, an_npc_with_a_facegen_file_wears_the_face_instead_of_its_head_parts)
{
    const ActorIndex index = world();
    const LoadOrder order = loadOrder();
    const std::string path = faceGeometryPath(kNpc, order);
    ASSERT_FALSE(path.empty());
    const FaceFileProbe probe = probeFor(order, { path }, nullptr);
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error, {}, &probe)) << error;
    EXPECT_EQ(models(a, ActorPartKind::Face), (std::vector<std::string>{ path.substr(7) })); // relative to the mesh root
    EXPECT_TRUE(models(a, ActorPartKind::HeadPart).empty());
    // Skin, outfit, then the face.
    ASSERT_EQ(a.parts.size(), 4u);
    EXPECT_EQ(a.parts[0].kind, ActorPartKind::Skin);
    EXPECT_EQ(a.parts[1].kind, ActorPartKind::Armor);
    EXPECT_EQ(a.parts[2].kind, ActorPartKind::Armor);
    EXPECT_EQ(a.parts[3].kind, ActorPartKind::Face);
    EXPECT_EQ(a.parts[3].slotMask, 0u);
}

TEST(SkyrimAppearance, an_npc_without_a_facegen_file_keeps_its_head_parts)
{
    const ActorIndex index = world();
    const LoadOrder order = loadOrder();
    std::vector<std::string> asked;
    const FaceFileProbe probe = probeFor(order, {}, &asked);
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error, {}, &probe)) << error;
    EXPECT_TRUE(models(a, ActorPartKind::Face).empty());
    EXPECT_EQ(models(a, ActorPartKind::HeadPart),
        (std::vector<std::string>{ "head\\malehead.nif", "head\\brow.nif", "head\\hair.nif" }));
    EXPECT_EQ(asked, (std::vector<std::string>{ faceGeometryPath(kNpc, order) })); // asked for, exactly once
}

TEST(SkyrimAppearance, no_probe_means_no_face_and_the_old_result)
{
    const ActorIndex index = world();
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error, {}, nullptr)) << error;
    EXPECT_EQ(a.parts.size(), 6u);
    EXPECT_TRUE(models(a, ActorPartKind::Face).empty());
}

TEST(SkyrimAppearance, a_probe_without_a_load_order_or_test_is_ignored)
{
    const ActorIndex index = world();
    FaceFileProbe empty;
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error, {}, &empty)) << error;
    EXPECT_EQ(a.parts.size(), 6u);
}

TEST(SkyrimAppearance, a_templated_npc_looks_up_the_face_of_the_npc_its_traits_come_from)
{
    ActorRecords r;
    RaceRecord race;
    race.mCommon.mId = kRaceNord;
    race.mSkeleton[0] = "skel.nif";
    r.mRaces.push_back(race);
    ActorNpc base = npc(0x700, kRaceNord, false);
    r.mNpcs.push_back(base);
    ActorNpc templated = npc(0x701, kRaceNord, false);
    templated.mTemplate = 0x700;
    templated.mTemplateFlags = TemplateFlags::Traits;
    r.mNpcs.push_back(templated);
    ActorIndex index;
    index.add(r);

    const LoadOrder order = loadOrder();
    const std::string templatePath = faceGeometryPath(0x700, order);
    const FaceFileProbe probe = probeFor(order, { templatePath }, nullptr);
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, 0x701, 1, a, error, {}, &probe)) << error;
    EXPECT_EQ(models(a, ActorPartKind::Face), (std::vector<std::string>{ templatePath.substr(7) }));
}

TEST(SkyrimAppearance, a_facegen_path_over_the_limit_is_not_used)
{
    const ActorIndex index = world();
    const LoadOrder order = loadOrder();
    const std::string path = faceGeometryPath(kNpc, order);
    const FaceFileProbe probe = probeFor(order, { path }, nullptr);
    AppearanceLimits limits;
    limits.mMaxPathBytes = 60; // the skeleton path (49 bytes) fits, the facegen path (70) does not
    ActorAppearance a;
    std::string error;
    ASSERT_TRUE(makeActorAppearance(index, kNpc, 1, a, error, limits, &probe)) << error;
    EXPECT_TRUE(models(a, ActorPartKind::Face).empty());
}
