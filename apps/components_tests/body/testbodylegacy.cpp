// Task 0063 (AdvancedBodies B1), stage c and the legacy part of stage d: the legacy adapter, on synthetic
// ESM::BodyPart records in a synthetic MWWorld::Store. Written from the task spec and bodylegacy.hpp only.
//
// Build note: this file needs the game's store (apps/openmw/mwworld/store.cpp), bodyparts.cpp and bodylegacy.cpp, so
// it is registered in openmw-tests (which links openmw-lib), not in components-tests.

#include "bodyfixtures.hpp"

#include "apps/openmw/mwrender/bodylegacy.hpp"
#include "apps/openmw/mwrender/bodyparts.hpp"

#include <components/body/bodyjson.hpp>
#include <components/body/bodyvalidate.hpp>
#include <components/misc/constants.hpp>
#include <components/mwanimation/partbones.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using namespace BodyTest;
    using Store = MWWorld::Store<ESM::BodyPart>;

    struct Slot
    {
        ESM::PartReferenceType mType;
        const char* mName;
        ESM::BodyPart::MeshPart mMesh;
    };

    // In ESM::PartReferenceType order, as bodylegacy.hpp lists the part type names.
    const Slot sSlots[] = {
        { ESM::PRT_Neck, "neck", ESM::BodyPart::MP_Neck },
        { ESM::PRT_Cuirass, "cuirass", ESM::BodyPart::MP_Chest },
        { ESM::PRT_Groin, "groin", ESM::BodyPart::MP_Groin },
        { ESM::PRT_RHand, "rhand", ESM::BodyPart::MP_Hand },
        { ESM::PRT_LHand, "lhand", ESM::BodyPart::MP_Hand },
        { ESM::PRT_RWrist, "rwrist", ESM::BodyPart::MP_Wrist },
        { ESM::PRT_LWrist, "lwrist", ESM::BodyPart::MP_Wrist },
        { ESM::PRT_RForearm, "rforearm", ESM::BodyPart::MP_Forearm },
        { ESM::PRT_LForearm, "lforearm", ESM::BodyPart::MP_Forearm },
        { ESM::PRT_RUpperarm, "rupperarm", ESM::BodyPart::MP_Upperarm },
        { ESM::PRT_LUpperarm, "lupperarm", ESM::BodyPart::MP_Upperarm },
        { ESM::PRT_RFoot, "rfoot", ESM::BodyPart::MP_Foot },
        { ESM::PRT_LFoot, "lfoot", ESM::BodyPart::MP_Foot },
        { ESM::PRT_RAnkle, "rankle", ESM::BodyPart::MP_Ankle },
        { ESM::PRT_LAnkle, "lankle", ESM::BodyPart::MP_Ankle },
        { ESM::PRT_RKnee, "rknee", ESM::BodyPart::MP_Knee },
        { ESM::PRT_LKnee, "lknee", ESM::BodyPart::MP_Knee },
        { ESM::PRT_RLeg, "rleg", ESM::BodyPart::MP_Upperleg },
        { ESM::PRT_LLeg, "lleg", ESM::BodyPart::MP_Upperleg },
        { ESM::PRT_Tail, "tail", ESM::BodyPart::MP_Tail },
    };

    // The body-part records a human has: one per mesh part (the hands, wrists... are shared by both sides).
    const ESM::BodyPart::MeshPart sHumanMeshParts[]
        = { ESM::BodyPart::MP_Neck, ESM::BodyPart::MP_Chest, ESM::BodyPart::MP_Groin, ESM::BodyPart::MP_Hand,
              ESM::BodyPart::MP_Wrist, ESM::BodyPart::MP_Forearm, ESM::BodyPart::MP_Upperarm, ESM::BodyPart::MP_Foot,
              ESM::BodyPart::MP_Ankle, ESM::BodyPart::MP_Knee, ESM::BodyPart::MP_Upperleg };

    /// A race id used by one test only: getBodyParts caches per race in a static map.
    std::string uniqueRace(const std::string& base)
    {
        static std::atomic<int> counter{ 0 };
        return base + "_" + std::to_string(counter.fetch_add(1));
    }

    ESM::RefId refId(const std::string& text)
    {
        return ESM::RefId::stringRefId(text);
    }

    ESM::BodyPart makePart(const std::string& id, const std::string& race, ESM::BodyPart::MeshPart part, bool female,
        const std::string& model, bool vampire = false, ESM::BodyPart::MeshType type = ESM::BodyPart::MT_Skin)
    {
        ESM::BodyPart record;
        record.blank();
        record.mId = refId(id);
        record.mRace = refId(race);
        record.mData.mPart = static_cast<unsigned char>(part);
        record.mData.mVampire = vampire ? 1 : 0;
        record.mData.mFlags = female ? ESM::BodyPart::BPF_Female : 0;
        record.mData.mType = static_cast<unsigned char>(type);
        record.mModel = model;
        return record;
    }

    std::string partId(const std::string& race, bool female, ESM::BodyPart::MeshPart part)
    {
        return race + (female ? "_f_" : "_m_") + std::to_string(static_cast<int>(part));
    }

    std::string partModel(const std::string& race, bool female, ESM::BodyPart::MeshPart part)
    {
        return "b/" + partId(race, female, part) + ".nif";
    }

    /// Adds the human skin parts of one sex, except the mesh parts listed in `leaveOut`.
    void addSkin(
        Store& store, const std::string& race, bool female, const std::vector<ESM::BodyPart::MeshPart>& leaveOut = {})
    {
        for (const ESM::BodyPart::MeshPart part : sHumanMeshParts)
        {
            if (std::find(leaveOut.begin(), leaveOut.end(), part) != leaveOut.end())
            {
                continue;
            }
            store.insertStatic(makePart(partId(race, female, part), race, part, female, partModel(race, female, part)));
        }
    }

    void addHeadAndHair(Store& store, const std::string& race, bool female)
    {
        const std::string sex = female ? "f" : "m";
        store.insertStatic(makePart(
            race + "_head_" + sex, race, ESM::BodyPart::MP_Head, female, "b/" + race + "_head_" + sex + ".nif"));
        store.insertStatic(makePart(
            race + "_hair_" + sex, race, ESM::BodyPart::MP_Hair, female, "b/" + race + "_hair_" + sex + ".nif"));
    }

    const Body::MeshEntry* findMesh(const Body::BodyPackage& package, const std::string& id)
    {
        for (const Body::MeshEntry& mesh : package.mMeshes)
        {
            if (mesh.mId == id)
            {
                return &mesh;
            }
        }
        return nullptr;
    }

    std::vector<const MWRender::LegacyNote*> notesWith(const MWRender::LegacyBody& body, const std::string& code)
    {
        std::vector<const MWRender::LegacyNote*> notes;
        for (const MWRender::LegacyNote& note : body.mNotes)
        {
            if (note.mCode == code)
            {
                notes.push_back(&note);
            }
        }
        return notes;
    }

    bool hasNoteFor(const MWRender::LegacyBody& body, const std::string& code, const std::string& part)
    {
        for (const MWRender::LegacyNote* note : notesWith(body, code))
        {
            if (note->mPart == part)
            {
                return true;
            }
        }
        return false;
    }

    /// The adapter's selected parts must equal what the engine's own functions select.
    void expectMatchesEngine(const MWRender::LegacyBody& body, const std::string& race, bool female, const Store& store)
    {
        const ESM::RefId raceId = refId(race);
        const std::vector<const ESM::BodyPart*>& parts
            = MWRender::getBodyParts(raceId, female, false, false, store);
        ASSERT_EQ(parts.size(), static_cast<std::size_t>(ESM::PRT_Count));
        for (const Slot& slot : sSlots)
        {
            SCOPED_TRACE(slot.mName);
            const ESM::BodyPart* part = parts[slot.mType];
            const Body::MeshEntry* mesh = findMesh(body.mPackage, slot.mName);
            if (part == nullptr)
            {
                EXPECT_EQ(mesh, nullptr) << "a mesh for a slot the engine leaves empty";
                continue;
            }
            ASSERT_NE(mesh, nullptr) << "no mesh for a slot the engine fills with " << part->mId;
            EXPECT_EQ(mesh->mPath, part->mModel);
            EXPECT_EQ(mesh->mFormat, Body::MeshFormat::Nif);
            EXPECT_EQ(mesh->mRole, Body::MeshRole::Skin);
            ASSERT_TRUE(mesh->mPartType.has_value());
            EXPECT_EQ(*mesh->mPartType, slot.mName);
            ASSERT_TRUE(mesh->mAttachBone.has_value());
            EXPECT_EQ(*mesh->mAttachBone, MWAnim::sPartBones.at(slot.mType));
            EXPECT_EQ(mesh->mLod, 0);
        }
    }

    /// The chosen head and hair must equal getHeadModel and getHairModel for the record the notes name.
    void expectHeadAndHair(const MWRender::LegacyBody& body, const std::string& race, bool female, const Store& store,
        const std::string& headId, const std::string& hairId)
    {
        const Body::MeshEntry* head = findMesh(body.mPackage, "head");
        ASSERT_NE(head, nullptr);
        EXPECT_EQ(head->mPath, MWRender::getHeadModel(false, false, female, refId(race), refId(headId), store));
        EXPECT_EQ(head->mRole, Body::MeshRole::Head);
        ASSERT_TRUE(head->mAttachBone.has_value());
        EXPECT_EQ(*head->mAttachBone, MWAnim::sPartBones.at(ESM::PRT_Head));
        const Body::MeshEntry* hair = findMesh(body.mPackage, "hair");
        ASSERT_NE(hair, nullptr);
        EXPECT_EQ(hair->mPath, MWRender::getHairModel(false, refId(hairId), store));
        EXPECT_EQ(hair->mRole, Body::MeshRole::Hair);
        ASSERT_TRUE(hair->mAttachBone.has_value());
        EXPECT_EQ(*hair->mAttachBone, MWAnim::sPartBones.at(ESM::PRT_Hair));

        const auto headNotes = notesWith(body, "LEGACY-HEAD-001");
        ASSERT_EQ(headNotes.size(), 1u);
        EXPECT_NE(headNotes[0]->mMessage.find(headId), std::string::npos) << headNotes[0]->mMessage;
        const auto hairNotes = notesWith(body, "LEGACY-HAIR-001");
        ASSERT_EQ(hairNotes.size(), 1u);
        EXPECT_NE(hairNotes[0]->mMessage.find(hairId), std::string::npos) << hairNotes[0]->mMessage;
    }

    /// A skeleton profile that names the Morrowind bones the fake summaries use.
    opas::anim::SkeletonProfile legacyProfile()
    {
        opas::anim::SkeletonProfile profile;
        profile.name = "legacy-test";
        profile.bones = { { "Pelvis", "Bip01 Pelvis" }, { "Spine", "Bip01 Spine" }, { "Neck", "Bip01 Neck" },
            { "Head", "Bip01 Head" }, { "LeftFoot", "Bip01 L Foot" }, { "RightFoot", "Bip01 R Foot" } };
        return profile;
    }

    /// A summary in Morrowind units (about 128 units tall) skinned to bones the legacy profile maps.
    Body::MeshSummary legacySummary()
    {
        return makeSummary(6, { "Bip01 Pelvis", "Bip01 Spine" }, { -20, -10, 0 }, { 20, 10, 128 });
    }

    void giveEveryPartASummary(const MWRender::LegacyBody& body, FakeMeshSource& source)
    {
        for (const Body::MeshEntry& mesh : body.mPackage.mMeshes)
        {
            source.mSummaries[mesh.mPath] = legacySummary();
        }
    }

    /// Builds the human male reference (complete set, a head, a hair) and returns the body.
    struct Reference
    {
        Store mStore;
        std::string mRace = uniqueRace("legacy_ref");
        MWRender::LegacyOptions mOptions;
        FakeMeshSource mSource;
    };

    MWRender::LegacyBody buildReference(Reference& reference, bool female)
    {
        addSkin(reference.mStore, reference.mRace, false);
        addSkin(reference.mStore, reference.mRace, true);
        addHeadAndHair(reference.mStore, reference.mRace, false);
        addHeadAndHair(reference.mStore, reference.mRace, true);
        reference.mOptions.mProfile = legacyProfile();
        // First build without a source to learn the paths, then give every path a summary.
        MWRender::LegacyBody first
            = MWRender::buildLegacyBody(refId(reference.mRace), female, reference.mStore, reference.mOptions);
        giveEveryPartASummary(first, reference.mSource);
        reference.mOptions.mMeshSource = &reference.mSource;
        return MWRender::buildLegacyBody(refId(reference.mRace), female, reference.mStore, reference.mOptions);
    }
}

