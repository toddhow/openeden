// Task 0176, milestone M-S6 (Papyrus wiring): the VMAD parser, on generated subrecords. No game data.
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse::vmad;

    class Writer
    {
    public:
        Writer& u8(std::uint8_t v)
        {
            mBytes.push_back(static_cast<std::byte>(v));
            return *this;
        }
        Writer& u16(std::uint16_t v)
        {
            return u8(static_cast<std::uint8_t>(v & 0xFF)).u8(static_cast<std::uint8_t>(v >> 8));
        }
        Writer& i16(std::int16_t v) { return u16(static_cast<std::uint16_t>(v)); }
        Writer& u32(std::uint32_t v)
        {
            return u16(static_cast<std::uint16_t>(v & 0xFFFF)).u16(static_cast<std::uint16_t>(v >> 16));
        }
        Writer& i32(std::int32_t v) { return u32(static_cast<std::uint32_t>(v)); }
        Writer& f32(float v)
        {
            std::uint32_t bits;
            std::memcpy(&bits, &v, 4);
            return u32(bits);
        }
        Writer& ws(const std::string& s)
        {
            u16(static_cast<std::uint16_t>(s.size()));
            for (const char c : s)
                u8(static_cast<std::uint8_t>(c));
            return *this;
        }
        // object, format 2: unused, alias, form
        Writer& obj2(std::uint32_t form, std::int16_t alias) { return u16(0).i16(alias).u32(form); }
        Writer& obj1(std::uint32_t form, std::int16_t alias) { return u32(form).i16(alias).u16(0); }
        Writer& header(std::int16_t format, std::uint16_t scripts) { return i16(5).i16(format).u16(scripts); }
        Writer& prop(const std::string& name, std::uint8_t type) { return ws(name).u8(type).u8(1); }
        [[nodiscard]] std::vector<std::byte>& bytes() { return mBytes; }

    private:
        std::vector<std::byte> mBytes;
    };

    TEST(SkyrimVmad, an_empty_script_list_is_ok)
    {
        Writer w;
        w.header(2, 0);
        const ParseResult r = parse(w.bytes(), false);
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_EQ(r.mVmad.mVersion, 5);
        EXPECT_EQ(r.mVmad.mObjectFormat, 2);
        EXPECT_TRUE(r.mVmad.mScripts.empty());
        EXPECT_FALSE(r.mVmad.mHasFragments);
    }

    TEST(SkyrimVmad, a_script_with_one_property_of_every_scalar_kind)
    {
        Writer w;
        w.header(2, 1).ws("MyScript").u8(0).u16(5);
        w.prop("pObj", 1).obj2(0x00012345, 7);
        w.prop("pStr", 2).ws("hello");
        w.prop("pInt", 3).i32(-42);
        w.prop("pFlt", 4).f32(2.5f);
        w.prop("pBool", 5).u8(1);
        const ParseResult r = parse(w.bytes(), false);
        ASSERT_TRUE(r.mOk) << r.mError;
        ASSERT_EQ(r.mVmad.mScripts.size(), 1u);
        const Script& s = r.mVmad.mScripts[0];
        EXPECT_EQ(s.mName, "MyScript");
        ASSERT_EQ(s.mProperties.size(), 5u);
        EXPECT_EQ(s.mProperties[0].mName, "pObj");
        EXPECT_EQ(s.mProperties[0].mValue.mKind, ValueKind::Object);
        EXPECT_EQ(s.mProperties[0].mValue.mForm, 0x00012345u);
        EXPECT_EQ(s.mProperties[0].mValue.mAlias, 7);
        EXPECT_EQ(s.mProperties[1].mValue.mKind, ValueKind::String);
        EXPECT_EQ(s.mProperties[1].mValue.mText, "hello");
        EXPECT_EQ(s.mProperties[2].mValue.mKind, ValueKind::Int);
        EXPECT_EQ(s.mProperties[2].mValue.mInteger, -42);
        EXPECT_EQ(s.mProperties[3].mValue.mKind, ValueKind::Float);
        EXPECT_FLOAT_EQ(s.mProperties[3].mValue.mNumber, 2.5f);
        EXPECT_EQ(s.mProperties[4].mValue.mKind, ValueKind::Bool);
        EXPECT_EQ(s.mProperties[4].mValue.mInteger, 1);
        EXPECT_FALSE(s.mProperties[0].mArray);
    }

    TEST(SkyrimVmad, any_nonzero_bool_byte_is_true)
    {
        Writer w;
        w.header(2, 1).ws("S").u8(0).u16(2);
        w.prop("a", 5).u8(7);
        w.prop("b", 5).u8(0);
        const ParseResult r = parse(w.bytes(), false);
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[0].mValue.mInteger, 1);
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[1].mValue.mInteger, 0);
    }

    TEST(SkyrimVmad, object_format_one_stores_the_form_first)
    {
        Writer w;
        w.header(1, 1).ws("S").u8(0).u16(1);
        w.prop("p", 1).obj1(0x00ABCDEF, 3);
        const ParseResult r = parse(w.bytes(), false);
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_EQ(r.mVmad.mObjectFormat, 1);
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[0].mValue.mForm, 0x00ABCDEFu);
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[0].mValue.mAlias, 3);
    }

    TEST(SkyrimVmad, arrays_hold_values_of_the_element_kind)
    {
        Writer w;
        w.header(2, 1).ws("S").u8(0).u16(3);
        w.prop("objs", 11).u32(2).obj2(0x10, -1).obj2(0x20, 4);
        w.prop("ints", 13).u32(3).i32(1).i32(2).i32(3);
        w.prop("bools", 15).u32(0);
        const ParseResult r = parse(w.bytes(), false);
        ASSERT_TRUE(r.mOk) << r.mError;
        const Script& s = r.mVmad.mScripts[0];
        EXPECT_TRUE(s.mProperties[0].mArray);
        ASSERT_EQ(s.mProperties[0].mElements.size(), 2u);
        EXPECT_EQ(s.mProperties[0].mElements[1].mForm, 0x20u);
        EXPECT_EQ(s.mProperties[0].mElements[1].mAlias, 4);
        ASSERT_EQ(s.mProperties[1].mElements.size(), 3u);
        EXPECT_EQ(s.mProperties[1].mElements[2].mInteger, 3);
        EXPECT_TRUE(s.mProperties[2].mArray);
        EXPECT_TRUE(s.mProperties[2].mElements.empty());
    }

    TEST(SkyrimVmad, a_quest_vmad_has_fragments_and_alias_scripts)
    {
        Writer w;
        w.header(2, 1).ws("QuestScript").u8(0).u16(0);
        w.u8(2).u16(2).ws("QF_Test_01000800");
        w.u16(10).i16(0).i32(1).u8(1).ws("QF_Test_01000800").ws("Fragment_0");
        w.u16(20).i16(0).i32(-1).u8(1).ws("QF_Test_01000800").ws("Fragment_1");
        w.u16(1); // one alias
        w.obj2(0x01000800, 3).i16(5).i16(2).u16(1).ws("AliasScript").u8(0).u16(1);
        w.prop("Count", 3).i32(9);
        const ParseResult r = parse(w.bytes(), true);
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_TRUE(r.mVmad.mHasFragments);
        EXPECT_EQ(r.mVmad.mFragmentScript, "QF_Test_01000800");
        ASSERT_EQ(r.mVmad.mFragments.size(), 2u);
        EXPECT_EQ(r.mVmad.mFragments[0].mStage, 10);
        EXPECT_EQ(r.mVmad.mFragments[0].mLogEntry, 1);
        EXPECT_EQ(r.mVmad.mFragments[0].mFunction, "Fragment_0");
        EXPECT_EQ(r.mVmad.mFragments[1].mStage, 20);
        EXPECT_EQ(r.mVmad.mFragments[1].mLogEntry, -1);
        ASSERT_EQ(r.mVmad.mAliases.size(), 1u);
        EXPECT_EQ(r.mVmad.mAliases[0].mQuest, 0x01000800u);
        EXPECT_EQ(r.mVmad.mAliases[0].mAlias, 3);
        ASSERT_EQ(r.mVmad.mAliases[0].mScripts.size(), 1u);
        EXPECT_EQ(r.mVmad.mAliases[0].mScripts[0].mName, "AliasScript");
        EXPECT_EQ(r.mVmad.mAliases[0].mScripts[0].mProperties[0].mValue.mInteger, 9);
    }

    TEST(SkyrimVmad, a_plain_record_ignores_nothing_it_must_use_every_byte)
    {
        Writer w;
        w.header(2, 0).u8(0);
        const ParseResult r = parse(w.bytes(), false);
        EXPECT_FALSE(r.mOk);
        EXPECT_NE(r.mError.find("offset"), std::string::npos);
    }

    TEST(SkyrimVmad, every_truncation_is_refused_with_an_offset)
    {
        Writer w;
        w.header(2, 1).ws("S").u8(0).u16(2);
        w.prop("a", 2).ws("text");
        w.prop("b", 11).u32(1).obj2(5, 1);
        const std::size_t scriptsEnd = w.bytes().size();
        w.u8(2).u16(1).ws("QF").u16(1).i16(0).i32(0).u8(1).ws("QF").ws("Fragment_0").u16(0);
        const std::vector<std::byte> full = w.bytes();
        ASSERT_TRUE(parse(full, true).mOk);
        for (std::size_t n = 0; n < full.size(); ++n)
        {
            if (n == scriptsEnd)
                continue; // a QUST VMAD may end after its scripts (118 of 974 in Skyrim.esm)
            const std::vector<std::byte> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(n));
            const ParseResult r = parse(cut, true);
            EXPECT_FALSE(r.mOk) << "length " << n;
            EXPECT_FALSE(r.mError.empty()) << "length " << n;
        }
    }

    TEST(SkyrimVmad, a_quest_vmad_may_end_after_its_scripts)
    {
        Writer w;
        w.header(2, 1).ws("S").u8(0).u16(0);
        const ParseResult r = parse(w.bytes(), true);
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_FALSE(r.mVmad.mHasFragments);
        EXPECT_EQ(r.mVmad.mScripts.size(), 1u);
    }

    TEST(SkyrimVmad, an_unknown_property_type_is_refused_and_the_scripts_before_it_are_kept)
    {
        Writer w;
        w.header(2, 2).ws("Good").u8(0).u16(0);
        w.ws("Bad").u8(0).u16(1).prop("p", 9);
        const ParseResult r = parse(w.bytes(), false);
        EXPECT_FALSE(r.mOk);
        EXPECT_NE(r.mError.find("type 9"), std::string::npos) << r.mError;
        ASSERT_GE(r.mVmad.mScripts.size(), 1u);
        EXPECT_EQ(r.mVmad.mScripts[0].mName, "Good");
    }

    TEST(SkyrimVmad, counts_over_the_limits_are_refused_before_anything_is_reserved)
    {
        Writer w;
        w.header(2, 60000);
        Limits limits;
        limits.mMaxScripts = 16;
        EXPECT_FALSE(parse(w.bytes(), false, limits).mOk);
        Writer a;
        a.header(2, 1).ws("S").u8(0).u16(1).prop("p", 11).u32(0x7FFFFFFFu);
        EXPECT_FALSE(parse(a.bytes(), false).mOk);
        Writer b;
        b.header(2, 1).ws("S").u8(0).u16(60000);
        EXPECT_FALSE(parse(b.bytes(), false).mOk);
    }

    TEST(SkyrimVmad, a_name_longer_than_the_subrecord_is_refused)
    {
        Writer w;
        w.header(2, 1).u16(500);
        EXPECT_FALSE(parse(w.bytes(), false).mOk);
    }

    TEST(SkyrimVmad, resolveForms_maps_every_object_and_counts_the_ones_it_cannot)
    {
        Writer w;
        w.header(2, 1).ws("S").u8(0).u16(2);
        w.prop("a", 1).obj2(0x00000010, -1);
        w.prop("b", 11).u32(2).obj2(0x00000020, 2).obj2(0, -1);
        ParseResult r = parse(w.bytes(), false);
        ASSERT_TRUE(r.mOk) << r.mError;
        const std::size_t missing = resolveForms(r.mVmad.mScripts, [](std::uint32_t f) -> std::uint32_t {
            return f == 0x10 ? 0x01000010u : 0u; // 0x20 cannot be resolved
        });
        EXPECT_EQ(missing, 1u) << "a stored 0 is no object and is not counted";
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[0].mValue.mForm, 0x01000010u);
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[1].mElements[0].mForm, 0u);
        EXPECT_EQ(r.mVmad.mScripts[0].mProperties[1].mElements[0].mAlias, 2) << "the alias is untouched";
    }

    TEST(SkyrimVmad, resolveForms_reaches_the_alias_scripts_and_the_alias_quest)
    {
        Writer w;
        w.header(2, 0).u8(2).u16(0).ws("");
        w.u16(1).obj2(0x00000040, 1).i16(5).i16(2).u16(1).ws("A").u8(0).u16(1).prop("q", 1).obj2(0x00000041, 0);
        ParseResult r = parse(w.bytes(), true);
        ASSERT_TRUE(r.mOk) << r.mError;
        const std::size_t missing = resolveForms(r.mVmad, [](std::uint32_t f) { return f + 0x01000000u; });
        EXPECT_EQ(missing, 0u);
        EXPECT_EQ(r.mVmad.mAliases[0].mQuest, 0x01000040u);
        EXPECT_EQ(r.mVmad.mAliases[0].mScripts[0].mProperties[0].mValue.mForm, 0x01000041u);
    }
}
