#include <gtest/gtest.h>

#include <components/contentsource/plugin.hpp>

#include <memory>
#include <sstream>
#include <string>

#include "pluginbuilder.hpp"

// Task 0083 review fix (finding 3): the game's loader used to choose the reader from the first four bytes alone. The
// strict header parse of Content::Plugin::open must not be what stands between a file and the typed reader that
// decides about it.
namespace
{
    using namespace ContentTests;

    std::optional<Content::Family> probe(const std::string& bytes)
    {
        std::istringstream stream(bytes, std::ios::binary);
        return Content::probeFamily(stream);
    }

    // A TES3 plugin whose HEDR subrecord is `hedrSize` bytes long.
    Bytes tes3WithHedr(std::size_t hedrSize)
    {
        Bytes hedr;
        hedr.fixed("", hedrSize);
        return record3("TES3", 0, sub3("HEDR", hedr));
    }

    TEST(ContentProbeFamily, tells_tes3_from_tes4_by_the_first_four_bytes)
    {
        EXPECT_EQ(probe(tes3Header(1.3f, 0, "a", "d", 0, {}).str()), Content::Family::Tes3);
        EXPECT_EQ(probe(tes4Header(1.7f, 0, "a", "d", 0, {}).str()), Content::Family::Tes4);
    }

    TEST(ContentProbeFamily, answers_for_a_tes3_file_with_a_short_hedr_that_open_refuses)
    {
        const Bytes shortHedr = tes3WithHedr(10);
        Content::Error error;
        EXPECT_EQ(Content::Plugin::open(
                      std::make_unique<std::istringstream>(shortHedr.str(), std::ios::binary), "short.esp", error),
            nullptr);
        EXPECT_NE(error.mMessage.find("HEDR"), std::string::npos) << error.describe();

        EXPECT_EQ(probe(shortHedr.str()), Content::Family::Tes3);
    }

    TEST(ContentProbeFamily, answers_for_a_tes3_file_with_a_cut_off_header_record)
    {
        const std::string whole = tes3Header(1.3f, 0, "a", "d", 0, {}).str();
        EXPECT_EQ(probe(whole.substr(0, 20)), Content::Family::Tes3);
    }

    TEST(ContentProbeFamily, refuses_what_is_neither)
    {
        EXPECT_EQ(probe(""), std::nullopt);
        EXPECT_EQ(probe("TES"), std::nullopt);
        EXPECT_EQ(probe("OMWA...."), std::nullopt);
        EXPECT_EQ(probe("tes3...."), std::nullopt);
    }

    TEST(ContentProbeFamily, rewinds_a_stream_that_was_already_read)
    {
        const std::string bytes = tes4Header(1.7f, 0, "a", "d", 0, {}).str();
        std::istringstream stream(bytes, std::ios::binary);
        stream.seekg(0, std::ios::end);
        EXPECT_EQ(Content::probeFamily(stream), Content::Family::Tes4);
    }
}