TEST(BodyLegacy, ToBodyUnitsConvertsAtOneFunction)
{
    const double units = static_cast<double>(Constants::UnitsPerMeter);
    EXPECT_NEAR(MWRender::toBodyUnits(units), 1.0, 1e-9);
    EXPECT_NEAR(MWRender::toBodyUnits(0.0), 0.0, 1e-12);
    EXPECT_NEAR(MWRender::toBodyUnits(2.5 * units), 2.5, 1e-9);
    EXPECT_NEAR(MWRender::toBodyUnits(-units), -1.0, 1e-9);
    EXPECT_NEAR(MWRender::toBodyUnits(128.0), 128.0 / units, 1e-9);
}

TEST(BodyLegacy, CompleteMaleSetMatchesTheEngineSelection)
{
    Store store;
    const std::string race = uniqueRace("complete_male");
    addSkin(store, race, false);
    addHeadAndHair(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    EXPECT_TRUE(body.mSupported);
    expectMatchesEngine(body, race, false, store);
    expectHeadAndHair(body, race, false, store, race + "_head_m", race + "_hair_m");
    for (const char* provided : { "neck", "cuirass", "groin", "rhand", "lleg", "lfoot" })
    {
        EXPECT_FALSE(hasNoteFor(body, "LEGACY-MISSING-001", provided)) << provided;
    }
    EXPECT_EQ(notesWith(body, "LEGACY-FALLBACK-001").size(), 0u);
    EXPECT_EQ(notesWith(body, "LEGACY-BEAST-001").size(), 0u);
    EXPECT_EQ(body.mPackage.mIdentity.mId, "morrowind." + race + ".male");
}

TEST(BodyLegacy, CompleteFemaleSetMatchesTheEngineSelection)
{
    Store store;
    const std::string race = uniqueRace("complete_female");
    addSkin(store, race, false);
    addSkin(store, race, true);
    addHeadAndHair(store, race, false);
    addHeadAndHair(store, race, true);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), true, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, true, store);
    expectHeadAndHair(body, race, true, store, race + "_head_f", race + "_hair_f");
    EXPECT_EQ(notesWith(body, "LEGACY-FALLBACK-001").size(), 0u);
    EXPECT_EQ(body.mPackage.mIdentity.mId, "morrowind." + race + ".female");
    for (const Slot& slot : sSlots)
    {
        const Body::MeshEntry* mesh = findMesh(body.mPackage, slot.mName);
        if (mesh != nullptr)
        {
            // Female parts, not the male ones.
            EXPECT_NE(mesh->mPath.find("_f_"), std::string::npos) << slot.mName << " " << mesh->mPath;
        }
    }
}

