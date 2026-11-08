#include <components/nif/exception.hpp>
#include <components/nif/niffile.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include "nifbuilder.hpp"

// Task 0082 review fix (finding 1): the process-wide default options of Nif::Reader are what the game's readers use
// through parse(stream). Other tests change them (nif/testphysics.cpp sets Any and never restores it), so these tests
// set what they need and restore the previous value, and pass in any order.
namespace Nif
{
    namespace
    {
        using Testing::Block;
        using Testing::GamebryoFile;

        constexpr VFS::Path::NormalizedView path("default-options-test.nif");

        // Owns the default options for the length of a test and puts the old ones back.
        class DefaultOptionsGuard
        {
        public:
            explicit DefaultOptionsGuard(const ReadOptions& options)
                : mSaved(Reader::getDefaultReadOptions())
            {
                Reader::setDefaultReadOptions(options);
            }
            DefaultOptionsGuard(const DefaultOptionsGuard&) = delete;
            DefaultOptionsGuard& operator=(const DefaultOptionsGuard&) = delete;
            ~DefaultOptionsGuard() { Reader::setDefaultReadOptions(mSaved); }

        private:
            ReadOptions mSaved;
        };

        std::string skyrimSeFile()
        {
            GamebryoFile file;
            Block node;
            node.mType = "NiNode";
            Testing::writeAvObject(node.mData, file.string("Root"), { 1.f, 2.f, 3.f }, 2.f);
            node.mData.put<std::uint32_t>(0); // children
            node.mData.put<std::uint32_t>(0); // effects
            file.mBlocks.push_back(std::move(node));
            return file.build();
        }

        // Parses with parse(stream): the default options decide.
        void parseWithDefaults(NIFFile& file, const std::string& bytes)
        {
            Reader reader(file, nullptr);
            auto input = std::make_unique<std::istringstream>(bytes);
            input->exceptions(std::ios::failbit | std::ios::badbit);
            reader.parse(std::move(input));
        }

        TEST(NifDefaultReadOptions, a_fresh_default_set_to_the_morrowind_options_refuses_skyrim_se)
        {
            const DefaultOptionsGuard guard{ ReadOptions{} };
            EXPECT_EQ(Reader::getDefaultReadOptions().mAllowedVersions, VersionSet::Morrowind);

            NIFFile file(path);
            try
            {
                parseWithDefaults(file, skyrimSeFile());
                FAIL() << "a Skyrim SE file was accepted by the Morrowind default";
            }
            catch (const std::exception& e)
            {
                EXPECT_NE(std::string(e.what()).find("Unsupported NIF version: 20.2.0.7"), std::string::npos)
                    << e.what();
            }
        }

        TEST(NifDefaultReadOptions, the_any_default_accepts_skyrim_se)
        {
            ReadOptions any;
            any.mAllowedVersions = VersionSet::Any;
            const DefaultOptionsGuard guard{ any };

            NIFFile file(path);
            parseWithDefaults(file, skyrimSeFile());
            EXPECT_EQ(file.mBethVersion, 100u);
            ASSERT_EQ(file.mRecords.size(), 1u);
        }

        TEST(NifDefaultReadOptions, the_guard_puts_the_previous_default_back)
        {
            ReadOptions outer;
            outer.mAllowedVersions = VersionSet::SkyrimSE;
            outer.mMorrowindRootRule = false;
            const DefaultOptionsGuard outerGuard{ outer };
            {
                const DefaultOptionsGuard inner{ ReadOptions{} };
                EXPECT_EQ(Reader::getDefaultReadOptions().mAllowedVersions, VersionSet::Morrowind);
            }
            EXPECT_EQ(Reader::getDefaultReadOptions().mAllowedVersions, VersionSet::SkyrimSE);
            EXPECT_FALSE(Reader::getDefaultReadOptions().mMorrowindRootRule);
        }
    }
}
