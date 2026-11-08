// Task 0093 stage a: the test-only ".pex" assembler is itself tested.
//
// In plain words: every other Papyrus test builds its input with the assembler, so the assembler must be exact. These
// tests look at the bytes it writes (magic and byte order of each game, version fields, the three object size
// conventions, the string table, the layout offsets it reports) without any reader: stage b re-reads the same files
// with the real reader and compares what it finds.

#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "pexassembler.hpp"

namespace
{
    using namespace PexAsm;

    ScriptBuilder sampleScript()
    {
        ScriptBuilder script("SampleQuest");
        script.parent("Quest").autoProperty("Count", "Int", Int(3));
        script.var("flag", "Bool", Bool(true));
        script.function("OnInit")
            .local("::t", "Int")
            .assign(Id("::t"), Int(1))
            .iadd(Id("::t"), Id("::t"), Int(2))
            .ret(Nil());
        script.function("Helper", "Int").global().param("a", "Int").ret(Id("a"));
        script.state("Busy").function("OnInit").ret(Nil());
        return script;
    }

    std::uint16_t get16(const std::vector<std::uint8_t>& b, std::size_t at, bool big)
    {
        return big ? static_cast<std::uint16_t>((b[at] << 8) | b[at + 1])
                   : static_cast<std::uint16_t>((b[at + 1] << 8) | b[at]);
    }

    TEST(PexAssembler, skyrim_is_big_endian_version_3_2_game_1)
    {
        const Built built = sampleScript().build();
        const auto& b = built.mBytes;
        ASSERT_GT(b.size(), 16u);
        EXPECT_EQ(b[0], 0xFA);
        EXPECT_EQ(b[1], 0x57);
        EXPECT_EQ(b[2], 0xC0);
        EXPECT_EQ(b[3], 0xDE);
        EXPECT_EQ(b[built.mLayout.mMajor], 3);
        EXPECT_EQ(b[built.mLayout.mMinor], 2);
        EXPECT_EQ(get16(b, built.mLayout.mGameId, true), 1);
    }

    TEST(PexAssembler, fallout4_is_little_endian_version_3_9_game_2)
    {
        Options options;
        options.mTarget = Target::Fallout4;
        const Built built = sampleScript().build(options);
        const auto& b = built.mBytes;
        EXPECT_EQ(b[0], 0xDE);
        EXPECT_EQ(b[1], 0xC0);
        EXPECT_EQ(b[2], 0x57);
        EXPECT_EQ(b[3], 0xFA);
        EXPECT_EQ(b[built.mLayout.mMajor], 3);
        EXPECT_EQ(b[built.mLayout.mMinor], 9);
        EXPECT_EQ(get16(b, built.mLayout.mGameId, false), 2);
    }

    TEST(PexAssembler, the_same_builder_and_options_give_the_same_bytes)
    {
        for (const Target target : { Target::SkyrimSE, Target::Fallout4 })
        {
            Options options;
            options.mTarget = target;
            options.mDebugInfo = true;
            options.mShuffleSeed = 77;
            EXPECT_EQ(sampleScript().build(options).mBytes, sampleScript().build(options).mBytes);
        }
    }

    TEST(PexAssembler, a_shuffle_changes_the_bytes_but_not_the_size)
    {
        Options plain;
        Options shuffled;
        shuffled.mShuffleSeed = 12345;
        const Built a = sampleScript().build(plain);
        const Built b = sampleScript().build(shuffled);
        EXPECT_NE(a.mBytes, b.mBytes);
        EXPECT_EQ(a.mBytes.size(), b.mBytes.size());
        Options other = shuffled;
        other.mShuffleSeed = 54321;
        EXPECT_NE(b.mBytes, sampleScript().build(other).mBytes);
    }

    TEST(PexAssembler, the_string_count_matches_the_distinct_strings_plus_padding)
    {
        Options options;
        const Built base = sampleScript().build(options);
        options.mPadStrings = 10;
        const Built padded = sampleScript().build(options);
        const std::uint16_t baseCount = get16(base.mBytes, base.mLayout.mStringCount, true);
        const std::uint16_t paddedCount = get16(padded.mBytes, padded.mLayout.mStringCount, true);
        EXPECT_GT(baseCount, 10);
        EXPECT_EQ(paddedCount, baseCount + 10);
    }

    TEST(PexAssembler, the_extended_string_count_is_ffff_then_a_u32)
    {
        Options options;
        options.mExtendedStringCount = true;
        const Built built = sampleScript().build(options);
        const auto& b = built.mBytes;
        const std::size_t at = built.mLayout.mStringCount;
        EXPECT_EQ(get16(b, at, true), 0xFFFF);
        EXPECT_GT(read32(b, at + 2, true), 10u);
        EXPECT_EQ(b.size(), sampleScript().build().mBytes.size() + 4);
    }