TEST(BodyLegacy, MaleAndFemaleBuildsDiffer)
{
    Store store;
    const std::string race = uniqueRace("sexes");
    addSkin(store, race, false);
    addSkin(store, race, true);
    addHeadAndHair(store, race, false);
    addHeadAndHair(store, race, true);
    const MWRender::LegacyBody male = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    const MWRender::LegacyBody female = MWRender::buildLegacyBody(refId(race), true, store, MWRender::LegacyOptions{});
    EXPECT_NE(male.mPackage.mIdentity.mId, female.mPackage.mIdentity.mId);
    ASSERT_NE(findMesh(male.mPackage, "cuirass"), nullptr);
    ASSERT_NE(findMesh(female.mPackage, "cuirass"), nullptr);
    EXPECT_NE(findMesh(male.mPackage, "cuirass")->mPath, findMesh(female.mPackage, "cuirass")->mPath);
}

TEST(BodyLegacy, MissingFemalePartIsStoodInByTheMalePartAndNoted)
{
    Store store;
    const std::string race = uniqueRace("fallback");
    addSkin(store, race, false);
    addSkin(store, race, true, { ESM::BodyPart::MP_Hand });
    addHeadAndHair(store, race, false);
    addHeadAndHair(store, race, true);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), true, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, true, store);

    // The engine puts the male hand in; the adapter must agree and say so, for both hands and for nothing else.
    const Body::MeshEntry* rhand = findMesh(body.mPackage, "rhand");
    const Body::MeshEntry* lhand = findMesh(body.mPackage, "lhand");
    ASSERT_NE(rhand, nullptr);
    ASSERT_NE(lhand, nullptr);
    EXPECT_EQ(rhand->mPath, partModel(race, false, ESM::BodyPart::MP_Hand));
    EXPECT_EQ(lhand->mPath, partModel(race, false, ESM::BodyPart::MP_Hand));
    EXPECT_TRUE(hasNoteFor(body, "LEGACY-FALLBACK-001", "rhand"));
    EXPECT_TRUE(hasNoteFor(body, "LEGACY-FALLBACK-001", "lhand"));
    EXPECT_EQ(notesWith(body, "LEGACY-FALLBACK-001").size(), 2u);
    EXPECT_FALSE(hasNoteFor(body, "LEGACY-FALLBACK-001", "cuirass"));
    EXPECT_FALSE(hasNoteFor(body, "LEGACY-MISSING-001", "rhand")) << "the slot is filled, only by a stand-in";
}

