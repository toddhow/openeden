#include <components/nif/data.hpp>
#include <components/nif/exception.hpp>
#include <components/nif/niffile.hpp>
#include <components/nif/node.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "nifbuilder.hpp"

// Task 0082: the reader's options and the version table. Morrowind's behaviour with the default options is pinned by
// testmorrowindunchanged.cpp.
namespace Nif
{
    namespace
    {
        using Testing::Block;
        using Testing::ByteWriter;
        using Testing::GamebryoFile;

        constexpr VFS::Path::NormalizedView path("options-test.nif");

        // A 20.2.0.7 file with one root node. Its transform is not the identity.
        GamebryoFile oneNodeFile(std::uint32_t userVersion = 12, std::uint32_t streamVersion = 100)
        {
            GamebryoFile file;
            file.mUserVersion = userVersion;
            file.mStreamVersion = streamVersion;
            Block node;
            node.mType = "NiNode";
            Testing::writeAvObject(node.mData, file.string("Root"), { 1.f, 2.f, 3.f }, 2.f);
            node.mData.put<std::uint32_t>(0); // children
            node.mData.put<std::uint32_t>(0); // effects
            file.mBlocks.push_back(std::move(node));
            return file;
        }

        void parse(NIFFile& file, const std::string& bytes, const ReadOptions& options)
        {
            Reader reader(file, nullptr);
            // The game's streams throw on a failed read; a string stream needs to be told to.
            auto input = std::make_unique<std::istringstream>(bytes);
            input->exceptions(std::ios::failbit | std::ios::badbit);
            reader.parse(std::move(input), options);
        }

        std::string errorOf(const std::string& bytes, const ReadOptions& options)
        {
            NIFFile file(path);
            try
            {
                parse(file, bytes, options);
            }
            catch (const std::exception& e)
            {
                return e.what();
            }
            return {};
        }

        ReadOptions options(VersionSet set, bool strict = false, bool rootRule = true)
        {
            ReadOptions result;
            result.mAllowedVersions = set;
            result.mStrict = strict;
            result.mMorrowindRootRule = rootRule;
            return result;
        }

        TEST(NifVersionTable, finds_rows_by_file_version_and_stream_version)
        {
            const VersionInfo* sse = findVersion(NIFStream::generateVersion(20, 2, 0, 7), 100);
            ASSERT_NE(sse, nullptr);
            EXPECT_EQ(sse->mName, "Skyrim SE");
            EXPECT_EQ(sse->mUserVersion, 12u);
            EXPECT_EQ(sse->mFirstAllowedIn, VersionSet::SkyrimSE);

            const VersionInfo* morrowind = findVersion(NIFStream::generateVersion(4, 0, 0, 2), 0);
            ASSERT_NE(morrowind, nullptr);
            EXPECT_EQ(morrowind->mFirstAllowedIn, VersionSet::Morrowind);

            const VersionInfo* fallout4 = findVersion(NIFStream::generateVersion(20, 2, 0, 7), 130);
            ASSERT_NE(fallout4, nullptr);
            EXPECT_EQ(fallout4->mFirstAllowedIn, VersionSet::Any);

            EXPECT_EQ(findVersion(NIFStream::generateVersion(20, 2, 0, 7), 99), nullptr);
            EXPECT_EQ(findVersion(NIFStream::generateVersion(3, 1, 0, 0), 0), nullptr);
        }

        TEST(NifVersionTable, is_in_a_fixed_order_with_unique_rows)
        {
            const auto table = versionTable();
            ASSERT_GE(table.size(), 6u);
            for (std::size_t i = 0; i < table.size(); ++i)
            {
                for (std::size_t j = i + 1; j < table.size(); ++j)
                {
                    EXPECT_FALSE(
                        table[i].mVersion == table[j].mVersion && table[i].mBethVersion == table[j].mBethVersion)
                        << table[i].mName << " and " << table[j].mName;
                }
            }
            EXPECT_EQ(table.front().mName, "Morrowind 4.0.0.0");
        }

