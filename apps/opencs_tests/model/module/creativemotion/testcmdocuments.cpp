// Task 0032: the creativemotion_editor module's rig and profile documents, written from the spec alone.
//
// Assumptions about names the spec leaves open (see the report):
//  - Property ids: "rig.name", "rig.bones", "rig.bodies", "rig.joints", "joint.<bone>.<field>" and
//    "joint.<bone>.swing" for rigs; "profile.rig", "default.<gain>" and "joint.<bone>.<gain>" for profiles, with
//    <field> the six Joint field names and <gain> one of stiffnessNmPerRad, dampingNmsPerRad, maxTorqueNm.
//  - Values read back with PropertySource::get are a double for Float properties, a std::int64_t for Int ones and a
//    std::string for String ones.
//  - Property groups are "Rig", the bone name, "Profile" and "Default motor".
//  - A failed open puts the loader's located message in `error`: it names the file or a JSON pointer.
#include "editor_modules/creativemotion_editor/documents.hpp"

#include "cmtestsupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

namespace CMTest
{
    namespace
    {
        using edit::AssetDocument;
        using edit::Command;
        using edit::PropertyDesc;
        using edit::PropertySource;
        using edit::PropertyType;
        using edit::PropertyValue;

        constexpr double kPi = std::numbers::pi;
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
        constexpr double kInf = std::numeric_limits<double>::infinity();

        const PropertyDesc* findDesc(const std::vector<PropertyDesc>& descs, const std::string& id)
        {
            const auto it = std::find_if(descs.begin(), descs.end(), [&](const PropertyDesc& d) { return d.id == id; });
            return it == descs.end() ? nullptr : &*it;
        }

        double getDouble(PropertySource& source, const std::string& id)
        {
            return std::get<double>(source.get(id));
        }

        bool contains(const std::string& text, const std::string& part)
        {
            return text.find(part) != std::string::npos;
        }

        // ---- open -----------------------------------------------------------------------------------------

        class CmDocumentsTest : public TempDirTest
        {
        protected:
            std::unique_ptr<AssetDocument> openRig(const std::filesystem::path& path)
            {
                std::string error;
                auto document = creativemotion_editor::openRig(path, error);
                EXPECT_NE(document, nullptr) << error;
                return document;
            }

            std::unique_ptr<AssetDocument> openProfile(const std::filesystem::path& path)
            {
                std::string error;
                auto document = creativemotion_editor::openProfile(path, error);
                EXPECT_NE(document, nullptr) << error;
                return document;
            }
        };

        TEST(CmFormatIds, areTheDocumentedOnes)
        {
            EXPECT_STREQ(creativemotion_editor::kRigFormat, "opas.rig");
            EXPECT_STREQ(creativemotion_editor::kProfileFormat, "opas.profile");
        }

        TEST_F(CmDocumentsTest, aValidRigOpensNotDirty)
        {
            const auto path = writeRigFile("valid", makeLimitsRig());
            std::string error;
            auto document = creativemotion_editor::openRig(path, error);
            ASSERT_NE(document, nullptr) << error;
            EXPECT_FALSE(document->isDirty());
            EXPECT_FALSE(document->assetId().empty());
            EXPECT_TRUE(document->tracks().empty());
        }

        TEST_F(CmDocumentsTest, aValidProfileOpensNotDirty)
        {
            const auto path = writeProfileFile("valid", makeProfile());
            std::string error;
            auto document = creativemotion_editor::openProfile(path, error);
            ASSERT_NE(document, nullptr) << error;
            EXPECT_FALSE(document->isDirty());
            EXPECT_FALSE(document->assetId().empty());
            EXPECT_TRUE(document->gizmos().empty());
            EXPECT_TRUE(document->tracks().empty());
        }

        TEST_F(CmDocumentsTest, aMissingRigFileGivesNullAndAMessage)
        {
            std::string error;
            EXPECT_EQ(creativemotion_editor::openRig(mDir / "missing.rig.json", error), nullptr);
            EXPECT_FALSE(error.empty());
        }

        TEST_F(CmDocumentsTest, aMissingProfileFileGivesNullAndAMessage)
        {
            std::string error;
            EXPECT_EQ(creativemotion_editor::openProfile(mDir / "missing.profile.json", error), nullptr);
            EXPECT_FALSE(error.empty());
        }

