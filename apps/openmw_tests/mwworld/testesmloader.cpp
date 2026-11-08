// Task 0083 review fix (finding 3): MWWorld::EsmLoader chooses the reader from a file's first four bytes, as it did
// before Content::Plugin, so a header record that Content::Plugin::open would refuse (a short HEDR) is still the typed
// reader's decision, and the message does not come from the strict parse.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>

#include "apps/components_tests/contentsource/pluginbuilder.hpp"
#include "apps/openmw/mwstate/loading.hpp"
#include "apps/openmw/mwworld/esmloader.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"

namespace
{
    struct StopLoading : MWState::Loading
    {
        bool run(float) override { return false; }
    };

    class TempFile
    {
    public:
        explicit TempFile(const std::string& bytes)
            : mPath(std::filesystem::temp_directory_path() / "openeden_esmloader_test.esp")
        {
            std::ofstream out(mPath, std::ios::binary);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        ~TempFile()
        {
            std::error_code ignored;
            std::filesystem::remove(mPath, ignored);
        }
        const std::filesystem::path& path() const { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    std::string loadError(const std::string& bytes)
    {
        const TempFile file(bytes);
        StopLoading loading;
        MWWorld::ESMStore store;
        ESM::ReadersCache readers;
        std::vector<int> versions(1, 0);
        MWWorld::EsmLoader loader(store, readers, nullptr, versions);
        int index = 0;
        try
        {
            loader.load(file.path(), index, loading);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    TEST(MWWorldEsmLoader, a_tes3_file_with_a_short_hedr_is_not_refused_by_the_strict_header_parse)
    {
        ContentTests::Bytes hedr;
        hedr.fixed("", 10);
        const ContentTests::Bytes file = ContentTests::record3("TES3", 0, ContentTests::sub3("HEDR", hedr));
        const std::string error = loadError(file.str());
        EXPECT_EQ(error.find("expected at least"), std::string::npos) << error;
    }

    TEST(MWWorldEsmLoader, a_file_that_is_neither_family_is_refused_with_its_name)
    {
        const std::string error = loadError("OMWA this is not a plugin");
        EXPECT_NE(error.find("openeden_esmloader_test.esp"), std::string::npos) << error;
        EXPECT_NE(error.find("not a TES3 or TES4 plugin"), std::string::npos) << error;
    }

    TEST(MWWorldEsmLoader, a_file_under_four_bytes_is_refused)
    {
        const std::string error = loadError("TE");
        EXPECT_NE(error.find("not a TES3 or TES4 plugin"), std::string::npos) << error;
    }
}