        TEST(NifReadOptions, the_default_is_morrowind_with_the_rule_on)
        {
            const ReadOptions defaults;
            EXPECT_EQ(defaults.mAllowedVersions, VersionSet::Morrowind);
            EXPECT_FALSE(defaults.mStrict);
            EXPECT_TRUE(defaults.mMorrowindRootRule);
        }

        TEST(NifReadOptions, skyrim_se_is_refused_by_the_morrowind_set_before_the_blocks)
        {
            const std::string error = errorOf(oneNodeFile().build(), options(VersionSet::Morrowind));
            EXPECT_NE(error.find("Unsupported NIF version: 20.2.0.7"), std::string::npos) << error;
            EXPECT_NE(error.find("options-test.nif"), std::string::npos) << error;
        }

        TEST(NifReadOptions, skyrim_se_loads_with_the_skyrim_se_set)
        {
            NIFFile file(path);
            parse(file, oneNodeFile().build(), options(VersionSet::SkyrimSE));
            EXPECT_EQ(file.mVersion, NIFStream::generateVersion(20, 2, 0, 7));
            EXPECT_EQ(file.mUserVersion, 12u);
            EXPECT_EQ(file.mBethVersion, 100u);
            ASSERT_EQ(file.mRecords.size(), 1u);
            EXPECT_EQ(file.mRecords[0]->mRecordName, "NiNode");
            ASSERT_EQ(file.mRoots.size(), 1u);
        }

        TEST(NifReadOptions, skyrim_se_loads_with_the_any_set)
        {
            NIFFile file(path);
            parse(file, oneNodeFile().build(), options(VersionSet::Any));
            EXPECT_EQ(file.mBethVersion, 100u);
        }

        TEST(NifReadOptions, other_bethesda_streams_are_refused_by_the_skyrim_se_set_after_the_header)
        {
            // Skyrim LE: same file version, stream version 83.
            const std::string error = errorOf(oneNodeFile(12, 83).build(), options(VersionSet::SkyrimSE));
            EXPECT_NE(
                error.find("Unsupported NIF version: 20.2.0.7, user version 12, stream version 83"), std::string::npos)
                << error;
            // The same file loads with the Any set.
            EXPECT_EQ(errorOf(oneNodeFile(12, 83).build(), options(VersionSet::Any)), "");
        }

        TEST(NifReadOptions, strict_also_compares_the_user_version)
        {
            const std::string bytes = oneNodeFile(11, 100).build();
            EXPECT_EQ(errorOf(bytes, options(VersionSet::SkyrimSE, false)), "");
            const std::string error = errorOf(bytes, options(VersionSet::SkyrimSE, true));
            EXPECT_NE(error.find("user version 11"), std::string::npos) << error;
            // The right user version passes strictly.
            EXPECT_EQ(errorOf(oneNodeFile(12, 100).build(), options(VersionSet::SkyrimSE, true)), "");
        }

        TEST(NifReadOptions, the_morrowind_rule_replaces_the_first_nodes_transform_only_when_on)
        {
            {
                NIFFile file(path);
                parse(file, oneNodeFile().build(), options(VersionSet::SkyrimSE, false, true));
                const auto* node = dynamic_cast<const NiNode*>(file.mRecords[0].get());
                ASSERT_NE(node, nullptr);
                EXPECT_EQ(node->mTransform.mTranslation.x, 0.f);
                EXPECT_EQ(node->mTransform.mScale, 1.f);
            }
            {
                NIFFile file(path);
                parse(file, oneNodeFile().build(), options(VersionSet::SkyrimSE, false, false));
                const auto* node = dynamic_cast<const NiNode*>(file.mRecords[0].get());
                ASSERT_NE(node, nullptr);
                EXPECT_EQ(node->mTransform.mTranslation.x, 1.f);
                EXPECT_EQ(node->mTransform.mTranslation.y, 2.f);
                EXPECT_EQ(node->mTransform.mTranslation.z, 3.f);
                EXPECT_EQ(node->mTransform.mScale, 2.f);
            }
        }