        TEST_F(CmDocumentsTest, aFileThatIsNotJsonGivesNullAndALocatedMessage)
        {
            const auto rigPath = mDir / "bad.rig.json";
            const auto profilePath = mDir / "bad.profile.json";
            writeText(rigPath, "{ this is not json");
            writeText(profilePath, "{ this is not json");

            std::string error;
            EXPECT_EQ(creativemotion_editor::openRig(rigPath, error), nullptr);
            EXPECT_FALSE(error.empty());
            EXPECT_TRUE(contains(error, "bad.rig.json")) << error;

            error.clear();
            EXPECT_EQ(creativemotion_editor::openProfile(profilePath, error), nullptr);
            EXPECT_FALSE(error.empty());
            EXPECT_TRUE(contains(error, "bad.profile.json")) << error;
        }

        TEST_F(CmDocumentsTest, aWrongFormatIsRefused)
        {
            const auto rigPath = writeRigFile("a", makeLimitsRig());
            const auto profilePath = writeProfileFile("a", makeProfile());

            std::string error;
            // A profile file where a rig is expected, and the other way round.
            EXPECT_EQ(creativemotion_editor::openRig(profilePath, error), nullptr);
            EXPECT_FALSE(error.empty());
            error.clear();
            EXPECT_EQ(creativemotion_editor::openProfile(rigPath, error), nullptr);
            EXPECT_FALSE(error.empty());

            // The right content with the format string changed.
            std::string text = readAll(rigPath);
            const std::size_t at = text.find("\"opas.rig\"");
            ASSERT_NE(at, std::string::npos);
            text.replace(at, std::string("\"opas.rig\"").size(), "\"opas.other\"");
            const auto wrong = mDir / "wrong.rig.json";
            writeText(wrong, text);
            error.clear();
            EXPECT_EQ(creativemotion_editor::openRig(wrong, error), nullptr);
            EXPECT_FALSE(error.empty());
        }

        TEST_F(CmDocumentsTest, anInvalidRigIsRefusedWithALocatedMessage)
        {
            rig::Rig value = makeLimitsRig();
            value.joints.front().bone = "NoSuchBone"; // a joint on a bone that does not exist
            const auto path = writeRigFile("invalid", value);

            std::string error;
            EXPECT_EQ(creativemotion_editor::openRig(path, error), nullptr);
            EXPECT_FALSE(error.empty());
            EXPECT_TRUE(contains(error, "joints") || contains(error, "invalid.rig.json")) << error;
        }

        TEST_F(CmDocumentsTest, anInvalidProfileIsRefused)
        {
            std::string text = rig::writeProfile(makeProfile());
            const std::string needle = "\"maxTorqueNm\": 500";
            const std::size_t at = text.find(needle);
            ASSERT_NE(at, std::string::npos) << text;
            text.replace(at, needle.size(), "\"maxTorqueNm\": -5");
            const auto path = mDir / "invalid.profile.json";
            writeText(path, text);

            std::string error;
            EXPECT_EQ(creativemotion_editor::openProfile(path, error), nullptr);
            EXPECT_FALSE(error.empty());
        }

        // ---- rig properties -------------------------------------------------------------------------------

        TEST_F(CmDocumentsTest, rigSummaryPropertiesAreReadOnlyAndEqualTheRig)
        {
            const rig::Rig value = makeLimitsRig();
            auto document = openRig(writeRigFile("props", value));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();
            const std::vector<PropertyDesc> descs = source.describe();

            const PropertyDesc* name = findDesc(descs, "rig.name");
            ASSERT_NE(name, nullptr);
            EXPECT_EQ(name->type, PropertyType::String);
            EXPECT_TRUE(name->readOnly);
            EXPECT_EQ(name->group, "Rig");
            EXPECT_EQ(std::get<std::string>(source.get("rig.name")), value.name);

            const struct
            {
                const char* id;
                std::size_t expected;
            } counts[] = { { "rig.bones", value.skeleton.bones.size() }, { "rig.bodies", value.bodies.size() },
                { "rig.joints", value.joints.size() } };
            for (const auto& count : counts)
            {
                const PropertyDesc* desc = findDesc(descs, count.id);
                ASSERT_NE(desc, nullptr) << count.id;
                EXPECT_EQ(desc->type, PropertyType::Int) << count.id;
                EXPECT_TRUE(desc->readOnly) << count.id;
                EXPECT_EQ(desc->group, "Rig") << count.id;
                EXPECT_EQ(std::get<std::int64_t>(source.get(count.id)), static_cast<std::int64_t>(count.expected));
            }
        }

