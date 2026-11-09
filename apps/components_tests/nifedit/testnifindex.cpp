#include <components/nifedit/nifdocument.hpp>
#include <components/nifedit/nifindex.hpp>
#include <components/nifedit/nifpatch.hpp>

#include <gtest/gtest.h>

#include "nifeditsupport.hpp"

// Task 0046 stage a: the record index and the guarantee that an unedited save equals the file that was opened.
namespace NifEdit
{
    namespace
    {
        using Testing::Built;
        using Testing::MwBuilder;

        class NifEditIndex : public ::testing::TestWithParam<std::uint32_t>
        {
        };

        TEST_P(NifEditIndex, the_ranges_tile_the_file_exactly_as_the_builder_wrote_it)
        {
            const Built built = MwBuilder::make(GetParam());
            const FileIndex index = indexFile(built.mBytes);

            EXPECT_EQ(index.mVersion, GetParam());
            EXPECT_EQ(index.mSize, built.mBytes.size());
            EXPECT_EQ(index.mHeaderEnd, built.mHeaderEnd);
            EXPECT_EQ(index.mFooterBegin, built.mFooterBegin);
            ASSERT_EQ(index.mRecords.size(), built.mRecords.size());
            for (std::size_t i = 0; i < index.mRecords.size(); ++i)
            {
                EXPECT_EQ(index.mRecords[i].mIndex, i);
                EXPECT_EQ(index.mRecords[i].mBegin, built.mRecords[i].first) << "record " << i;
                EXPECT_EQ(index.mRecords[i].mEnd, built.mRecords[i].second) << "record " << i;
                // The type name (4-byte length and the text) comes first in a 4.0.0.x record.
                EXPECT_EQ(index.mRecords[i].mBodyBegin, index.mRecords[i].mBegin + 4 + index.mRecords[i].mType.size());
            }
            EXPECT_EQ(index.mRecords[0].mType, "NiNode");
            EXPECT_EQ(index.mRecords[1].mType, "NiTriShape");
            EXPECT_EQ(index.mRecords[8].mType, "NiTextKeyExtraData");
            ASSERT_EQ(index.mRoots.size(), 1u);
            EXPECT_EQ(index.mRoots[0], 0);
        }

        TEST_P(NifEditIndex, opening_and_building_without_edits_gives_the_same_bytes)
        {
            const Built built = MwBuilder::make(GetParam());
            Document document = Document::open(built.mBytes);
            EXPECT_EQ(document.build(), built.mBytes);
            EXPECT_EQ(document.buildChecked(), built.mBytes);
            EXPECT_FALSE(document.dirty());
        }

        INSTANTIATE_TEST_SUITE_P(MorrowindVersions, NifEditIndex, ::testing::Values(0x04000002u, 0x04000000u));

        TEST(NifEditIndexReader, a_parse_with_a_layout_reads_the_same_records_as_one_without)
        {
            const Built built = MwBuilder::make();
            const auto plain = Testing::parseFile(built.mBytes);

            constexpr VFS::Path::NormalizedView path("nifedit-layout");
            Nif::NIFFile other(path);
            Nif::Reader reader(other, nullptr);
            Nif::FileLayout layout;
            reader.parse(std::make_unique<std::istringstream>(built.mBytes), Nif::ReadOptions{}, &layout);

            ASSERT_EQ(other.mRecords.size(), plain->mRecords.size());
            for (std::size_t i = 0; i < plain->mRecords.size(); ++i)
                EXPECT_EQ(Testing::dumpRecord(other, i), Testing::dumpRecord(*plain, i));
            EXPECT_EQ(layout.mRecords.size(), plain->mRecords.size());
        }

        TEST(NifEditIndexReader, the_reader_without_a_layout_still_parses_the_file)
        {
            // The two-argument parse is the game's path: it must not need a layout.
            const Built built = MwBuilder::make();
            EXPECT_EQ(Testing::parseFile(built.mBytes)->mRecords.size(), built.mRecords.size());
        }

        TEST(NifEditIndexHostile, bytes_that_are_not_a_nif_are_refused_not_crashed_on)
        {
            EXPECT_ANY_THROW(indexFile(""));
            EXPECT_ANY_THROW(indexFile("not a nif file at all"));
            const Built built = MwBuilder::make();
            for (std::size_t length = 0; length < built.mBytes.size(); length += 7)
            {
                // A truncated file must be refused, with whatever exception the reader throws.
                bool threw = false;
                try
                {
                    (void)indexFile(std::string_view(built.mBytes).substr(0, length));
                }
                catch (const std::exception&)
                {
                    threw = true;
                }
                EXPECT_TRUE(threw) << "a file cut to " << length << " bytes was indexed";
            }
        }

        TEST(NifEditIndexHostile, a_file_of_another_version_opens_for_reading_but_not_for_editing)
        {
            // 10.0.1.2 is refused by the Morrowind-only reader at once.
            std::string bytes = "NetImmerse File Format, Version 10.0.1.2\n";
            const std::uint32_t version = 0x0A000102;
            bytes.append(reinterpret_cast<const char*>(&version), 4);
            bytes.append(32, '\0');
            EXPECT_ANY_THROW(Document::open(bytes));
        }

        TEST(NifEditIndexFields, the_editable_fields_follow_the_type_and_version)
        {
            EXPECT_EQ(editableFields("NiNode", 0x04000002).size(), 5u);
            EXPECT_EQ(editableFields("NiMaterialProperty", 0x04000000).size(), 8u);
            EXPECT_TRUE(editableFields("NiTriShapeData", 0x04000002).empty());
            EXPECT_TRUE(editableFields("NiNode", 0x14020007).empty());
        }
    }
}