TEST(BodyLegacy, FemaleWithNoFemalePartsAtAllUsesTheMaleSetAndNotesEverySlot)
{
    Store store;
    const std::string race = uniqueRace("only_male_parts");
    addSkin(store, race, false);
    addHeadAndHair(store, race, true);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), true, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, true, store);
    EXPECT_EQ(notesWith(body, "LEGACY-FALLBACK-001").size(), 19u) << "one note per filled slot";
}

TEST(BodyLegacy, FirstPersonPartsAreNeverPicked)
{
    Store store;
    const std::string race = uniqueRace("firstperson");
    // The first-person chest and hand come first in store order.
    store.insertStatic(
        makePart(race + "_chest.1st", race, ESM::BodyPart::MP_Chest, false, "b/" + race + "_chest1st.nif"));
    store.insertStatic(makePart(race + "_hand.1st", race, ESM::BodyPart::MP_Hand, false, "b/" + race + "_hand1st.nif"));
    addSkin(store, race, false);
    store.insertStatic(
        makePart(race + "_wrist.1st", race, ESM::BodyPart::MP_Wrist, false, "b/" + race + "_wrist1st.nif"));
    addHeadAndHair(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, false, store);
    for (const Body::MeshEntry& mesh : body.mPackage.mMeshes)
    {
        EXPECT_EQ(mesh.mPath.find("1st"), std::string::npos) << mesh.mId << ": " << mesh.mPath;
    }
    ASSERT_NE(findMesh(body.mPackage, "cuirass"), nullptr);
    EXPECT_EQ(findMesh(body.mPackage, "cuirass")->mPath, partModel(race, false, ESM::BodyPart::MP_Chest));
}