        // Refusals: one malformed variant each. All throw a std::exception with a message; none crashes.
        TEST(NifReadOptions, a_bad_header_is_refused_with_the_file_name)
        {
            GamebryoFile file = oneNodeFile();
            file.mHeaderLine = "Not a NIF file\n";
            const std::string error = errorOf(file.build(), options(VersionSet::Any));
            EXPECT_NE(error.find("Invalid NIF header"), std::string::npos) << error;
            EXPECT_NE(error.find("options-test.nif"), std::string::npos) << error;
        }

        TEST(NifReadOptions, a_big_endian_file_is_refused)
        {
            GamebryoFile file = oneNodeFile();
            file.mEndian = 0;
            const std::string error = errorOf(file.build(), options(VersionSet::SkyrimSE));
            EXPECT_NE(error.find("Big endian"), std::string::npos) << error;
        }

        TEST(NifReadOptions, hashed_record_types_are_refused)
        {
            GamebryoFile file = oneNodeFile();
            file.mVersion = Testing::version(20, 3, 1, 2);
            file.mHeaderLine = "Gamebryo File Format, Version 20.3.1.2\n";
            const std::string error = errorOf(file.build(), options(VersionSet::Any));
            EXPECT_NE(error.find("Hashed record types"), std::string::npos) << error;
        }

        TEST(NifReadOptions, a_truncated_file_is_refused_at_every_length)
        {
            const std::string bytes = oneNodeFile().build();
            for (std::size_t length = 0; length < bytes.size(); ++length)
            {
                const std::string error = errorOf(bytes.substr(0, length), options(VersionSet::SkyrimSE));
                ASSERT_FALSE(error.empty()) << "length " << length << " of " << bytes.size();
            }
        }

        TEST(NifReadOptions, a_giant_record_count_is_refused_without_a_giant_allocation)
        {
            GamebryoFile file = oneNodeFile();
            std::string bytes = file.build();
            // The record count follows the header line (39 bytes), the version, the endianness and the user version.
            const std::size_t offset = file.mHeaderLine.size() + 4 + 1 + 4;
            const std::uint32_t giant = 0xFFFFFFF0u;
            bytes.replace(offset, 4, reinterpret_cast<const char*>(&giant), 4);
            const std::string error = errorOf(bytes, options(VersionSet::SkyrimSE));
            EXPECT_FALSE(error.empty());
        }

        // NiSkinPartition (Skyrim SE) reads its vertex count as dataSize / vertexSize.
        Block partitionWith(std::uint32_t dataSize, std::uint32_t vertexSize)
        {
            Block block;
            block.mType = "NiSkinPartition";
            block.mData.put<std::uint32_t>(0); // partitions
            block.mData.put(dataSize);
            block.mData.put(vertexSize);
            block.mData.put<std::uint64_t>(0x1ull << 44); // a descriptor with the Vertex attribute
            return block;
        }

        TEST(NifReadOptions, a_skin_partition_with_data_and_a_vertex_size_of_zero_is_refused)
        {
            GamebryoFile file = oneNodeFile();
            file.mBlocks.push_back(partitionWith(64, 0));
            const std::string error = errorOf(file.build(), options(VersionSet::SkyrimSE));
            EXPECT_NE(error.find("vertex size of zero"), std::string::npos) << error;
        }

        TEST(NifReadOptions, a_skin_partition_with_more_data_than_the_file_holds_is_refused)
        {
            GamebryoFile file = oneNodeFile();
            file.mBlocks.push_back(partitionWith(0x7FFFFFF0u, 16));
            const std::string error = errorOf(file.build(), options(VersionSet::SkyrimSE));
            EXPECT_FALSE(error.empty());
            EXPECT_NE(error.find("more than stream size"), std::string::npos) << error;
        }

        TEST(NifReadOptions, an_empty_skin_partition_is_accepted)
        {
            GamebryoFile file = oneNodeFile();
            file.mBlocks.push_back(partitionWith(0, 0));
            EXPECT_EQ(errorOf(file.build(), options(VersionSet::SkyrimSE)), "");
        }
    }
}
