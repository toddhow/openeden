#include <components/havokpack/packfile.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "packgen.hpp"

// Task 0119: the Havok packfile container reader, on packfiles our own generator writes.
namespace
{
    using namespace HavokPack;
    using HavokPackTest::PackGen;

    std::vector<std::uint8_t> sample()
    {
        PackGen g;
        const auto root = g.addObject("hkRootLevelContainer", 32);
        const auto skel = g.addObject("hkaSkeleton", 64);
        const auto name = g.addCString("Bone01");
        g.pointer(root + 0, skel);
        g.pointer(skel + 8, name);
        const auto arr = g.addBytes({ 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0 });
        g.array(skel + 16, arr, 4);
        g.putF32(skel + 40, 1.5f);
        return g.build();
    }

    template <class F>
    void expectError(F&& f, const std::string& part = {})
    {
        try
        {
            f();
            FAIL() << "no error";
        }
        catch (const Error& e)
        {
            if (!part.empty())
            {
                EXPECT_NE(std::string(e.what()).find(part), std::string::npos) << e.what();
            }
            EXPECT_NE(std::string(e.what()).find("offset"), std::string::npos) << "error has no place: " << e.what();
        }
    }
}

TEST(HavokPackfile, reads_header_sections_and_classes)
{
    const auto bytes = sample();
    const Packfile p = Packfile::parse(bytes);
    EXPECT_EQ(p.versionString(), "hk_2010.2.0-r1");
    EXPECT_EQ(p.fileVersion(), 8u);
    ASSERT_EQ(p.sections().size(), 3u);
    EXPECT_EQ(p.sections()[0].mName, "__classnames__");
    EXPECT_EQ(p.sections()[1].mName, "__types__");
    EXPECT_EQ(p.sections()[2].mName, "__data__");
    ASSERT_EQ(p.classNames().size(), 2u);
    EXPECT_EQ(p.classNames()[0], "hkRootLevelContainer");
    EXPECT_EQ(p.classNames()[1], "hkaSkeleton");
}

TEST(HavokPackfile, lists_objects_with_class_and_size)
{
    const auto bytes = sample();
    const Packfile p = Packfile::parse(bytes);
    ASSERT_EQ(p.objects().size(), 2u);
    EXPECT_EQ(p.objects()[0].mOffset, 0u);
    EXPECT_EQ(p.className(p.objects()[0]), "hkRootLevelContainer");
    EXPECT_EQ(p.objects()[0].mSize, 32u);
    EXPECT_EQ(p.objects()[1].mOffset, 32u);
    EXPECT_EQ(p.className(p.objects()[1]), "hkaSkeleton");
    // up to the end of the data (no object follows): the 64 bytes, the string and the 16 byte array that came after
    EXPECT_EQ(p.objects()[1].mSize, p.sections()[2].mDataSize - 32u);
    EXPECT_GE(p.objects()[1].mSize, 64u);
    EXPECT_EQ(p.objects()[0].mSection, 2u);
    EXPECT_EQ(p.objectsOfClass("hkaSkeleton").size(), 1u);
    EXPECT_TRUE(p.objectsOfClass("hkaNothing").empty());
    EXPECT_NE(p.findObject({ 2, 32 }), nullptr);
    EXPECT_EQ(p.findObject({ 2, 33 }), nullptr);
}

TEST(HavokPackfile, follows_local_pointers_strings_and_arrays)
{
    const auto bytes = sample();
    const Packfile p = Packfile::parse(bytes);
    const auto target = p.pointerAt({ 2, 0 });
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, (Ref{ 2, 32 }));
    const auto str = p.pointerAt({ 2, 40 });
    ASSERT_TRUE(str.has_value());
    EXPECT_EQ(p.cstring(*str), "Bone01");
    const ArrayRef arr = p.arrayAt({ 2, 48 });
    ASSERT_TRUE(arr.mData.has_value());
    EXPECT_EQ(arr.mSize, 4u);
    EXPECT_EQ(p.u32({ arr.mData->mSection, arr.mData->mOffset + 8 }), 3u);
    EXPECT_EQ(p.f32({ 2, 72 }), 1.5f);
}

TEST(HavokPackfile, a_null_pointer_with_no_fixup_is_nothing)
{
    const auto bytes = sample();
    const Packfile p = Packfile::parse(bytes);
    EXPECT_FALSE(p.pointerAt({ 2, 16 }).has_value());
}