        TEST_F(CmDocumentsTest, everyJointHasSixFloatPropertiesInRadiansWithTheRigsValues)
        {
            const rig::Rig value = makeLimitsRig();
            auto document = openRig(writeRigFile("joints", value));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();
            const std::vector<PropertyDesc> descs = source.describe();

            for (const rig::Joint& joint : value.joints)
            {
                const struct
                {
                    const char* field;
                    double expected;
                } fields[] = { { "planeSwingMinRad", joint.planeSwingMinRad },
                    { "planeSwingMaxRad", joint.planeSwingMaxRad }, { "normalSwingMinRad", joint.normalSwingMinRad },
                    { "normalSwingMaxRad", joint.normalSwingMaxRad }, { "twistMinRad", joint.twistMinRad },
                    { "twistMaxRad", joint.twistMaxRad } };
                for (const auto& field : fields)
                {
                    const std::string id = "joint." + joint.bone + "." + field.field;
                    const PropertyDesc* desc = findDesc(descs, id);
                    ASSERT_NE(desc, nullptr) << id;
                    EXPECT_EQ(desc->type, PropertyType::Float) << id;
                    EXPECT_EQ(desc->unit, "rad") << id;
                    EXPECT_FALSE(desc->readOnly) << id;
                    EXPECT_EQ(desc->group, joint.bone) << id;
                    EXPECT_DOUBLE_EQ(desc->min, -kPi) << id;
                    EXPECT_DOUBLE_EQ(desc->max, kPi) << id;
                    EXPECT_EQ(getDouble(source, id), field.expected) << id;
                }

                const std::string swingId = "joint." + joint.bone + ".swing";
                const PropertyDesc* swing = findDesc(descs, swingId);
                ASSERT_NE(swing, nullptr) << swingId;
                EXPECT_EQ(swing->type, PropertyType::String);
                EXPECT_TRUE(swing->readOnly);
                EXPECT_EQ(swing->group, joint.bone);
                EXPECT_EQ(std::get<std::string>(source.get(swingId)),
                    joint.swing == rig::SwingShape::Cone ? "cone" : "pyramid");
            }
        }