TEST(BodyLegacy, OnlyFirstPersonPartsMeansTheSlotIsMissing)
{
    Store store;
    const std::string race = uniqueRace("only_firstperson");
    store.insertStatic(makePart(race + "_neck.1st", race, ESM::BodyPart::MP_Neck, false, "b/" + race + "_neck1st.nif"));
    addSkin(store, race, false, { ESM::BodyPart::MP_Neck });
    addHeadAndHair(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, false, store);
    EXPECT_EQ(findMesh(body.mPackage, "neck"), nullptr);
    EXPECT_TRUE(hasNoteFor(body, "LEGACY-MISSING-001", "neck"));
}

TEST(BodyLegacy, VampireHeadIsNotPickedForAPlainHuman)
{
    Store store;
    const std::string race = uniqueRace("plainfolk");
    addSkin(store, race, false);
    // A vampire head and another race's and another sex's heads come first in store order.
    store.insertStatic(
        makePart(race + "_head_bitten", race, ESM::BodyPart::MP_Head, false, "b/" + race + "_head_bitten.nif", true));
    store.insertStatic(makePart("other_race_head", "other_race_x", ESM::BodyPart::MP_Head, false, "b/other_head.nif"));
    store.insertStatic(
        makePart(race + "_head_female", race, ESM::BodyPart::MP_Head, true, "b/" + race + "_head_f.nif"));
    store.insertStatic(makePart(race + "_head_a", race, ESM::BodyPart::MP_Head, false, "b/" + race + "_head_a.nif"));
    store.insertStatic(makePart(race + "_head_b", race, ESM::BodyPart::MP_Head, false, "b/" + race + "_head_b.nif"));
    store.insertStatic(
        makePart(race + "_hair_bitten", race, ESM::BodyPart::MP_Hair, false, "b/" + race + "_hair_bitten.nif", true));
    store.insertStatic(makePart(race + "_hair_a", race, ESM::BodyPart::MP_Hair, false, "b/" + race + "_hair_a.nif"));
    store.insertStatic(makePart(race + "_hair_b", race, ESM::BodyPart::MP_Hair, false, "b/" + race + "_hair_b.nif"));
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, false, store);
    const Body::MeshEntry* head = findMesh(body.mPackage, "head");
    ASSERT_NE(head, nullptr);
    EXPECT_EQ(head->mPath, "b/" + race + "_head_a.nif") << "the first suitable head in store order";
    expectHeadAndHair(body, race, false, store, race + "_head_a", race + "_hair_a");
    const Body::MeshEntry* hair = findMesh(body.mPackage, "hair");
    ASSERT_NE(hair, nullptr);
    EXPECT_EQ(hair->mPath.find("bitten"), std::string::npos);
}

TEST(BodyLegacy, FemaleHeadComesFromTheFemaleRecords)
{
    Store store;
    const std::string race = uniqueRace("femalehead");
    addSkin(store, race, false);
    addSkin(store, race, true);
    store.insertStatic(makePart(race + "_head_m", race, ESM::BodyPart::MP_Head, false, "b/" + race + "_head_m.nif"));
    store.insertStatic(makePart(race + "_head_f", race, ESM::BodyPart::MP_Head, true, "b/" + race + "_head_f.nif"));
    store.insertStatic(makePart(race + "_hair_m", race, ESM::BodyPart::MP_Hair, false, "b/" + race + "_hair_m.nif"));
    store.insertStatic(makePart(race + "_hair_f", race, ESM::BodyPart::MP_Hair, true, "b/" + race + "_hair_f.nif"));
    const MWRender::LegacyBody female = MWRender::buildLegacyBody(refId(race), true, store, MWRender::LegacyOptions{});
    expectHeadAndHair(female, race, true, store, race + "_head_f", race + "_hair_f");
    const MWRender::LegacyBody male = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    expectHeadAndHair(male, race, false, store, race + "_head_m", race + "_hair_m");
}

