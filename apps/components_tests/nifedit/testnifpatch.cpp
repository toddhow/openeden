#include <components/nifedit/nifindex.hpp>
#include <components/nifedit/nifpatch.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <random>

#include "nifeditsupport.hpp"

// Task 0046 stage b: patches to named fields. The expected bytes are made here, from the builder's own offsets and an
// independent encoding, so the tests do not trust the library's locator.
namespace NifEdit
{
    namespace
    {
        using Testing::Built;
        using Testing::MwBuilder;

        template <class T>
        std::string raw(const T& value)
        {
            return std::string(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        std::string sized(const std::string& text)
        {
            return raw(static_cast<std::uint32_t>(text.size())) + text;
        }

        // The file after replacing [begin, end) of the original with the given bytes.
        std::string spliced(
            const Built& built, const std::pair<std::size_t, std::size_t>& range, const std::string& with)
        {
            return built.mBytes.substr(0, range.first) + with + built.mBytes.substr(range.second);
        }

        struct Case
        {
            int mRecord;
            const char* mField;
            PatchValue mValue;
            std::string mEncoded;
        };

        std::vector<Case> cases()
        {
            return {
                { 2, "name", std::string("Renamed Node With A Longer Name"), sized("Renamed Node With A Longer Name") },
                { 2, "name", std::string("x"), sized("x") },
                { 2, "name", std::string(""), sized("") },
                { 1, "flags", std::int64_t(0x0A0B), raw(std::uint16_t(0x0A0B)) },
                { 2, "translation", std::array<float, 3>{ 4.f, -5.5f, 6.25f }, raw(4.f) + raw(-5.5f) + raw(6.25f) },
                { 2, "rotation", std::array<float, 9>{ 1, 0, 0, 0, 1, 0, 0, 0, 1 },
                    raw(1.f) + raw(0.f) + raw(0.f) + raw(0.f) + raw(1.f) + raw(0.f) + raw(0.f) + raw(0.f) + raw(1.f) },
                { 2, "scale", 2.25, raw(2.25f) },
                { 1, "name", std::string("Shape2"), sized("Shape2") },
                { 4, "name", std::string("Metal"), sized("Metal") },
                { 4, "flags", std::int64_t(0x0002), raw(std::uint16_t(2)) },
                { 4, "ambient", std::array<float, 3>{ 1.f, 0.5f, 0.25f }, raw(1.f) + raw(0.5f) + raw(0.25f) },
                { 4, "diffuse", std::array<float, 3>{ 0.125f, 0.25f, 0.5f }, raw(0.125f) + raw(0.25f) + raw(0.5f) },
                { 4, "specular", std::array<float, 3>{ 0.f, 0.f, 1.f }, raw(0.f) + raw(0.f) + raw(1.f) },
                { 4, "emissive", std::array<float, 3>{ 0.75f, 0.75f, 0.75f }, raw(0.75f) + raw(0.75f) + raw(0.75f) },
                { 4, "glossiness", 50.0, raw(50.f) },
                { 4, "alpha", 0.5, raw(0.5f) },
                { 5, "flags", std::int64_t(0x0001), raw(std::uint16_t(1)) },
                { 5, "threshold", std::int64_t(200), raw(std::uint8_t(200)) },
                { 6, "name", std::string("TexName"), sized("TexName") },
                { 6, "file", std::string("textures\\other\\much_longer_texture_name.dds"),
                    sized("textures\\other\\much_longer_texture_name.dds") },
                { 7, "string", std::string("NiOptimizeKeep and more"), sized("NiOptimizeKeep and more") },
                { 9, "flags", std::int64_t(0x0010), raw(std::uint16_t(0x10)) },
                { 10, "flags", std::int64_t(0x0003), raw(std::uint16_t(3)) },
                { 11, "flags", std::int64_t(0x0000), raw(std::uint16_t(0)) },
                { 12, "flags", std::int64_t(0x0001), raw(std::uint16_t(1)) },
                { 13, "flags", std::int64_t(0x0000), raw(std::uint16_t(0)) },
                { 14, "flags", std::int64_t(0x0000), raw(std::uint16_t(0)) },
                { 8, "keys",
                    std::vector<TextKey>{ { 0.f, "walk: Start" }, { 0.5f, "walk: Loop Start" }, { 1.f, "walk: Stop" },
                        { 1.25f, "sound: Left" }, { 2.f, "" } },
                    raw(std::uint32_t(5)) + raw(0.f) + sized("walk: Start") + raw(0.5f) + sized("walk: Loop Start")
                        + raw(1.f) + sized("walk: Stop") + raw(1.25f) + sized("sound: Left") + raw(2.f) + sized("") },
                { 8, "keys", std::vector<TextKey>{}, raw(std::uint32_t(0)) },
            };
        }

        class NifEditPatch : public ::testing::TestWithParam<std::uint32_t>
        {
        };

        TEST_P(NifEditPatch, every_editable_field_set_to_its_own_value_writes_the_same_bytes)
        {
            const Built built = MwBuilder::make(GetParam());
            const FileIndex index = indexFile(built.mBytes);
            PatchSet patches;
            std::size_t fields = 0;
            for (const IndexedRecord& record : index.mRecords)
            {
                for (const std::string& field : editableFields(record.mType, index.mVersion))
                {
                    patches.set({ record.mIndex, field, readField(built.mBytes, index, record.mIndex, field) });
                    ++fields;
                }
            }
            EXPECT_GT(fields, 40u);
            EXPECT_EQ(applyPatches(built.mBytes, index, patches), built.mBytes);
        }

        TEST_P(NifEditPatch, a_patch_changes_its_field_and_no_other_byte)
        {
            const Built built = MwBuilder::make(GetParam());
            const FileIndex index = indexFile(built.mBytes);
            for (const Case& item : cases())
            {
                SCOPED_TRACE(std::to_string(item.mRecord) + ":" + item.mField);
                PatchSet patches;
                patches.set({ static_cast<std::uint32_t>(item.mRecord), item.mField, item.mValue });
                const std::string out = applyPatches(built.mBytes, index, patches);
                std::string expected = spliced(built, built.field(item.mRecord, item.mField), item.mEncoded);
                if (item.mRecord == 7)
                {
                    // "Bytes remaining" in front of the string follows it (4 + the characters).
                    const std::size_t at = built.field(7, "string").first - 4;
                    expected.replace(at, 4, raw(static_cast<std::uint32_t>(item.mEncoded.size())));
                }
                EXPECT_EQ(out, expected);
            }
        }

        TEST_P(NifEditPatch, the_engine_reader_reads_the_edited_file_with_only_that_record_changed)
        {
            const Built built = MwBuilder::make(GetParam());
            const FileIndex index = indexFile(built.mBytes);
            const std::vector<std::string> before = Testing::dumpAll(built.mBytes);
            for (const Case& item : cases())
            {
                SCOPED_TRACE(std::to_string(item.mRecord) + ":" + item.mField);
                PatchSet patches;
                patches.set({ static_cast<std::uint32_t>(item.mRecord), item.mField, item.mValue });
                const std::string out = applyPatches(built.mBytes, index, patches);
                const std::vector<std::string> after = Testing::dumpAll(out);
                ASSERT_EQ(after.size(), before.size());
                for (std::size_t i = 0; i < before.size(); ++i)
                {
                    if (static_cast<int>(i) != item.mRecord)
                        EXPECT_EQ(after[i], before[i]) << "record " << i;
                }
                // The edited file indexes again, with the same records and roots.
                const FileIndex again = indexFile(out);
                EXPECT_EQ(again.mRecords.size(), index.mRecords.size());
                EXPECT_EQ(again.mRoots, index.mRoots);
            }
        }

        TEST_P(NifEditPatch, edited_values_are_what_the_engine_reader_sees)
        {
            const Built built = MwBuilder::make(GetParam());
            const FileIndex index = indexFile(built.mBytes);
            PatchSet patches;
            patches.set({ 2, "name", std::string("Renamed") });
            patches.set({ 2, "scale", 3.0 });
            patches.set({ 2, "translation", std::array<float, 3>{ 7.f, 8.f, 9.f } });
            patches.set({ 4, "alpha", 0.25 });
            patches.set({ 6, "file", std::string("textures\\new.tga") });
            patches.set({ 8, "keys", std::vector<TextKey>{ { 0.5f, "a: b" } } });
            const auto file = Testing::parseFile(applyPatches(built.mBytes, index, patches));
            const auto* node = dynamic_cast<const Nif::NiNode*>(file->mRecords[2].get());
            ASSERT_NE(node, nullptr);
            EXPECT_EQ(node->mName, "Renamed");
            EXPECT_EQ(node->mTransform.mScale, 3.f);
            EXPECT_EQ(node->mTransform.mTranslation.y, 8.f);
            EXPECT_EQ(dynamic_cast<const Nif::NiMaterialProperty*>(file->mRecords[4].get())->mAlpha, 0.25f);
            EXPECT_EQ(dynamic_cast<const Nif::NiSourceTexture*>(file->mRecords[6].get())->mFile, "textures\\new.tga");
            const auto* keys = dynamic_cast<const Nif::NiTextKeyExtraData*>(file->mRecords[8].get());
            ASSERT_NE(keys, nullptr);
            ASSERT_EQ(keys->mList.size(), 1u);
            EXPECT_EQ(keys->mList[0].mText, "a: b");
        }

        TEST_P(NifEditPatch, several_patches_in_one_record_and_in_several_records_all_apply)
        {
            const Built built = MwBuilder::make(GetParam());
            const FileIndex index = indexFile(built.mBytes);
            PatchSet patches;
            patches.set({ 4, "alpha", 0.5 });
            patches.set({ 4, "name", std::string("M") });
            patches.set({ 4, "ambient", std::array<float, 3>{ 1.f, 1.f, 1.f } });
            patches.set({ 2, "scale", 4.0 });
            std::string expected = built.mBytes;
            // From the back, so earlier offsets stay valid.
            auto replaceRange = [&](int record, const char* field, const std::string& with) {
                const auto range = built.field(record, field);
                expected.replace(range.first, range.second - range.first, with);
            };
            replaceRange(4, "alpha", raw(0.5f));
            replaceRange(4, "ambient", raw(1.f) + raw(1.f) + raw(1.f));
            replaceRange(4, "name", sized("M"));
            // record 2 comes before record 4 in the file; patch it last so that its offsets are the original's
            // only if nothing before it changed size: the name patch changed record 4, which comes after 2.
            replaceRange(2, "scale", raw(4.f));
            EXPECT_EQ(applyPatches(built.mBytes, index, patches), expected);
        }

        TEST(NifEditPatchRules, a_string_extra_datas_bytes_remaining_follows_the_string_only_when_it_was_exact)
        {
            const Built built = MwBuilder::make();
            const std::size_t at = built.field(7, "string").first - 4;
            std::uint32_t remaining;
            std::memcpy(&remaining, built.mBytes.data() + at, 4);
            EXPECT_EQ(remaining, 18u); // the builder writes it exact, as the real files have it

            // A record whose field holds something else keeps that value.
            std::string odd = built.mBytes;
            const std::uint32_t other = 77;
            std::memcpy(&odd[at], &other, 4);
            PatchSet patches;
            patches.set({ 7, "string", std::string("short") });
            const std::string out = applyPatches(odd, indexFile(odd), patches);
            std::uint32_t kept;
            std::memcpy(&kept, out.data() + at, 4);
            EXPECT_EQ(kept, 77u);
            // and an identity patch of such a record is still byte for byte the file.
            PatchSet same;
            same.set({ 7, "string", std::string("NiOptimizeKeep") });
            EXPECT_EQ(applyPatches(odd, indexFile(odd), same), odd);
        }

        TEST(NifEditPatchRules, the_last_patch_of_a_field_wins_and_erase_removes_it)
        {
            PatchSet patches;
            patches.set({ 2, "scale", 1.0 });
            patches.set({ 2, "scale", 2.0 });
            ASSERT_EQ(patches.patches().size(), 1u);
            EXPECT_EQ(std::get<double>(patches.find(2, "scale")->mValue), 2.0);
            EXPECT_TRUE(patches.erase(2, "scale"));
            EXPECT_FALSE(patches.erase(2, "scale"));
            EXPECT_TRUE(patches.empty());
        }

        struct Refusal
        {
            int mRecord;
            const char* mField;
            PatchValue mValue;
            const char* mWhy;
        };

        TEST(NifEditPatchRules, what_cannot_be_written_exactly_is_refused_with_the_record_and_field_named)
        {
            const Built built = MwBuilder::make();
            const FileIndex index = indexFile(built.mBytes);
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const std::vector<Refusal> refusals = {
                { 3, "name", std::string("x"), "not supported" }, // NiTriShapeData has no editable field
                { 2, "children", std::int64_t(1), "no editable field" },
                { 2, "name", 1.0, "must be text" },
                { 2, "scale", std::string("big"), "must be a number" },
                { 2, "flags", std::int64_t(70000), "outside 0..65535" },
                { 2, "flags", std::int64_t(-1), "outside 0..65535" },
                { 5, "threshold", std::int64_t(256), "outside 0..255" },
                { 2, "scale", static_cast<double>(nan), "not finite" },
                { 2, "scale", 1e300, "does not fit" },
                { 2, "translation", std::array<float, 3>{ 0.f, nan, 0.f }, "not finite" },
                { 2, "translation", 1.0, "must be three numbers" },
                { 2, "rotation", std::array<float, 3>{ 0.f, 0.f, 0.f }, "must be nine numbers" },
                { 2, "name", std::string("a\0b", 3), "zero byte" },
                { 8, "keys", std::vector<TextKey>{ { nan, "x" } }, "not finite" },
                { 8, "keys", std::vector<TextKey>{ { 0.f, std::string("a\0b", 3) } }, "zero byte" },
                { 8, "keys", std::string("not keys"), "list of text keys" },
                { 99, "name", std::string("x"), "does not exist" },
                { 7, "name", std::string("x"), "no editable field" },
            };
            for (const Refusal& item : refusals)
            {
                SCOPED_TRACE(std::to_string(item.mRecord) + ":" + item.mField);
                PatchSet patches;
                patches.set({ static_cast<std::uint32_t>(item.mRecord), item.mField, item.mValue });
                try
                {
                    (void)applyPatches(built.mBytes, index, patches);
                    ADD_FAILURE() << "the patch was accepted";
                }
                catch (const EditError& e)
                {
                    EXPECT_NE(std::string(e.what()).find(item.mWhy), std::string::npos) << e.what();
                    if (item.mRecord != 99)
                        EXPECT_NE(
                            std::string(e.what()).find("record " + std::to_string(item.mRecord)), std::string::npos)
                            << e.what();
                }
            }
        }

        TEST(NifEditPatchRules, a_texture_with_its_pixels_inside_the_file_has_no_editable_file_name)
        {
            // A NiSourceTexture whose external flag is 0, indexed by hand: only its layout up to the flag matters.
            Built built = MwBuilder::make();
            FileIndex index = indexFile(built.mBytes);
            std::string bytes = built.mBytes;
            bytes[built.field(6, "name").second + 8] = 0; // the external flag, one byte
            try
            {
                PatchSet patches;
                patches.set({ 6, "file", std::string("x") });
                (void)applyPatches(bytes, index, patches);
                ADD_FAILURE() << "accepted";
            }
            catch (const EditError& e)
            {
                EXPECT_NE(std::string(e.what()).find("pixel data"), std::string::npos) << e.what();
            }
        }

        TEST(NifEditPatchRules, only_morrowinds_versions_can_be_edited)
        {
            const Built built = MwBuilder::make();
            FileIndex index = indexFile(built.mBytes);
            index.mVersion = 0x14020007;
            PatchSet patches;
            patches.set({ 2, "scale", 2.0 });
            EXPECT_THROW(applyPatches(built.mBytes, index, patches), EditError);
            EXPECT_THROW(readField(built.mBytes, index, 2, "scale"), EditError);
        }

        TEST(NifEditPatchHostile, a_record_range_shorter_than_its_layout_is_refused_not_overread)
        {
            const Built built = MwBuilder::make();
            const FileIndex good = indexFile(built.mBytes);
            std::size_t refused = 0;
            for (const IndexedRecord& record : good.mRecords)
            {
                for (const std::string& field : editableFields(record.mType, good.mVersion))
                {
                    for (std::uint64_t cut = 1; cut < record.mEnd - record.mBodyBegin; cut += 3)
                    {
                        FileIndex index = good;
                        index.mRecords[record.mIndex].mEnd = record.mEnd - cut;
                        try
                        {
                            (void)readField(built.mBytes, index, record.mIndex, field);
                        }
                        catch (const EditError&)
                        {
                            ++refused;
                        }
                    }
                }
            }
            EXPECT_GT(refused, 100u);
        }

        TEST(NifEditPatchHostile, random_bytes_in_a_record_never_crash_the_locator)
        {
            const Built built = MwBuilder::make();
            const FileIndex index = indexFile(built.mBytes);
            std::mt19937 random(12345);
            for (int round = 0; round < 3000; ++round)
            {
                std::string bytes = built.mBytes;
                for (int hit = 0; hit < 3; ++hit)
                    bytes[random() % bytes.size()] = static_cast<char>(random());
                for (const IndexedRecord& record : index.mRecords)
                {
                    for (const std::string& field : editableFields(record.mType, index.mVersion))
                    {
                        try
                        {
                            (void)readField(bytes, index, record.mIndex, field);
                        }
                        catch (const EditError&)
                        {
                        }
                    }
                }
            }
            SUCCEED();
        }

        INSTANTIATE_TEST_SUITE_P(MorrowindVersions, NifEditPatch, ::testing::Values(0x04000002u, 0x04000000u));
    }
}