        TEST_F(CmDocumentsTest, aConeStoresItsHalfAnglesAsMinusAndPlus)
        {
            auto document = openRig(writeRigFile("cone", makeLimitsRig()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();
            EXPECT_EQ(getDouble(source, "joint.Spine.planeSwingMinRad"), -0.4);
            EXPECT_EQ(getDouble(source, "joint.Spine.planeSwingMaxRad"), 0.4);
            EXPECT_EQ(getDouble(source, "joint.Spine.normalSwingMinRad"), -0.7);
            EXPECT_EQ(getDouble(source, "joint.Spine.normalSwingMaxRad"), 0.7);
            EXPECT_EQ(std::get<std::string>(source.get("joint.Spine.swing")), "cone");
            EXPECT_EQ(std::get<std::string>(source.get("joint.LeftThigh.swing")), "pyramid");
        }

        TEST_F(CmDocumentsTest, noJointPropertiesExistForABoneWithoutAJoint)
        {
            auto document = openRig(writeRigFile("nojoint", makeLimitsRig()));
            ASSERT_NE(document, nullptr);
            const std::vector<PropertyDesc> descs = document->properties().describe();
            EXPECT_EQ(findDesc(descs, "joint.Pelvis.twistMinRad"), nullptr);
            // Ids are unique.
            std::vector<std::string> ids;
            for (const PropertyDesc& desc : descs)
                ids.push_back(desc.id);
            std::sort(ids.begin(), ids.end());
            EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
        }

        // ---- rig edits ------------------------------------------------------------------------------------

        TEST_F(CmDocumentsTest, aValidRigEditChangesNothingUntilRedoAndUndoRestoresTheExactValue)
        {
            auto document = openRig(writeRigFile("edit", makeLimitsRig()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();
            const double before = getDouble(source, "joint.LeftThigh.planeSwingMaxRad");

            auto command = source.makeSetCommand("joint.LeftThigh.planeSwingMaxRad", PropertyValue(1.0));
            ASSERT_NE(command, nullptr);
            EXPECT_FALSE(command->label().empty());
            EXPECT_EQ(getDouble(source, "joint.LeftThigh.planeSwingMaxRad"), before);
            EXPECT_FALSE(document->isDirty());

            command->redo();
            EXPECT_EQ(getDouble(source, "joint.LeftThigh.planeSwingMaxRad"), 1.0);
            EXPECT_TRUE(document->isDirty());

            command->undo();
            EXPECT_EQ(getDouble(source, "joint.LeftThigh.planeSwingMaxRad"), before);
            command->redo();
            EXPECT_EQ(getDouble(source, "joint.LeftThigh.planeSwingMaxRad"), 1.0);
        }

        TEST_F(CmDocumentsTest, twistOfAConeAndAHingeCanBeEdited)
        {
            auto document = openRig(writeRigFile("twist", makeLimitsRig()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();

            auto cone = source.makeSetCommand("joint.Spine.twistMaxRad", PropertyValue(0.5));
            ASSERT_NE(cone, nullptr);
            cone->redo();
            EXPECT_EQ(getDouble(source, "joint.Spine.twistMaxRad"), 0.5);

            auto hinge = source.makeSetCommand("joint.LeftShin.twistMaxRad", PropertyValue(kPi));
            ASSERT_NE(hinge, nullptr);
            hinge->redo();
            EXPECT_EQ(getDouble(source, "joint.LeftShin.twistMaxRad"), kPi);
        }

        TEST_F(CmDocumentsTest, editsOfTheSamePropertyMergeIntoOneStepThatUndoesToTheOldestValue)
        {
            auto document = openRig(writeRigFile("merge", makeLimitsRig()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();
            const double oldest = getDouble(source, "joint.LeftThigh.planeSwingMaxRad");

            auto first = source.makeSetCommand("joint.LeftThigh.planeSwingMaxRad", PropertyValue(1.0));
            ASSERT_NE(first, nullptr);
            first->redo();
            auto second = source.makeSetCommand("joint.LeftThigh.planeSwingMaxRad", PropertyValue(0.9));
            ASSERT_NE(second, nullptr);
            second->redo();

            ASSERT_GE(first->mergeId(), 0);
            EXPECT_EQ(first->mergeId(), second->mergeId());
            EXPECT_TRUE(first->mergeWith(*second));
            EXPECT_EQ(getDouble(source, "joint.LeftThigh.planeSwingMaxRad"), 0.9);

            first->undo();
            EXPECT_EQ(getDouble(source, "joint.LeftThigh.planeSwingMaxRad"), oldest);
            first->redo();
            EXPECT_EQ(getDouble(source, "joint.LeftThigh.planeSwingMaxRad"), 0.9);
        }

        TEST_F(CmDocumentsTest, editsOfDifferentPropertiesHaveDifferentMergeIds)
        {
            auto document = openRig(writeRigFile("nomerge", makeLimitsRig()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();

            auto planeMax = source.makeSetCommand("joint.LeftThigh.planeSwingMaxRad", PropertyValue(1.0));
            auto normalMax = source.makeSetCommand("joint.LeftThigh.normalSwingMaxRad", PropertyValue(0.5));
            auto otherJoint = source.makeSetCommand("joint.Spine.twistMaxRad", PropertyValue(0.5));
            auto sameFieldOtherJoint = source.makeSetCommand("joint.LeftShin.twistMaxRad", PropertyValue(1.5));
            ASSERT_NE(planeMax, nullptr);
            ASSERT_NE(normalMax, nullptr);
            ASSERT_NE(otherJoint, nullptr);
            ASSERT_NE(sameFieldOtherJoint, nullptr);

            EXPECT_NE(planeMax->mergeId(), normalMax->mergeId());
            EXPECT_NE(otherJoint->mergeId(), sameFieldOtherJoint->mergeId());
            EXPECT_NE(planeMax->mergeId(), otherJoint->mergeId());

            planeMax->redo();
            normalMax->redo();
            // Even if a host asked, a different property is not absorbed.
            EXPECT_FALSE(planeMax->mergeWith(*normalMax));
        }

        TEST_F(CmDocumentsTest, badRigEditsAreRefusedAndChangeNothing)
        {
            auto document = openRig(writeRigFile("refuse", makeLimitsRig()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();
            const std::string thighMax = "joint.LeftThigh.planeSwingMaxRad";
            const std::string thighMin = "joint.LeftThigh.planeSwingMinRad";
            const double thighMaxBefore = getDouble(source, thighMax);

            // Not finite.
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(kNaN)), nullptr);
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(kInf)), nullptr);
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(-kInf)), nullptr);
            // Wrong type.
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(std::string("1.0"))), nullptr);
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(true)), nullptr);
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(std::int64_t{ 1 })), nullptr);
            // Unknown ids.
            EXPECT_EQ(source.makeSetCommand("joint.NoSuchBone.twistMinRad", PropertyValue(0.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.Pelvis.twistMinRad", PropertyValue(0.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.LeftThigh.noSuchField", PropertyValue(0.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("bogus", PropertyValue(0.0)), nullptr);
            // Read only properties.
            EXPECT_EQ(source.makeSetCommand("rig.name", PropertyValue(std::string("x"))), nullptr);
            EXPECT_EQ(source.makeSetCommand("rig.bones", PropertyValue(std::int64_t{ 9 })), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.LeftThigh.swing", PropertyValue(std::string("cone"))), nullptr);
            // A pyramid range that excludes 0.
            EXPECT_EQ(source.makeSetCommand(thighMin, PropertyValue(0.1)), nullptr);
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(-0.1)), nullptr);
            // min > max (twist of the hinge runs 0 to 2).
            EXPECT_EQ(source.makeSetCommand("joint.LeftShin.twistMinRad", PropertyValue(2.5)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.LeftShin.twistMaxRad", PropertyValue(-0.5)), nullptr);
            // A cone edit that breaks min = -max.
            EXPECT_EQ(source.makeSetCommand("joint.Spine.planeSwingMaxRad", PropertyValue(0.5)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.Spine.planeSwingMinRad", PropertyValue(-0.5)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.Spine.normalSwingMaxRad", PropertyValue(0.1)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.Spine.normalSwingMinRad", PropertyValue(-0.1)), nullptr);
            // Outside [-pi, pi].
            EXPECT_EQ(source.makeSetCommand("joint.LeftShin.twistMaxRad", PropertyValue(3.5)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.LeftThigh.twistMinRad", PropertyValue(-3.5)), nullptr);
            EXPECT_EQ(source.makeSetCommand(thighMax, PropertyValue(kPi + 0.01)), nullptr);

            EXPECT_EQ(getDouble(source, thighMax), thighMaxBefore);
            EXPECT_FALSE(document->isDirty());
        }

        // ---- rig save -------------------------------------------------------------------------------------

        TEST_F(CmDocumentsTest, anUnchangedCanonicalRigSavesByteForByte)
        {
            const auto path = writeRigFile("canonical", makeLimitsRig());
            const std::string original = readAll(path);
            auto document = openRig(path);
            ASSERT_NE(document, nullptr);

            std::string error;
            ASSERT_TRUE(document->save(error)) << error;
            EXPECT_FALSE(document->isDirty());
            EXPECT_EQ(readAll(path), original);
        }

        TEST_F(CmDocumentsTest, afterAnEditAndSaveTheFileDiffersOnlyInThatValuesLine)
        {
            const rig::Rig value = makeLimitsRig();
            const auto path = writeRigFile("saved", value);
            const std::string original = readAll(path);
            auto document = openRig(path);
            ASSERT_NE(document, nullptr);

            auto command
                = document->properties().makeSetCommand("joint.LeftThigh.planeSwingMaxRad", PropertyValue(1.0));
            ASSERT_NE(command, nullptr);
            command->redo();
            ASSERT_TRUE(document->isDirty());

            std::string error;
            ASSERT_TRUE(document->save(error)) << error;
            EXPECT_FALSE(document->isDirty());

            const std::string saved = readAll(path);
            EXPECT_NE(saved, original);
            const std::vector<std::string> before = splitLines(original);
            const std::vector<std::string> after = splitLines(saved);
            ASSERT_EQ(before.size(), after.size());
            int differing = 0;
            for (std::size_t i = 0; i < before.size(); ++i)
            {
                if (before[i] != after[i])
                {
                    ++differing;
                    EXPECT_TRUE(contains(after[i], "planeSwingMaxRad")) << after[i];
                }
            }
            EXPECT_EQ(differing, 1);

            const rig::RigLoadResult loaded = rig::loadRigFile(path);
            ASSERT_TRUE(loaded.ok());
            EXPECT_EQ(loaded.value->joints[1].planeSwingMaxRad, 1.0);
            // Everything else equals the original rig.
            rig::Rig expected = value;
            expected.joints[1].planeSwingMaxRad = 1.0;
            EXPECT_EQ(*loaded.value, expected);

            // Reopening shows the new value and a clean document.
            auto reopened = openRig(path);
            ASSERT_NE(reopened, nullptr);
            EXPECT_EQ(getDouble(reopened->properties(), "joint.LeftThigh.planeSwingMaxRad"), 1.0);
            EXPECT_FALSE(reopened->isDirty());
        }

        TEST_F(CmDocumentsTest, savingAfterUndoWritesTheOriginalValueBack)
        {
            const auto path = writeRigFile("undo", makeLimitsRig());
            const std::string original = readAll(path);
            auto document = openRig(path);
            ASSERT_NE(document, nullptr);

            auto command = document->properties().makeSetCommand("joint.Spine.twistMaxRad", PropertyValue(0.1));
            ASSERT_NE(command, nullptr);
            command->redo();
            command->undo();

            std::string error;
            ASSERT_TRUE(document->save(error)) << error;
            EXPECT_EQ(readAll(path), original);
        }

        TEST_F(CmDocumentsTest, savingAnEditedRigToAVanishedFolderReportsAnError)
        {
            const auto folder = mDir / "gone";
            std::filesystem::create_directories(folder);
            const auto path = folder / "x.rig.json";
            writeText(path, rig::writeRig(makeLimitsRig()));
            auto document = openRig(path);
            ASSERT_NE(document, nullptr);

            auto command = document->properties().makeSetCommand("joint.Spine.twistMaxRad", PropertyValue(0.1));
            ASSERT_NE(command, nullptr);
            command->redo();

            std::filesystem::remove_all(folder);
            std::string error;
            EXPECT_FALSE(document->save(error));
            EXPECT_FALSE(error.empty());
        }

        // ---- profile properties ---------------------------------------------------------------------------

        TEST_F(CmDocumentsTest, profilePropertiesAreTheRigNameAndTheMotorGains)
        {
            const rig::Profile value = makeProfile();
            auto document = openProfile(writeProfileFile("props", value));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();
            const std::vector<PropertyDesc> descs = source.describe();

            const PropertyDesc* rigName = findDesc(descs, "profile.rig");
            ASSERT_NE(rigName, nullptr);
            EXPECT_EQ(rigName->type, PropertyType::String);
            EXPECT_TRUE(rigName->readOnly);
            EXPECT_EQ(rigName->group, "Profile");
            EXPECT_EQ(std::get<std::string>(source.get("profile.rig")), value.rig);

            struct Gains
            {
                std::string prefix;
                std::string group;
                rig::MotorGains gains;
            };
            const std::vector<Gains> sets = { { "default.", "Default motor", value.defaultMotor },
                { "joint.Spine.", "Spine", value.joints[0].motor },
                { "joint.LeftShin.", "LeftShin", value.joints[1].motor } };
            for (const Gains& set : sets)
            {
                const struct
                {
                    const char* field;
                    const char* unit;
                    double expected;
                } fields[] = { { "stiffnessNmPerRad", "N*m/rad", set.gains.stiffnessNmPerRad },
                    { "dampingNmsPerRad", "N*m*s/rad", set.gains.dampingNmsPerRad },
                    { "maxTorqueNm", "N*m", set.gains.maxTorqueNm } };
                for (const auto& field : fields)
                {
                    const std::string id = set.prefix + field.field;
                    const PropertyDesc* desc = findDesc(descs, id);
                    ASSERT_NE(desc, nullptr) << id;
                    EXPECT_EQ(desc->type, PropertyType::Float) << id;
                    EXPECT_EQ(desc->unit, field.unit) << id;
                    EXPECT_EQ(desc->group, set.group) << id;
                    EXPECT_FALSE(desc->readOnly) << id;
                    EXPECT_GE(desc->min, 0.0) << id;
                    EXPECT_FALSE(desc->tooltip.empty()) << id;
                    EXPECT_EQ(getDouble(source, id), field.expected) << id;
                }
            }
            // The stiffness and damping ranges start at 0.
            EXPECT_EQ(findDesc(descs, "default.stiffnessNmPerRad")->min, 0.0);
            EXPECT_EQ(findDesc(descs, "default.dampingNmsPerRad")->min, 0.0);
            // A bone without an override has no joint properties.
            EXPECT_EQ(findDesc(descs, "joint.LeftThigh.maxTorqueNm"), nullptr);
        }

        // ---- profile edits --------------------------------------------------------------------------------

        TEST_F(CmDocumentsTest, aValidProfileEditChangesNothingUntilRedoAndUndoRestores)
        {
            auto document = openProfile(writeProfileFile("edit", makeProfile()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();

            auto command = source.makeSetCommand("joint.Spine.stiffnessNmPerRad", PropertyValue(700.0));
            ASSERT_NE(command, nullptr);
            EXPECT_FALSE(command->label().empty());
            EXPECT_EQ(getDouble(source, "joint.Spine.stiffnessNmPerRad"), 600.0);
            EXPECT_FALSE(document->isDirty());

            command->redo();
            EXPECT_EQ(getDouble(source, "joint.Spine.stiffnessNmPerRad"), 700.0);
            EXPECT_TRUE(document->isDirty());
            command->undo();
            EXPECT_EQ(getDouble(source, "joint.Spine.stiffnessNmPerRad"), 600.0);
            command->redo();
            EXPECT_EQ(getDouble(source, "joint.Spine.stiffnessNmPerRad"), 700.0);

            // Zero is allowed for stiffness and damping.
            auto zeroStiffness = source.makeSetCommand("default.stiffnessNmPerRad", PropertyValue(0.0));
            auto zeroDamping = source.makeSetCommand("default.dampingNmsPerRad", PropertyValue(0.0));
            EXPECT_NE(zeroStiffness, nullptr);
            EXPECT_NE(zeroDamping, nullptr);
        }

        TEST_F(CmDocumentsTest, profileEditsOfTheSamePropertyMergeAndDifferentOnesDoNot)
        {
            auto document = openProfile(writeProfileFile("merge", makeProfile()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();

            auto first = source.makeSetCommand("default.maxTorqueNm", PropertyValue(550.0));
            ASSERT_NE(first, nullptr);
            first->redo();
            auto second = source.makeSetCommand("default.maxTorqueNm", PropertyValue(560.0));
            ASSERT_NE(second, nullptr);
            second->redo();
            auto other = source.makeSetCommand("default.dampingNmsPerRad", PropertyValue(31.0));
            auto otherJoint = source.makeSetCommand("joint.Spine.maxTorqueNm", PropertyValue(910.0));
            ASSERT_NE(other, nullptr);
            ASSERT_NE(otherJoint, nullptr);

            ASSERT_GE(first->mergeId(), 0);
            EXPECT_EQ(first->mergeId(), second->mergeId());
            EXPECT_NE(first->mergeId(), other->mergeId());
            EXPECT_NE(first->mergeId(), otherJoint->mergeId());
            EXPECT_TRUE(first->mergeWith(*second));
            EXPECT_EQ(getDouble(source, "default.maxTorqueNm"), 560.0);

            first->undo();
            EXPECT_EQ(getDouble(source, "default.maxTorqueNm"), 500.0);
            first->redo();
            EXPECT_EQ(getDouble(source, "default.maxTorqueNm"), 560.0);
        }

        TEST_F(CmDocumentsTest, badProfileEditsAreRefused)
        {
            auto document = openProfile(writeProfileFile("refuse", makeProfile()));
            ASSERT_NE(document, nullptr);
            PropertySource& source = document->properties();

            // Ranges: maxTorqueNm > 0, the others >= 0.
            EXPECT_EQ(source.makeSetCommand("default.maxTorqueNm", PropertyValue(0.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("default.maxTorqueNm", PropertyValue(-1.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.Spine.maxTorqueNm", PropertyValue(0.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("default.stiffnessNmPerRad", PropertyValue(-0.001)), nullptr);
            EXPECT_EQ(source.makeSetCommand("default.dampingNmsPerRad", PropertyValue(-1.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.LeftShin.stiffnessNmPerRad", PropertyValue(-5.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.LeftShin.dampingNmsPerRad", PropertyValue(-5.0)), nullptr);
            // Not finite.
            EXPECT_EQ(source.makeSetCommand("default.stiffnessNmPerRad", PropertyValue(kNaN)), nullptr);
            EXPECT_EQ(source.makeSetCommand("default.stiffnessNmPerRad", PropertyValue(kInf)), nullptr);
            EXPECT_EQ(source.makeSetCommand("default.maxTorqueNm", PropertyValue(kInf)), nullptr);
            // Wrong type.
            EXPECT_EQ(source.makeSetCommand("default.stiffnessNmPerRad", PropertyValue(std::string("5"))), nullptr);
            EXPECT_EQ(source.makeSetCommand("default.stiffnessNmPerRad", PropertyValue(true)), nullptr);
            // Unknown ids and read only ones.
            EXPECT_EQ(source.makeSetCommand("joint.NoSuchBone.maxTorqueNm", PropertyValue(5.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("joint.LeftThigh.maxTorqueNm", PropertyValue(5.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("default.bogus", PropertyValue(5.0)), nullptr);
            EXPECT_EQ(source.makeSetCommand("profile.rig", PropertyValue(std::string("other"))), nullptr);

            EXPECT_EQ(getDouble(source, "default.maxTorqueNm"), 500.0);
            EXPECT_FALSE(document->isDirty());
        }

        // ---- profile save ---------------------------------------------------------------------------------

        TEST_F(CmDocumentsTest, anUnchangedCanonicalProfileSavesByteForByte)
        {
            const auto path = writeProfileFile("canonical", makeProfile());
            const std::string original = readAll(path);
            auto document = openProfile(path);
            ASSERT_NE(document, nullptr);

            std::string error;
            ASSERT_TRUE(document->save(error)) << error;
            EXPECT_FALSE(document->isDirty());
            EXPECT_EQ(readAll(path), original);
        }

        TEST_F(CmDocumentsTest, afterAProfileEditAndSaveTheFileDiffersOnlyInThatLine)
        {
            const rig::Profile value = makeProfile();
            const auto path = writeProfileFile("saved", value);
            const std::string original = readAll(path);
            auto document = openProfile(path);
            ASSERT_NE(document, nullptr);

            auto command = document->properties().makeSetCommand("joint.LeftShin.dampingNmsPerRad", PropertyValue(8.5));
            ASSERT_NE(command, nullptr);
            command->redo();

            std::string error;
            ASSERT_TRUE(document->save(error)) << error;
            EXPECT_FALSE(document->isDirty());

            const std::vector<std::string> before = splitLines(original);
            const std::vector<std::string> after = splitLines(readAll(path));
            ASSERT_EQ(before.size(), after.size());
            int differing = 0;
            for (std::size_t i = 0; i < before.size(); ++i)
                if (before[i] != after[i])
                    ++differing;
            EXPECT_EQ(differing, 1);

            const rig::ProfileLoadResult loaded = rig::loadProfileFile(path);
            ASSERT_TRUE(loaded.ok());
            rig::Profile expected = value;
            expected.joints[1].motor.dampingNmsPerRad = 8.5;
            EXPECT_EQ(*loaded.value, expected);

            // The default motor too.
            auto reopened = openProfile(path);
            ASSERT_NE(reopened, nullptr);
            EXPECT_EQ(getDouble(reopened->properties(), "joint.LeftShin.dampingNmsPerRad"), 8.5);
            auto defaultEdit = reopened->properties().makeSetCommand("default.stiffnessNmPerRad", PropertyValue(320.0));
            ASSERT_NE(defaultEdit, nullptr);
            defaultEdit->redo();
            ASSERT_TRUE(reopened->save(error)) << error;
            const rig::ProfileLoadResult again = rig::loadProfileFile(path);
            ASSERT_TRUE(again.ok());
            EXPECT_EQ(again.value->defaultMotor.stiffnessNmPerRad, 320.0);
        }

        TEST_F(CmDocumentsTest, savingAnEditedProfileToAVanishedFolderReportsAnError)
        {
            const auto folder = mDir / "gone";
            std::filesystem::create_directories(folder);
            const auto path = folder / "x.profile.json";
            writeText(path, rig::writeProfile(makeProfile()));
            auto document = openProfile(path);
            ASSERT_NE(document, nullptr);

            auto command = document->properties().makeSetCommand("default.stiffnessNmPerRad", PropertyValue(1.0));
            ASSERT_NE(command, nullptr);
            command->redo();

            std::filesystem::remove_all(folder);
            std::string error;
            EXPECT_FALSE(document->save(error));
            EXPECT_FALSE(error.empty());
        }
    }
}