TEST(BodyLegacy, MissingSlotsAreNotedInSlotOrderAndHaveNoMesh)
{
    Store store;
    const std::string race = uniqueRace("missing");
    addSkin(store, race, false, { ESM::BodyPart::MP_Neck, ESM::BodyPart::MP_Groin, ESM::BodyPart::MP_Knee });
    addHeadAndHair(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, false, store);
    for (const char* missing : { "neck", "groin", "rknee", "lknee" })
    {
        EXPECT_EQ(findMesh(body.mPackage, missing), nullptr) << missing;
        EXPECT_TRUE(hasNoteFor(body, "LEGACY-MISSING-001", missing)) << missing;
    }
    for (const char* present : { "cuirass", "rhand", "lfoot", "rleg" })
    {
        EXPECT_FALSE(hasNoteFor(body, "LEGACY-MISSING-001", present)) << present;
    }
    // Slot order: the parts of the MISSING notes appear in ESM::PartReferenceType order.
    std::vector<std::size_t> positions;
    for (const MWRender::LegacyNote* note : notesWith(body, "LEGACY-MISSING-001"))
    {
        for (std::size_t i = 0; i < std::size(sSlots); ++i)
        {
            if (note->mPart == sSlots[i].mName)
            {
                positions.push_back(i);
            }
        }
        EXPECT_FALSE(note->mMessage.empty());
    }
    EXPECT_TRUE(std::is_sorted(positions.begin(), positions.end()));
}