TEST(HavokPackfile, a_global_fixup_points_into_another_section)
{
    PackGen g;
    const auto a = g.addObject("hkRootLevelContainer", 16);
    g.globalPointer(a, 2, 0);
    const auto bytes = g.build();
    const Packfile p = Packfile::parse(bytes);
    const auto t = p.pointerAt({ 2, a });
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(*t, (Ref{ 2, 0 }));
}

TEST(HavokPackfile, a_nonzero_slot_without_a_fixup_is_an_error)
{
    PackGen g;
    const auto a = g.addObject("hkRootLevelContainer", 16);
    g.put32(a, 0x1234);
    const auto bytes = g.build();
    const Packfile p = Packfile::parse(bytes);
    expectError([&] { p.pointerAt({ 2, a }); }, "fix-up");
}

TEST(HavokPackfile, reads_are_bounds_checked)
{
    const auto bytes = sample();
    const Packfile p = Packfile::parse(bytes);
    const auto size = p.sections()[2].mDataSize;
    expectError([&] { p.u32({ 2, size - 2 }); });
    expectError([&] { p.u8({ 2, size }); });
    expectError([&] { p.u8({ 7, 0 }); });
    expectError([&] { p.bytes({ 2, 0xFFFFFFF0u }, 32); });
    expectError([&] { p.bytes({ 2, 8 }, 0xFFFFFFFFu); });
}

TEST(HavokPackfile, an_unterminated_string_is_an_error)
{
    PackGen g;
    const auto a = g.addObject("hkRootLevelContainer", 16);
    g.mData.resize(g.mData.size());
    for (std::size_t i = a; i < g.mData.size(); ++i)
        g.mData[i] = 'x';
    const auto bytes = g.build();
    const Packfile p = Packfile::parse(bytes);
    expectError([&] { p.cstring({ 2, a }); }, "string");
}

TEST(HavokPackfile, refuses_a_bad_magic)
{
    auto bytes = sample();
    bytes[0] ^= 1;
    expectError([&] { Packfile::parse(bytes); }, "magic");
}

TEST(HavokPackfile, refuses_other_pointer_sizes_and_versions)
{
    auto bytes = sample();
    auto b = bytes;
    b[16] = 4;
    expectError([&] { Packfile::parse(b); }, "pointer size");
    b = bytes;
    b[12] = 11;
    expectError([&] { Packfile::parse(b); }, "version");
    b = bytes;
    b[17] = 0; // big endian
    expectError([&] { Packfile::parse(b); }, "endian");
}

TEST(HavokPackfile, refuses_every_truncation_without_crashing)
{
    const auto bytes = sample();
    for (std::size_t n = 0; n < bytes.size(); ++n)
    {
        const std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + n);
        try
        {
            Packfile::parse(cut);
            ADD_FAILURE() << "accepted a file cut to " << n;
        }
        catch (const Error&)
        {
        }
    }
}

TEST(HavokPackfile, refuses_a_huge_section_count)
{
    auto bytes = sample();
    const std::uint32_t n = 0x7FFFFFFF;
    std::memcpy(&bytes[20], &n, 4);
    expectError([&] { Packfile::parse(bytes); }, "section");
}

TEST(HavokPackfile, refuses_section_offsets_out_of_order_or_out_of_the_file)
{
    const auto bytes = sample();
    // the data section header is the third: 64 + 2 * 48; its fields start 20 bytes in
    const std::size_t h = 64 + 2 * 48 + 20;
    for (std::size_t field = 0; field < 7; ++field)
    {
        auto b = bytes;
        const std::uint32_t big = 0x40000000;
        std::memcpy(&b[h + 4 * field], &big, 4);
        expectError([&] { Packfile::parse(b); });
    }
}

TEST(HavokPackfile, refuses_a_fixup_that_leaves_the_section)
{
    PackGen g;
    const auto a = g.addObject("hkRootLevelContainer", 16);
    g.pointer(a, 0x10000);
    auto bytes = g.build();
    expectError([&] { Packfile::parse(bytes); }, "fix-up");

    PackGen h;
    const auto b = h.addObject("hkRootLevelContainer", 16);
    h.pointer(0x10000, b);
    bytes = h.build();
    expectError([&] { Packfile::parse(bytes); }, "fix-up");

    PackGen k;
    const auto c = k.addObject("hkRootLevelContainer", 16);
    k.globalPointer(c, 9, 0);
    bytes = k.build();
    expectError([&] { Packfile::parse(bytes); }, "fix-up");
}