    TEST(PexAssembler, the_three_size_conventions_differ_exactly_as_the_data_shows)
    {
        for (const Target target : { Target::SkyrimSE, Target::Fallout4 })
        {
            const bool big = target == Target::SkyrimSE;
            Options options;
            options.mTarget = target;
            options.mSizeConv = SizeConv::IncludesSelf;
            const Built includes = sampleScript().build(options);
            options.mSizeConv = SizeConv::ExcludesSelf;
            const Built excludes = sampleScript().build(options);
            options.mSizeConv = SizeConv::ExcludesSelfOtherOrder;
            const Built other = sampleScript().build(options);

            ASSERT_EQ(includes.mBytes.size(), excludes.mBytes.size());
            ASSERT_EQ(includes.mBytes.size(), other.mBytes.size());
            const std::size_t at = includes.mLayout.mObjectSize;
            ASSERT_GT(at, 0u);
            const std::uint32_t withSelf = read32(includes.mBytes, at, big);
            const std::uint32_t without = read32(excludes.mBytes, at, big);
            EXPECT_EQ(withSelf, without + 4);
            // The object ends where the file ends: its body is everything after the size field.
            EXPECT_EQ(without, includes.mBytes.size() - (at + 4));
            // The other order is the same number with its bytes reversed.
            EXPECT_EQ(read32(other.mBytes, at, !big), without);
            // Only the size field differs.
            std::size_t different = 0;
            for (std::size_t i = 0; i < includes.mBytes.size(); ++i)
            {
                different += includes.mBytes[i] != excludes.mBytes[i] ? 1 : 0;
            }
            EXPECT_LE(different, 1u);
        }
    }

    TEST(PexAssembler, trailing_bytes_are_added_after_the_last_object)
    {
        Options options;
        const std::size_t plain = sampleScript().build(options).mBytes.size();
        options.mTrailingBytes = 5;
        const Built built = sampleScript().build(options);
        EXPECT_EQ(built.mBytes.size(), plain + 5);
        EXPECT_EQ(built.mBytes.back(), 0xA5);
    }

    TEST(PexAssembler, the_layout_points_at_the_fields_it_names)
    {
        const Built built = sampleScript().build();
        const auto& b = built.mBytes;
        const Layout& layout = built.mLayout;
        EXPECT_EQ(get16(b, layout.mObjectCount, true), 1);
        EXPECT_EQ(get16(b, layout.mVariableCount, true), 2); // ::Count_var and flag
        EXPECT_EQ(get16(b, layout.mPropertyCount, true), 1);
        EXPECT_EQ(get16(b, layout.mStateCount, true), 2); // the empty state and Busy
        EXPECT_EQ(get16(b, layout.mFirstStateFunctionCount, true), 2); // OnInit and Helper
        EXPECT_EQ(get16(b, layout.mFirstFunctionInstrCount, true), 3); // assign, iadd, return
        EXPECT_EQ(layout.mFileSize, b.size());
        ASSERT_EQ(layout.mFirstFunctionInstr.size(), 3u);
        EXPECT_EQ(b[layout.mFirstFunctionInstr[0]], Op::Assign);
        EXPECT_EQ(b[layout.mFirstFunctionInstr[1]], Op::IAdd);
        EXPECT_EQ(b[layout.mFirstFunctionInstr[2]], Op::Return);
    }

    TEST(PexAssembler, fallout4_files_carry_struct_and_const_fields_skyrim_files_do_not)
    {
        ScriptBuilder script("WithStruct");
        script.structDef("Point").member("x", "Float", Flt(1.0f)).member("y", "Float");
        Options fo4;
        fo4.mTarget = Target::Fallout4;
        const Built built = script.build(fo4);
        EXPECT_GT(built.mLayout.mStructCount, 0u);
        EXPECT_EQ(get16(built.mBytes, built.mLayout.mStructCount, false), 1);
        const Built skyrim = script.build();
        EXPECT_EQ(skyrim.mLayout.mStructCount, 0u);
    }

    TEST(PexAssembler, a_call_writes_its_extra_argument_count)
    {
        ScriptBuilder script("Caller");
        script.function("F").local("::r", "Int");
        script.callStatic("Utility", "Wait", Id("::r"), { Flt(1.0f), Int(2) }).ret(Nil());
        const Built built = script.build();
        ASSERT_EQ(built.mLayout.mFirstFunctionInstr.size(), 2u);
        // opcode (1) + 3 fixed operands (class, function, destination: 1 + 2 bytes each) + the count value (1 + 4)
        // + two extra values (1 + 4 each).
        const std::size_t length = built.mLayout.mFirstFunctionInstr[1] - built.mLayout.mFirstFunctionInstr[0];
        EXPECT_EQ(length, 1u + 3u * 3u + 5u + 5u + 5u);
    }

    TEST(PexAssembler, the_debug_section_adds_bytes_and_is_flagged)
    {
        Options options;
        const std::size_t plain = sampleScript().build(options).mBytes.size();
        options.mDebugInfo = true;
        EXPECT_GT(sampleScript().build(options).mBytes.size(), plain);
    }

    TEST(PexAssembler, the_byte_order_can_be_forced_and_the_magic_follows)
    {
        Options options;
        options.mBigEndianOverride = 0;
        const Built little = sampleScript().build(options);
        EXPECT_EQ(little.mBytes[0], 0xDE);
        options.mBigEndianOverride = 1;
        options.mTarget = Target::Fallout4;
        const Built big = sampleScript().build(options);
        EXPECT_EQ(big.mBytes[0], 0xFA);
    }

    TEST(PexAssembler, patch_helpers_write_in_the_files_order)
    {
        std::vector<std::uint8_t> bytes(8, 0);
        patch16(bytes, 0, 0x1234, true);
        EXPECT_EQ(bytes[0], 0x12);
        EXPECT_EQ(bytes[1], 0x34);
        patch16(bytes, 2, 0x1234, false);
        EXPECT_EQ(bytes[2], 0x34);
        EXPECT_EQ(bytes[3], 0x12);
        patch32(bytes, 4, 0x01020304u, true);
        EXPECT_EQ(read32(bytes, 4, true), 0x01020304u);
        patch32(bytes, 4, 0x01020304u, false);
        EXPECT_EQ(read32(bytes, 4, false), 0x01020304u);
        EXPECT_EQ(bytes[4], 0x04);
    }
}