TEST(BodyLegacy, NoHeadOrHairRecordsAreNoted)
{
    Store store;
    const std::string race = uniqueRace("nohead");
    addSkin(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    expectMatchesEngine(body, race, false, store);
    EXPECT_EQ(findMesh(body.mPackage, "head"), nullptr);
    EXPECT_EQ(findMesh(body.mPackage, "hair"), nullptr);
    EXPECT_EQ(notesWith(body, "LEGACY-HEAD-002").size(), 1u);
    EXPECT_EQ(notesWith(body, "LEGACY-HAIR-002").size(), 1u);
    EXPECT_EQ(notesWith(body, "LEGACY-HEAD-001").size(), 0u);
    EXPECT_EQ(notesWith(body, "LEGACY-HAIR-001").size(), 0u);
}

TEST(BodyLegacy, NotesComeInTheDocumentedOrder)
{
    Store store;
    const std::string race = uniqueRace("order");
    addSkin(store, race, false, { ESM::BodyPart::MP_Neck });
    addHeadAndHair(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    const auto positionOf = [&body](const std::string& code) {
        for (std::size_t i = 0; i < body.mNotes.size(); ++i)
        {
            if (body.mNotes[i].mCode == code)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int missing = positionOf("LEGACY-MISSING-001");
    const int head = positionOf("LEGACY-HEAD-001");
    const int hair = positionOf("LEGACY-HAIR-001");
    const int age = positionOf("LEGACY-AGE-001");
    ASSERT_GE(missing, 0);
    ASSERT_GE(head, 0);
    ASSERT_GE(hair, 0);
    ASSERT_GE(age, 0);
    EXPECT_LT(missing, head);
    EXPECT_LT(head, hair);
    EXPECT_LT(hair, age) << "package-wide notes come last";
}

TEST(BodyLegacy, BeastRaceIsNotSupportedYet)
{
    Store store;
    const std::string race = uniqueRace("beast");
    addSkin(store, race, false);
    addHeadAndHair(store, race, false);
    MWRender::LegacyOptions options;
    options.mBeast = true;
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, options);
    EXPECT_FALSE(body.mSupported);
    ASSERT_EQ(notesWith(body, "LEGACY-BEAST-001").size(), 1u);
    EXPECT_FALSE(notesWith(body, "LEGACY-BEAST-001")[0]->mMessage.empty());

    const MWRender::LegacyBody plain = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    EXPECT_TRUE(plain.mSupported);
    EXPECT_EQ(notesWith(plain, "LEGACY-BEAST-001").size(), 0u);
}

TEST(BodyLegacy, AgeClassIsUnspecifiedAndNeverEligibleForARestrictedTier)
{
    Store store;
    const std::string race = uniqueRace("age");
    addSkin(store, race, false);
    addHeadAndHair(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    EXPECT_EQ(body.mPackage.mIdentity.mAgeClass, Body::AgeClass::Unspecified);
    EXPECT_EQ(notesWith(body, "LEGACY-AGE-001").size(), 1u);
    for (const Body::AgeClass actor : { Body::AgeClass::Unspecified, Body::AgeClass::Minor, Body::AgeClass::Adult })
    {
        EXPECT_FALSE(Body::restrictedTierAllowed(body.mPackage.mIdentity.mAgeClass, actor));
    }
    EXPECT_FALSE(body.mPackage.mPhysicsProfile.has_value());
    EXPECT_FALSE(body.mPackage.mSoftTissue.has_value());
}

TEST(BodyLegacy, PackageIdIsLowercaseWithInvalidCharactersReplaced)
{
    Store store;
    const std::string base = uniqueRace("Dark Elf!");
    addSkin(store, base, false);
    addHeadAndHair(store, base, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(base), false, store, MWRender::LegacyOptions{});
    std::string expected = "morrowind.";
    for (const char c : base)
    {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (c >= 'A' && c <= 'Z')
        {
            expected += static_cast<char>(c - 'A' + 'a');
        }
        else
        {
            expected += valid ? c : '_';
        }
    }
    expected += ".male";
    EXPECT_EQ(body.mPackage.mIdentity.mId, expected);
    EXPECT_TRUE(Body::isValidPackageId(body.mPackage.mIdentity.mId));
}

TEST(BodyLegacy, BuildingTwiceGivesTheSamePackageAndNotes)
{
    Store store;
    const std::string race = uniqueRace("determinism");
    addSkin(store, race, false, { ESM::BodyPart::MP_Neck });
    addSkin(store, race, true);
    addHeadAndHair(store, race, false);
    addHeadAndHair(store, race, true);
    for (const bool female : { false, true })
    {
        const MWRender::LegacyBody first
            = MWRender::buildLegacyBody(refId(race), female, store, MWRender::LegacyOptions{});
        const MWRender::LegacyBody second
            = MWRender::buildLegacyBody(refId(race), female, store, MWRender::LegacyOptions{});
        EXPECT_EQ(first.mPackage, second.mPackage);
        EXPECT_EQ(first.mNotes, second.mNotes);
        EXPECT_EQ(Body::writeBodyPackage(first.mPackage), Body::writeBodyPackage(second.mPackage));
    }
}

TEST(BodyLegacy, TheBindingNamesTheProfile)
{
    Store store;
    const std::string race = uniqueRace("binding");
    addSkin(store, race, false);
    addHeadAndHair(store, race, false);
    {
        const MWRender::LegacyBody body
            = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
        EXPECT_FALSE(body.mPackage.mBinding.mSkeletonProfile.has_value());
        ASSERT_TRUE(body.mPackage.mBinding.mSkeletonPath.has_value());
        EXPECT_EQ(*body.mPackage.mBinding.mSkeletonPath, "data/profiles/humanoid-to-eden.skeletonprofile.json");
    }
    {
        MWRender::LegacyOptions options;
        options.mProfilePath = "somewhere/else.skeletonprofile.json";
        const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, options);
        ASSERT_TRUE(body.mPackage.mBinding.mSkeletonPath.has_value());
        EXPECT_EQ(*body.mPackage.mBinding.mSkeletonPath, "somewhere/else.skeletonprofile.json");
    }
    {
        MWRender::LegacyOptions options;
        options.mProfile = legacyProfile();
        const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, options);
        ASSERT_TRUE(body.mPackage.mBinding.mSkeletonProfile.has_value());
        EXPECT_EQ(*body.mPackage.mBinding.mSkeletonProfile, legacyProfile());
        EXPECT_FALSE(body.mPackage.mBinding.mSkeletonPath.has_value());
    }
}

TEST(BodyLegacy, MeshSummariesComeFromTheSourceInBodyUnits)
{
    Store store;
    const std::string race = uniqueRace("summaries");
    addSkin(store, race, false);
    addHeadAndHair(store, race, false);
    FakeMeshSource source;
    MWRender::LegacyOptions options;
    options.mMeshSource = &source;
    // Give a summary to every path but the neck's and the hair's.
    const MWRender::LegacyBody probe = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    giveEveryPartASummary(probe, source);
    source.mSummaries.erase(partModel(race, false, ESM::BodyPart::MP_Neck));
    source.mSummaries.erase("b/" + race + "_hair_m.nif");

    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, options);
    expectMatchesEngine(body, race, false, store);
    const double perMeter = static_cast<double>(Constants::UnitsPerMeter);
    for (const Body::MeshEntry& mesh : body.mPackage.mMeshes)
    {
        SCOPED_TRACE(mesh.mId);
        if (mesh.mId == "neck" || mesh.mId == "hair")
        {
            EXPECT_FALSE(mesh.mSummary.has_value());
            EXPECT_TRUE(hasNoteFor(body, "LEGACY-MESH-001", mesh.mId));
            continue;
        }
        EXPECT_FALSE(hasNoteFor(body, "LEGACY-MESH-001", mesh.mId));
        ASSERT_TRUE(mesh.mSummary.has_value());
        const Body::MeshSummary original = legacySummary();
        EXPECT_EQ(mesh.mSummary->mVertexCount, original.mVertexCount);
        EXPECT_EQ(mesh.mSummary->mTriangleCount, original.mTriangleCount);
        EXPECT_EQ(mesh.mSummary->mBoneNames, original.mBoneNames);
        for (int axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(mesh.mSummary->mBoundsMin[axis], original.mBoundsMin[axis] / perMeter, 1e-6) << axis;
            EXPECT_NEAR(mesh.mSummary->mBoundsMax[axis], original.mBoundsMax[axis] / perMeter, 1e-6) << axis;
        }
    }
    // The source was asked for every part, by path, as a "nif".
    EXPECT_FALSE(source.mCalls.empty());
    for (const auto& call : source.mCalls)
    {
        EXPECT_EQ(call.second, "nif");
    }
}

TEST(BodyLegacy, NoSourceMeansNoSummariesAndANoteForEveryPart)
{
    Store store;
    const std::string race = uniqueRace("nosource");
    addSkin(store, race, false);
    addHeadAndHair(store, race, false);
    const MWRender::LegacyBody body = MWRender::buildLegacyBody(refId(race), false, store, MWRender::LegacyOptions{});
    ASSERT_FALSE(body.mPackage.mMeshes.empty());
    for (const Body::MeshEntry& mesh : body.mPackage.mMeshes)
    {
        EXPECT_FALSE(mesh.mSummary.has_value()) << mesh.mId;
        EXPECT_TRUE(hasNoteFor(body, "LEGACY-MESH-001", mesh.mId)) << mesh.mId;
    }
}

TEST(BodyLegacy, TheBuiltPackageValidatesWithoutStructuralErrors)
{
    for (const bool female : { false, true })
    {
        Reference reference;
        const MWRender::LegacyBody body = buildReference(reference, female);
        expectMatchesEngine(body, reference.mRace, female, reference.mStore);

        Body::ValidateOptions options;
        options.mMeshSource = &reference.mSource;
        const Body::Report report = Body::validateBodyPackage(body.mPackage, options);
        // Structure, skinning, geometry, morph, age and provenance rules have no business failing on this package.
        // (Binding findings depend on the skeleton the adapter binds; see the report to the implementer.)
        for (const Body::Finding& finding : report.mFindings)
        {
            if (finding.mSeverity != Body::Severity::Error)
            {
                continue;
            }
            for (const char* group : { "BODY-STRUCT-", "BODY-SKIN-", "BODY-GEOM-", "BODY-MORPH-", "BODY-AGE-",
                     "BODY-PROV-", "BODY-ORIGIN-", "BODY-PROXY-", "BODY-PHYS-" })
            {
                EXPECT_NE(finding.mCode.compare(0, std::string(group).size(), group), 0)
                    << finding.mCode << " " << finding.mPath << ": " << finding.mMessage;
            }
        }
        // Parts of one body must be unique by (partType, role, lod): STRUCT-005 and STRUCT-004 in particular.
        EXPECT_EQ(findCode(report, "BODY-STRUCT-005"), nullptr) << describe(report);
        EXPECT_EQ(findCode(report, "BODY-STRUCT-004"), nullptr) << describe(report);
        EXPECT_EQ(findCode(report, "BODY-STRUCT-003"), nullptr) << describe(report);
    }
}

TEST(BodyLegacy, TheBuiltPackageSurvivesAJsonRoundTrip)
{
    Reference reference;
    const MWRender::LegacyBody body = buildReference(reference, false);
    const std::string text = Body::writeBodyPackage(body.mPackage);
    const Body::BodyLoadResult loaded = Body::readBodyPackage(text, "legacy");
    ASSERT_TRUE(loaded.mValue.has_value()) << loaded.mError.toString();
    EXPECT_EQ(*loaded.mValue, body.mPackage);
}

TEST(BodyConformance, LegacyAssembledBodyIsComplete)
{
    // The conformance table's third fixture: legacy_assembled, built from synthetic records.
    Reference reference;
    const MWRender::LegacyBody body = buildReference(reference, false);
    EXPECT_TRUE(body.mSupported);
    // 19 body slots are filled (a human has no tail), plus the head and the hair.
    EXPECT_EQ(body.mPackage.mMeshes.size(), 21u);
    EXPECT_EQ(notesWith(body, "LEGACY-BEAST-001").size(), 0u);
    EXPECT_EQ(notesWith(body, "LEGACY-FALLBACK-001").size(), 0u);
    EXPECT_EQ(notesWith(body, "LEGACY-MESH-001").size(), 0u);
    expectMatchesEngine(body, reference.mRace, false, reference.mStore);
}