TEST(HavokPackfile, refuses_a_class_name_offset_that_names_no_class)
{
    PackGen g;
    g.addObject("hkRootLevelContainer", 16);
    auto bytes = g.build();
    // the virtual table is the last thing before the padding; the class offset is the third word of its first entry
    // (found by search so that the test does not depend on the generator's padding)
    const std::uint32_t good = 5;
    std::size_t at = 0;
    for (std::size_t i = bytes.size() - 12; i > 0; --i)
    {
        std::uint32_t a, b, c;
        std::memcpy(&a, &bytes[i], 4);
        std::memcpy(&b, &bytes[i + 4], 4);
        std::memcpy(&c, &bytes[i + 8], 4);
        if (a == 0 && b == 0 && c == good)
        {
            at = i + 8;
            break;
        }
    }
    ASSERT_NE(at, 0u);
    const std::uint32_t bad = 6; // inside a name, not at the start of one
    std::memcpy(&bytes[at], &bad, 4);
    expectError([&] { Packfile::parse(bytes); }, "class");
}

TEST(HavokPackfile, refuses_two_objects_at_one_place)
{
    PackGen g;
    const auto a = g.addObject("hkRootLevelContainer", 16);
    g.addObject("hkaSkeleton", 16);
    auto bytes = g.build();
    // make the second virtual fix-up's source equal to the first's
    std::size_t at = 0;
    const std::uint32_t second = a + 16;
    for (std::size_t i = bytes.size() - 12; i > 0; --i)
    {
        std::uint32_t x;
        std::memcpy(&x, &bytes[i], 4);
        std::uint32_t y;
        std::memcpy(&y, &bytes[i + 4], 4);
        if (x == second && y == 0)
        {
            at = i;
            break;
        }
    }
    ASSERT_NE(at, 0u);
    std::memcpy(&bytes[at], &a, 4);
    expectError([&] { Packfile::parse(bytes); }, "twice");
}

TEST(HavokPackfile, caps_are_checked_before_allocation)
{
    const auto bytes = sample();
    Limits l;
    l.mMaxFileBytes = 100;
    expectError([&] { Packfile::parse(bytes, l); }, "large");
    l = Limits{};
    l.mMaxObjects = 1;
    expectError([&] { Packfile::parse(bytes, l); }, "objects");
    l = Limits{};
    l.mMaxClasses = 1;
    expectError([&] { Packfile::parse(bytes, l); }, "classes");
    l = Limits{};
    l.mMaxFixupsPerTable = 1;
    expectError([&] { Packfile::parse(bytes, l); }, "fix-up");
}

TEST(HavokPackfile, survives_random_single_byte_mutations)
{
    const auto bytes = sample();
    std::uint32_t state = 12345;
    int accepted = 0, refused = 0;
    for (int i = 0; i < 20000; ++i)
    {
        state = state * 1664525u + 1013904223u;
        auto b = bytes;
        b[(state >> 8) % b.size()] = static_cast<std::uint8_t>(state >> 24);
        try
        {
            const Packfile p = Packfile::parse(b);
            ++accepted;
            // an accepted file must be safe to walk
            for (const auto& o : p.objects())
            {
                (void)p.className(o);
                try
                {
                    (void)p.pointerAt({ o.mSection, o.mOffset });
                }
                catch (const Error&)
                {
                }
            }
        }
        catch (const Error&)
        {
            ++refused;
        }
    }
    EXPECT_GT(refused, 0);
    EXPECT_GT(accepted, 0);
}

// Opt-in: every file of a folder (the user's extracted .hkx files) must parse. Aggregates only are printed.
TEST(HavokPackfileRealData, every_file_in_the_folder_parses)
{
    const char* dir = std::getenv("OPENEDEN_HAVOK_HKX_DIR");
    if (dir == nullptr)
        GTEST_SKIP() << "OPENEDEN_HAVOK_HKX_DIR is not set";
    std::size_t files = 0, failed = 0, objects = 0;
    std::map<std::string, std::size_t> classes;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir))
    {
        if (!e.is_regular_file())
            continue;
        std::ifstream in(e.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ++files;
        try
        {
            const Packfile p = Packfile::parse(bytes);
            objects += p.objects().size();
            std::set<std::string> seen;
            for (const auto& o : p.objects())
                seen.insert(std::string(p.className(o)));
            for (const auto& c : seen)
                ++classes[c];
        }
        catch (const Error& ex)
        {
            ++failed;
            ADD_FAILURE() << ex.what();
        }
    }
    std::cout << "havok files " << files << ", failed " << failed << ", objects " << objects << ", classes "
              << classes.size() << "\n";
    for (const char* c : { "hkaSkeleton", "hkaSplineCompressedAnimation", "hkaAnimationBinding" })
        std::cout << c << " in " << classes[c] << " files\n";
    EXPECT_EQ(failed, 0u);
}
