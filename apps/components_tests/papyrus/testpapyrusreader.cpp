// Task 0093 stage b: the .pex reader and validator (components/script_papyrus/pexreader.cpp).
//
// In plain words: the reader is the one place that touches bytes from outside, so it is checked hard. Every test file
// is made by the test-only assembler (no game asset). The tests check that both games and both byte orders read back
// what was written, that the three object size conventions found in the installed files are accepted and counted,
// and that every kind of broken file gives a located error instead of a crash: a bad magic, a version or game the
// runtime does not run, a truncated file, trailing bytes, every count over its cap, an index or jump out of range, a
// duplicate state or function, guard opcodes and opcodes of the other game.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <components/script_papyrus/papyruslimits.hpp>

#include "papyrustestutil.hpp"

namespace
{
    using namespace PapyrusTest;
    using namespace PexAsm;
    namespace Pex = ScriptPapyrus::Pex;
    namespace Limits = ScriptPapyrus::Limits;
    using openeden::script::PexSizeConvention;

    const Target kTargets[] = { Target::SkyrimSE, Target::Fallout4 };

    // ---------------------------------------------------------------- what is written is what is read

    TEST(PapyrusReader, both_games_read_back_what_was_written)
    {
        for (const Target target : kTargets)
        {
            const Built built = richScript(target).build(optionsFor(target));
            const Pex::ParseResult result = parseBytes(built.mBytes);
            ASSERT_TRUE(result.mOk) << messages(result);
            const Pex::File& file = result.mFile;
            const bool fo4 = target == Target::Fallout4;
            EXPECT_EQ(file.mGame, fo4 ? Pex::Game::Fallout4 : Pex::Game::SkyrimSE);
            EXPECT_EQ(file.mMajor, 3);
            EXPECT_EQ(file.mMinor, fo4 ? 9 : 2);
            EXPECT_EQ(file.mBigEndian, !fo4);
            ASSERT_EQ(file.mObjects.size(), 1u);
            const Pex::Object& object = file.mObjects[0];
            EXPECT_EQ(file.str(object.mName), "RichQuest");
            EXPECT_EQ(file.str(object.mParent), "Quest");
            EXPECT_EQ(file.str(object.mDoc), "a documented script");
            EXPECT_EQ(object.mVariables.size(), 6u); // five declared and the backing variable of Total
            EXPECT_EQ(object.mProperties.size(), 2u);
            EXPECT_EQ(object.mStates.size(), 2u);
            EXPECT_EQ(object.mStructs.size(), fo4 ? 1u : 0u);
            EXPECT_EQ(file.str(object.mVariables[0].mName), "counter");
            EXPECT_EQ(file.str(object.mVariables[0].mType), "Int");
            EXPECT_EQ(object.mVariables[0].mInitial.mType, Pex::ValueType::Integer);
            EXPECT_EQ(object.mVariables[0].mInitial.mInteger, 5);
            EXPECT_EQ(file.str(object.mVariables[1].mInitial.mString), "hi");
            EXPECT_EQ(object.mVariables[2].mInitial.mFloat, 1.5f);
            EXPECT_EQ(object.mVariables[3].mInitial.mType, Pex::ValueType::Bool);
            EXPECT_EQ(object.mVariables[3].mInitial.mInteger, 1);
            EXPECT_EQ(object.mVariables[4].mInitial.mType, Pex::ValueType::None);
            const Pex::Property& total = object.mProperties[0];
            EXPECT_EQ(file.str(total.mName), "Total");
            EXPECT_NE(total.mFlags & Pex::Property::kAuto, 0);
            EXPECT_EQ(file.str(total.mAutoVariable), "::Total_var");
            const Pex::Property& computed = object.mProperties[1];
            EXPECT_EQ(file.str(computed.mName), "Computed");
            EXPECT_EQ(computed.mFlags & Pex::Property::kAuto, 0);
            EXPECT_EQ(computed.mGetter.mInstructions.size(), 2u);
            EXPECT_EQ(computed.mSetter.mParameters.size(), 1u);
            if (fo4)
            {
                ASSERT_EQ(object.mStructs.size(), 1u);
                EXPECT_EQ(file.str(object.mStructs[0].mName), "Point");
                ASSERT_EQ(object.mStructs[0].mMembers.size(), 2u);
                EXPECT_EQ(object.mStructs[0].mMembers[0].mInitial.mFloat, 2.0f);
            }

            const Pex::Function* init = findFunction(file, "OnInit", "");
            ASSERT_NE(init, nullptr);
            EXPECT_EQ(init->mLocals.size(), 2u);
            ASSERT_EQ(init->mInstructions.size(), 6u);
            EXPECT_EQ(init->mInstructions[0].mOpcode, Pex::Op::Assign);
            EXPECT_EQ(init->mInstructions[1].mOpcode, Pex::Op::IAdd);
            EXPECT_EQ(init->mInstructions[2].mOpcode, Pex::Op::StrCat);
            EXPECT_EQ(init->mInstructions[3].mOpcode, Pex::Op::JmpF);
            EXPECT_EQ(init->mInstructions[4].mOpcode, Pex::Op::CallMethod);
            EXPECT_EQ(init->mInstructions[4].mFixedCount, 3);
            EXPECT_EQ(init->mInstructions[4].mExtraCount, 2);
            EXPECT_EQ(init->mInstructions[5].mOpcode, Pex::Op::Return);
            const Pex::Function* helper = findFunction(file, "helper");
            ASSERT_NE(helper, nullptr);
            EXPECT_TRUE(helper->isGlobal());
            EXPECT_FALSE(helper->isNative());
            EXPECT_NE(findFunction(file, "OnInit", "Busy"), nullptr);
            EXPECT_TRUE(result.mReports.empty()) << messages(result);
        }
    }

    TEST(PapyrusReader, the_string_table_order_does_not_matter)
    {
        for (const Target target : kTargets)
        {
            Options plain = optionsFor(target);
            const Pex::ParseResult reference = parseBytes(richScript(target).build(plain).mBytes);
            ASSERT_TRUE(reference.mOk) << messages(reference);
            for (std::uint32_t seed = 1; seed <= 6; ++seed)
            {
                Options shuffled = plain;
                shuffled.mShuffleSeed = seed * 7919;
                const Pex::ParseResult result = parseBytes(richScript(target).build(shuffled).mBytes);
                ASSERT_TRUE(result.mOk) << messages(result);
                const Pex::Function* a = findFunction(reference.mFile, "OnInit", "");
                const Pex::Function* b = findFunction(result.mFile, "OnInit", "");
                ASSERT_NE(a, nullptr);
                ASSERT_NE(b, nullptr);
                ASSERT_EQ(a->mInstructions.size(), b->mInstructions.size());
                EXPECT_EQ(result.mFile.str(result.mFile.mObjects[0].mName), "RichQuest");
                EXPECT_EQ(result.mFile.str(b->mOperands[0].mString), reference.mFile.str(a->mOperands[0].mString));
                EXPECT_NE(result.mFile.mStrings, reference.mFile.mStrings);
            }
        }
    }

    TEST(PapyrusReader, a_debug_section_is_read_and_skipped)
    {
        for (const Target target : kTargets)
        {
            Options options = optionsFor(target);
            options.mDebugInfo = true;
            const Pex::ParseResult result = parseBytes(richScript(target).build(options).mBytes);
            ASSERT_TRUE(result.mOk) << messages(result);
            EXPECT_EQ(result.mFile.mObjects.size(), 1u);
        }
    }

    TEST(PapyrusReader, skyrim_version_3_1_is_accepted)
    {
        Options options;
        options.mMinorOverride = 1;
        const Pex::ParseResult result = parseBytes(richScript(Target::SkyrimSE).build(options).mBytes);
        ASSERT_TRUE(result.mOk) << messages(result);
        EXPECT_EQ(result.mFile.mMinor, 1);
    }

    TEST(PapyrusReader, the_extended_string_count_is_read)
    {
        for (const Target target : kTargets)
        {
            Options options = optionsFor(target);
            options.mExtendedStringCount = true;
            const Pex::ParseResult result = parseBytes(richScript(target).build(options).mBytes);
            ASSERT_TRUE(result.mOk) << messages(result);
        }
    }

    TEST(PapyrusReader, parsing_twice_gives_the_same_model_and_the_same_text)
    {
        for (const Target target : kTargets)
        {
            Options options = optionsFor(target);
            options.mSizeConv = SizeConv::ExcludesSelf;
            const Bytes bytes = richScript(target).build(options).mBytes;
            const Pex::ParseResult a = parseBytes(bytes);
            const Pex::ParseResult b = parseBytes(bytes);
            EXPECT_EQ(messages(a), messages(b));
            EXPECT_EQ(a.mFile.mStrings, b.mFile.mStrings);
            ASSERT_EQ(a.mFile.mObjects.size(), b.mFile.mObjects.size());
            EXPECT_EQ(a.mFile.mObjects[0].mStates.size(), b.mFile.mObjects[0].mStates.size());

            Bytes broken = bytes;
            broken.resize(broken.size() - 3);
            EXPECT_EQ(messages(parseBytes(broken)), messages(parseBytes(broken)));
        }
    }

    // ---------------------------------------------------------------- the object size conventions

    TEST(PapyrusReader, the_three_size_conventions_are_accepted_and_counted)
    {
        const std::pair<SizeConv, PexSizeConvention> conventions[] = {
            { SizeConv::IncludesSelf, PexSizeConvention::IncludesSelf },
            { SizeConv::ExcludesSelf, PexSizeConvention::ExcludesSelf },
            { SizeConv::ExcludesSelfOtherOrder, PexSizeConvention::ExcludesSelfOtherOrder },
        };
        for (const Target target : kTargets)
        {
            for (const auto& [written, expected] : conventions)
            {
                Options options = optionsFor(target);
                options.mSizeConv = written;
                const Pex::ParseResult result = parseBytes(richScript(target).build(options).mBytes);
                ASSERT_TRUE(result.mOk) << messages(result);
                EXPECT_EQ(result.mFile.mObjects[0].mSizeConvention, expected);
                // A deviant convention is reported once as a warning that names it; the usual one says nothing.
                if (written == SizeConv::IncludesSelf)
                {
                    EXPECT_TRUE(result.mReports.empty());
                }
                else
                {
                    ASSERT_EQ(result.mReports.size(), 1u) << messages(result);
                    EXPECT_EQ(result.mReports[0].severity, openeden::script::Severity::Warning);
                    EXPECT_TRUE(contains(result.mReports[0].message, "size")) << messages(result);
                    EXPECT_TRUE(contains(result.mReports[0].message,
                        written == SizeConv::ExcludesSelf ? "after the field" : "other byte order"))
                        << messages(result);
                }
            }
        }
    }

    TEST(PapyrusReader, strict_mode_refuses_a_deviant_size_and_accepts_the_usual_one)
    {
        for (const Target target : kTargets)
        {
            Options usual = optionsFor(target);
            EXPECT_TRUE(parseBytes(richScript(target).build(usual).mBytes, true).mOk);
            for (const SizeConv conv : { SizeConv::ExcludesSelf, SizeConv::ExcludesSelfOtherOrder })
            {
                Options deviant = optionsFor(target);
                deviant.mSizeConv = conv;
                const Pex::ParseResult result = parseBytes(richScript(target).build(deviant).mBytes, true);
                EXPECT_FALSE(result.mOk);
                EXPECT_TRUE(contains(messages(result), "size")) << messages(result);
            }
        }
    }

    TEST(PapyrusReader, a_size_that_matches_no_convention_is_refused)
    {
        for (const Target target : kTargets)
        {
            const bool big = target == Target::SkyrimSE;
            const Built built = richScript(target).build(optionsFor(target));
            for (const std::int32_t delta : { 1, -1, 7, 1000 })
            {
                Bytes bytes = built.mBytes;
                const std::uint32_t declared = read32(bytes, built.mLayout.mObjectSize, big);
                patch32(bytes, built.mLayout.mObjectSize, declared + static_cast<std::uint32_t>(delta), big);
                const Pex::ParseResult result = parseBytes(bytes);
                EXPECT_FALSE(result.mOk) << delta;
                EXPECT_TRUE(contains(messages(result), "object size")) << messages(result);
                EXPECT_TRUE(contains(messages(result), "offset")) << messages(result);
            }
        }
    }

    // ---------------------------------------------------------------- header refusals

    TEST(PapyrusReader, a_bad_magic_is_refused_and_named)
    {
        Bytes bytes = richScript(Target::SkyrimSE).build().mBytes;
        bytes[0] = 0x00;
        const Pex::ParseResult result = parseBytes(bytes);
        EXPECT_FALSE(result.mOk);
        EXPECT_TRUE(contains(messages(result), "magic")) << messages(result);
        EXPECT_TRUE(contains(messages(result), "test.pex")) << messages(result);
    }

    TEST(PapyrusReader, a_version_or_game_the_runtime_does_not_run_is_refused_naming_both)
    {
        struct Case
        {
            Target mTarget = Target::SkyrimSE;
            int mMajor = -1;
            int mMinor = -1;
            int mGame = -1;
            const char* mVersion = "";
            const char* mGameText = "";
        };
        const Case cases[] = {
            { Target::SkyrimSE, -1, 9, -1, "3.9", "game id 1" }, // a Fallout 4 version with the Skyrim game
            { Target::SkyrimSE, -1, -1, 2, "3.2", "game id 2" },
            { Target::Fallout4, -1, 2, -1, "3.2", "game id 2" },
            { Target::Fallout4, -1, -1, 1, "3.9", "game id 1" },
            { Target::SkyrimSE, 4, 0, -1, "4.0", "game id 1" },
            { Target::SkyrimSE, 3, 12, 3, "3.12", "game id 3" }, // another game, for example Starfield
            { Target::SkyrimSE, 2, 2, -1, "2.2", "game id 1" },
        };
        for (const Case& c : cases)
        {
            Options options = optionsFor(c.mTarget);
            options.mMajorOverride = c.mMajor;
            options.mMinorOverride = c.mMinor;
            options.mGameIdOverride = c.mGame;
            const Pex::ParseResult result = parseBytes(richScript(c.mTarget).build(options).mBytes);
            EXPECT_FALSE(result.mOk) << c.mVersion << " " << c.mGameText;
            EXPECT_TRUE(contains(messages(result), c.mVersion)) << messages(result);
            EXPECT_TRUE(contains(messages(result), c.mGameText)) << messages(result);
        }
    }

    TEST(PapyrusReader, the_byte_order_follows_the_magic_whichever_game_the_file_names)
    {
        Options options = optionsFor(Target::Fallout4);
        options.mBigEndianOverride = 1;
        const Pex::ParseResult big = parseBytes(richScript(Target::Fallout4).build(options).mBytes);
        ASSERT_TRUE(big.mOk) << messages(big);
        EXPECT_TRUE(big.mFile.mBigEndian);
        EXPECT_EQ(big.mFile.mGame, Pex::Game::Fallout4);
        options = optionsFor(Target::SkyrimSE);
        options.mBigEndianOverride = 0;
        const Pex::ParseResult little = parseBytes(richScript(Target::SkyrimSE).build(options).mBytes);
        ASSERT_TRUE(little.mOk) << messages(little);
        EXPECT_FALSE(little.mFile.mBigEndian);
    }

    TEST(PapyrusReader, an_empty_file_and_every_truncation_are_refused_with_a_location)
    {
        for (const Target target : kTargets)
        {
            Options options = optionsFor(target);
            options.mDebugInfo = true;
            const Bytes full = richScript(target).build(options).mBytes;
            ASSERT_TRUE(parseBytes(full).mOk);
            for (std::size_t length = 0; length < full.size(); ++length)
            {
                const Bytes cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(length));
                const Pex::ParseResult result = parseBytes(cut);
                ASSERT_FALSE(result.mOk) << "length " << length;
                ASSERT_FALSE(result.mReports.empty());
                EXPECT_FALSE(result.mReports[0].message.empty());
                EXPECT_EQ(result.mReports[0].script, "test.pex");
            }
        }
    }

    TEST(PapyrusReader, bytes_after_the_last_object_are_refused)
    {
        for (const Target target : kTargets)
        {
            Options options = optionsFor(target);
            options.mTrailingBytes = 1;
            const Pex::ParseResult result = parseBytes(richScript(target).build(options).mBytes);
            EXPECT_FALSE(result.mOk);
            EXPECT_TRUE(contains(messages(result), "trailing")) << messages(result);
        }
    }

    TEST(PapyrusReader, a_little_zero_padding_after_the_last_object_is_read_with_a_warning)
    {
        // 165 installed mod scripts end with 29 to 51 zero bytes; nothing else is tolerated.
        for (const Target target : kTargets)
        {
            for (const std::size_t padding : { std::size_t{ 1 }, std::size_t{ 30 }, Limits::kMaxTrailingPadding })
            {
                Options options = optionsFor(target);
                options.mTrailingBytes = padding;
                options.mTrailingValue = 0;
                const Pex::ParseResult result = parseBytes(richScript(target).build(options).mBytes);
                ASSERT_TRUE(result.mOk) << messages(result);
                ASSERT_EQ(result.mReports.size(), 1u);
                EXPECT_EQ(result.mReports[0].severity, openeden::script::Severity::Warning);
                EXPECT_TRUE(contains(result.mReports[0].message, "padding")) << messages(result);
                EXPECT_TRUE(contains(result.mReports[0].message, std::to_string(padding))) << messages(result);
                // Strict mode refuses it.
                EXPECT_FALSE(parseBytes(richScript(target).build(options).mBytes, true).mOk);
            }
            Options tooMuch = optionsFor(target);
            tooMuch.mTrailingBytes = Limits::kMaxTrailingPadding + 1;
            tooMuch.mTrailingValue = 0;
            const Pex::ParseResult over = parseBytes(richScript(target).build(tooMuch).mBytes);
            EXPECT_FALSE(over.mOk);
            EXPECT_TRUE(contains(messages(over), "trailing")) << messages(over);
            // One nonzero byte among the zeros makes it damage again.
            Bytes mixed = richScript(target).build(optionsFor(target)).mBytes;
            mixed.insert(mixed.end(), 10, 0);
            mixed.push_back(1);
            EXPECT_FALSE(parseBytes(mixed).mOk);
        }
    }

    TEST(PapyrusReader, a_file_over_the_size_cap_is_refused_before_it_is_read)
    {
        const Bytes huge(Limits::kMaxFileBytes + 1, 0xFA);
        const Pex::ParseResult result = parseBytes(huge);
        EXPECT_FALSE(result.mOk);
        EXPECT_TRUE(contains(messages(result), "cap")) << messages(result);
    }

    // ---------------------------------------------------------------- caps, at the cap and one past

    ScriptBuilder withNops(std::size_t count)
    {
        ScriptBuilder script("Big");
        script.function("F");
        for (std::size_t i = 0; i < count; ++i)
        {
            script.nop();
        }
        script.ret(Nil());
        return script;
    }

    TEST(PapyrusReader, instructions_per_function_cap)
    {
        for (const Target target : kTargets)
        {
            EXPECT_TRUE(parseBytes(withNops(Limits::kMaxInstructions - 1).build(optionsFor(target)).mBytes).mOk);
            const Pex::ParseResult over
                = parseBytes(withNops(Limits::kMaxInstructions).build(optionsFor(target)).mBytes);
            EXPECT_FALSE(over.mOk);
            EXPECT_TRUE(contains(messages(over), "instructions")) << messages(over);
            EXPECT_TRUE(contains(messages(over), "cap")) << messages(over);
        }
    }

    TEST(PapyrusReader, variables_per_object_cap)
    {
        for (const std::size_t count : { Limits::kMaxMembersPerObject, Limits::kMaxMembersPerObject + 1 })
        {
            ScriptBuilder script("Vars");
            for (std::size_t i = 0; i < count; ++i)
            {
                script.var("v" + std::to_string(i), "Int");
            }
            const Pex::ParseResult result = parseBytes(script.build().mBytes);
            EXPECT_EQ(result.mOk, count == Limits::kMaxMembersPerObject) << messages(result);
            if (!result.mOk)
            {
                EXPECT_TRUE(contains(messages(result), "variables")) << messages(result);
            }
        }
    }

    TEST(PapyrusReader, properties_per_object_cap)
    {
        for (const std::size_t count : { Limits::kMaxMembersPerObject, Limits::kMaxMembersPerObject + 1 })
        {
            ScriptBuilder script("Props");
            for (std::size_t i = 0; i < count; ++i)
            {
                script.autoProperty("p" + std::to_string(i), "Int");
            }
            // The backing variables count too: stay within the variable cap by using one variable per property only
            // up to the cap; the property cap is the same number, so the variable count trips first one past.
            const Pex::ParseResult result = parseBytes(script.build().mBytes);
            EXPECT_EQ(result.mOk, count == Limits::kMaxMembersPerObject) << messages(result);
        }
    }

    TEST(PapyrusReader, states_per_object_cap)
    {
        for (const std::size_t count : { Limits::kMaxStates, Limits::kMaxStates + 1 })
        {
            ScriptBuilder script("States");
            for (std::size_t i = 0; i < count; ++i)
            {
                script.state("s" + std::to_string(i)).function("F");
            }
            const Pex::ParseResult result = parseBytes(script.build().mBytes);
            EXPECT_EQ(result.mOk, count == Limits::kMaxStates) << messages(result);
            if (!result.mOk)
            {
                EXPECT_TRUE(contains(messages(result), "states")) << messages(result);
            }
        }
    }

    TEST(PapyrusReader, functions_per_state_cap)
    {
        for (const std::size_t count : { Limits::kMaxFunctionsPerState, Limits::kMaxFunctionsPerState + 1 })
        {
            ScriptBuilder script("Functions");
            for (std::size_t i = 0; i < count; ++i)
            {
                script.function("f" + std::to_string(i));
            }
            const Pex::ParseResult result = parseBytes(script.build().mBytes);
            EXPECT_EQ(result.mOk, count == Limits::kMaxFunctionsPerState) << messages(result);
            if (!result.mOk)
            {
                EXPECT_TRUE(contains(messages(result), "functions")) << messages(result);
            }
        }
    }

    TEST(PapyrusReader, parameters_and_locals_per_function_cap)
    {
        for (const std::size_t count : { Limits::kMaxParametersAndLocals, Limits::kMaxParametersAndLocals + 1 })
        {
            ScriptBuilder params("Params");
            params.function("F");
            ScriptBuilder locals("Locals");
            locals.function("F");
            for (std::size_t i = 0; i < count; ++i)
            {
                params.param("p" + std::to_string(i), "Int");
                locals.local("l" + std::to_string(i), "Int");
            }
            EXPECT_EQ(parseBytes(params.build().mBytes).mOk, count == Limits::kMaxParametersAndLocals);
            EXPECT_EQ(parseBytes(locals.build().mBytes).mOk, count == Limits::kMaxParametersAndLocals);
        }
    }

    TEST(PapyrusReader, call_arguments_cap)
    {
        for (const std::size_t count : { Limits::kMaxCallArguments, Limits::kMaxCallArguments + 1 })
        {
            ScriptBuilder script("Calls");
            script.function("F").local("::r", "Int");
            std::vector<Operand> args;
            for (std::size_t i = 0; i < count; ++i)
            {
                args.push_back(Int(static_cast<std::int32_t>(i)));
            }
            script.callStatic("Utility", "Wait", Id("::r"), args).ret(Nil());
            const Pex::ParseResult result = parseBytes(script.build().mBytes);
            EXPECT_EQ(result.mOk, count == Limits::kMaxCallArguments) << messages(result);
            if (!result.mOk)
            {
                EXPECT_TRUE(contains(messages(result), "argument")) << messages(result);
            }
        }
    }

    TEST(PapyrusReader, objects_per_file_cap)
    {
        for (const std::size_t count : { Limits::kMaxObjects, Limits::kMaxObjects + 1 })
        {
            std::vector<ScriptBuilder> scripts;
            for (std::size_t i = 0; i < count; ++i)
            {
                scripts.emplace_back("Obj" + std::to_string(i));
            }
            const Pex::ParseResult result = parseBytes(buildFile(scripts, {}).mBytes);
            EXPECT_EQ(result.mOk, count == Limits::kMaxObjects) << messages(result);
            if (result.mOk)
            {
                EXPECT_EQ(result.mFile.mObjects.size(), count);
            }
        }
    }

    TEST(PapyrusReader, structs_and_struct_members_cap)
    {
        Options fo4 = optionsFor(Target::Fallout4);
        for (const std::size_t count : { Limits::kMaxStructs, Limits::kMaxStructs + 1 })
        {
            ScriptBuilder script("Structs");
            for (std::size_t i = 0; i < count; ++i)
            {
                script.structDef("S" + std::to_string(i));
            }
            EXPECT_EQ(parseBytes(script.build(fo4).mBytes).mOk, count == Limits::kMaxStructs);
        }
        for (const std::size_t count : { Limits::kMaxStructMembers, Limits::kMaxStructMembers + 1 })
        {
            ScriptBuilder script("Members");
            script.structDef("S");
            for (std::size_t i = 0; i < count; ++i)
            {
                script.member("m" + std::to_string(i), "Int");
            }
            EXPECT_EQ(parseBytes(script.build(fo4).mBytes).mOk, count == Limits::kMaxStructMembers);
        }
    }

    TEST(PapyrusReader, strings_cap_with_the_two_byte_and_the_four_byte_count)
    {
        // The two byte count holds at most 65,534 (0xFFFF announces a four byte count).
        {
            Options options;
            const Pex::ParseResult plain = parseBytes(ScriptBuilder("S").build(options).mBytes);
            ASSERT_TRUE(plain.mOk) << messages(plain);
            const std::size_t used = plain.mFile.mStrings.size();
            options.mPadStrings = Limits::kMaxStrings - used;
            const Pex::ParseResult atCap = parseBytes(ScriptBuilder("S").build(options).mBytes);
            ASSERT_TRUE(atCap.mOk) << messages(atCap);
            EXPECT_EQ(atCap.mFile.mStrings.size(), Limits::kMaxStrings);
        }
        {
            Options options;
            options.mExtendedStringCount = true;
            const std::size_t used = parseBytes(ScriptBuilder("S").build(options).mBytes).mFile.mStrings.size();
            options.mPadStrings = Limits::kMaxExtendedStrings - used;
            const Pex::ParseResult atCap = parseBytes(ScriptBuilder("S").build(options).mBytes);
            EXPECT_TRUE(atCap.mOk) << messages(atCap);
            options.mPadStrings += 1;
            const Pex::ParseResult over = parseBytes(ScriptBuilder("S").build(options).mBytes);
            EXPECT_FALSE(over.mOk);
            EXPECT_TRUE(contains(messages(over), "strings")) << messages(over);
        }
    }

    TEST(PapyrusReader, functions_and_instructions_per_file_caps)
    {
        // 16 states of 4096 functions is exactly the per-file function cap; one more function in a 17th state is over.
        {
            ScriptBuilder script("Many");
            for (std::size_t s = 0; s < 16; ++s)
            {
                script.state("s" + std::to_string(s));
                for (std::size_t f = 0; f < 4096; ++f)
                {
                    script.function("f" + std::to_string(f));
                }
            }
            EXPECT_TRUE(parseBytes(script.build().mBytes).mOk);
            script.state("last").function("one");
            const Pex::ParseResult over = parseBytes(script.build().mBytes);
            EXPECT_FALSE(over.mOk);
            EXPECT_TRUE(contains(messages(over), "functions")) << messages(over);
        }
        // Functions of the largest size, as many as the per-file instruction cap allows, are exactly at that cap.
        {
            ScriptBuilder script("Long");
            for (std::size_t f = 0; f < Limits::kMaxInstructionsPerFile / Limits::kMaxInstructions; ++f)
            {
                script.function("f" + std::to_string(f));
                for (std::size_t i = 0; i < Limits::kMaxInstructions - 1; ++i)
                {
                    script.nop();
                }
                script.ret(Nil());
            }
            EXPECT_TRUE(parseBytes(script.build().mBytes).mOk);
            script.function("extra").ret(Nil());
            const Pex::ParseResult over = parseBytes(script.build().mBytes);
            EXPECT_FALSE(over.mOk);
            EXPECT_TRUE(contains(messages(over), "instructions")) << messages(over);
        }
    }

    TEST(PapyrusReader, every_count_field_set_to_ffff_is_refused_without_a_crash)
    {
        for (const Target target : kTargets)
        {
            const bool big = target == Target::SkyrimSE;
            const Built built = richScript(target).build(optionsFor(target));
            std::vector<std::size_t> fields = { built.mLayout.mObjectCount, built.mLayout.mVariableCount,
                built.mLayout.mPropertyCount, built.mLayout.mStateCount, built.mLayout.mFirstStateFunctionCount,
                built.mLayout.mFirstFunctionParamCount, built.mLayout.mFirstFunctionLocalCount,
                built.mLayout.mFirstFunctionInstrCount, built.mLayout.mStringCount };
            if (target == Target::Fallout4)
            {
                fields.push_back(built.mLayout.mStructCount);
            }
            for (const std::size_t field : fields)
            {
                ASSERT_GT(field, 0u);
                Bytes bytes = built.mBytes;
                patch16(bytes, field, 0xFFFF, big);
                const Pex::ParseResult result = parseBytes(bytes);
                EXPECT_FALSE(result.mOk) << "field at " << field;
                EXPECT_FALSE(result.mReports.empty());
            }
        }
    }

    // ---------------------------------------------------------------- indexes, names and jumps

    TEST(PapyrusReader, a_string_index_out_of_range_is_refused_and_located)
    {
        for (const Target target : kTargets)
        {
            const bool big = target == Target::SkyrimSE;
            const Built built = richScript(target).build(optionsFor(target));
            // The first operand of the first instruction: type byte then the string index.
            Bytes bytes = built.mBytes;
            patch16(bytes, built.mLayout.mFirstFunctionInstr[0] + 2, 0xFFF0, big);
            const Pex::ParseResult result = parseBytes(bytes);
            EXPECT_FALSE(result.mOk);
            EXPECT_TRUE(contains(messages(result), "string index")) << messages(result);
            EXPECT_TRUE(contains(messages(result), "RichQuest.OnInit@0")) << messages(result);
            EXPECT_TRUE(contains(messages(result), "offset")) << messages(result);

            Bytes parent = built.mBytes;
            patch16(parent, built.mLayout.mObjectParent, 0xFFF0, big);
            const Pex::ParseResult badParent = parseBytes(parent);
            EXPECT_FALSE(badParent.mOk);
            EXPECT_TRUE(contains(messages(badParent), "string index")) << messages(badParent);
        }
    }

    TEST(PapyrusReader, an_unknown_value_type_is_refused)
    {
        ScriptBuilder script("BadType");
        script.function("F").local("::t", "Int");
        Operand bad = Int(1);
        bad.mBadType = 9;
        script.op(Op::Assign, { Id("::t"), bad }).ret(Nil());
        const Pex::ParseResult result = parseBytes(script.build().mBytes);
        EXPECT_FALSE(result.mOk);
        EXPECT_TRUE(contains(messages(result), "value type")) << messages(result);
    }

    TEST(PapyrusReader, jumps_must_land_inside_the_function)
    {
        auto withJump = [](std::int32_t offset) {
            ScriptBuilder script("Jumps");
            script.function("F").nop().jmp(offset).nop().ret(Nil());
            return parseBytes(script.build().mBytes);
        };
        EXPECT_TRUE(withJump(2).mOk); // to the last instruction
        EXPECT_TRUE(
            withJump(3)
                .mOk); // to the end of the function (what the compiler writes for a branch past the last statement)
        EXPECT_TRUE(withJump(0).mOk); // to itself
        EXPECT_TRUE(withJump(-1).mOk); // to the first instruction
        for (const std::int32_t bad : { 4, 100, -2, -1000, 2147483647, -2147483647 - 1 })
        {
            const Pex::ParseResult result = withJump(bad);
            EXPECT_FALSE(result.mOk) << bad;
            EXPECT_TRUE(contains(messages(result), "jump")) << messages(result);
            EXPECT_TRUE(contains(messages(result), "Jumps.F@1")) << messages(result);
        }
        ScriptBuilder conditional("Cond");
        conditional.function("F").jmpf(Bool(true), 6).ret(Nil());
        EXPECT_FALSE(parseBytes(conditional.build().mBytes).mOk);
        ScriptBuilder conditionalT("CondT");
        conditionalT.function("F").jmpt(Bool(true), -1).ret(Nil());
        EXPECT_FALSE(parseBytes(conditionalT.build().mBytes).mOk);
    }

    TEST(PapyrusReader, a_jump_operand_must_be_an_integer)
    {
        ScriptBuilder script("JumpType");
        script.function("F").op(Op::Jmp, { Str("x") }).ret(Nil());
        EXPECT_FALSE(parseBytes(script.build().mBytes).mOk);
    }

    TEST(PapyrusReader, a_duplicate_state_is_refused_without_regard_to_case)
    {
        ScriptBuilder script("DupState");
        script.state("Busy").function("F");
        script.state("busy").function("G");
        // The builder merges equal names only; "Busy" and "busy" are different strings, so two states are written.
        const Pex::ParseResult result = parseBytes(script.build().mBytes);
        EXPECT_FALSE(result.mOk);
        EXPECT_TRUE(contains(messages(result), "state")) << messages(result);
        EXPECT_TRUE(contains(messages(result), "duplicate")) << messages(result);
    }

    TEST(PapyrusReader, a_duplicate_function_in_one_state_is_refused_but_not_across_states)
    {
        ScriptBuilder dup("DupFunction");
        dup.function("OnInit").ret(Nil());
        dup.function("oninit").ret(Nil());
        const Pex::ParseResult result = parseBytes(dup.build().mBytes);
        EXPECT_FALSE(result.mOk);
        EXPECT_TRUE(contains(messages(result), "duplicate")) << messages(result);
        EXPECT_TRUE(contains(messages(result), "function")) << messages(result);

        ScriptBuilder fine("TwoStates");
        fine.function("OnInit").ret(Nil());
        fine.state("Busy").function("OnInit").ret(Nil());
        EXPECT_TRUE(parseBytes(fine.build().mBytes).mOk);
    }

    TEST(PapyrusReader, an_auto_property_must_name_a_variable_that_exists)
    {
        ScriptBuilder script("AutoMissing");
        script.autoProperty("Count", "Int", Int(1));
        EXPECT_TRUE(parseBytes(script.build().mBytes).mOk);

        // Point the property at a string that is not a variable: one unused string at the end of the table.
        Options options;
        options.mPadStrings = 1;
        Built padded = script.build(options);
        const Pex::ParseResult padResult = parseBytes(padded.mBytes);
        ASSERT_TRUE(padResult.mOk);
        const std::size_t padIndex = padResult.mFile.mStrings.size() - 1;
        // After the property count: name, type and doc (2 bytes each), flags (4), property flags (1), then the index.
        const std::size_t at = padded.mLayout.mPropertyCount + 2 + 6 + 4 + 1;
        patch16(padded.mBytes, at, static_cast<std::uint16_t>(padIndex), true);
        const Pex::ParseResult result = parseBytes(padded.mBytes);
        EXPECT_FALSE(result.mOk);
        EXPECT_TRUE(contains(messages(result), "property")) << messages(result);
        EXPECT_TRUE(contains(messages(result), "variable")) << messages(result);
    }

    TEST(PapyrusReader, an_identifier_that_names_nothing_is_refused_unless_a_parent_could_own_it)
    {
        ScriptBuilder alone("Alone");
        alone.function("F").assign(Id("nothing"), Int(1)).ret(Nil());
        const Pex::ParseResult refused = parseBytes(alone.build().mBytes);
        EXPECT_FALSE(refused.mOk);
        EXPECT_TRUE(contains(messages(refused), "nothing")) << messages(refused);
        EXPECT_TRUE(contains(messages(refused), "Alone.F@0")) << messages(refused);

        ScriptBuilder child("Child");
        child.parent("Base");
        child.function("F").assign(Id("inheritedVariable"), Int(1)).ret(Nil());
        EXPECT_TRUE(parseBytes(child.build().mBytes).mOk);

        ScriptBuilder selfUse("SelfUse");
        selfUse.function("F").local("::r", "Int").callMethod("Other", Id("self"), Id("::r")).ret(Nil());
        EXPECT_TRUE(parseBytes(selfUse.build().mBytes).mOk);
    }

    // ---------------------------------------------------------------- opcodes

    TEST(PapyrusReader, guard_opcodes_are_refused_with_a_clear_message)
    {
        for (const std::uint8_t opcode : { Op::LockGuards, Op::UnlockGuards, Op::TryLockGuards })
        {
            ScriptBuilder script("Guards");
            script.function("F").op(opcode, { Int(0) }).ret(Nil());
            const Pex::ParseResult result = parseBytes(script.build(optionsFor(Target::Fallout4)).mBytes);
            EXPECT_FALSE(result.mOk);
            EXPECT_TRUE(contains(messages(result), "guard")) << messages(result);
            EXPECT_TRUE(contains(messages(result), "Guards.F@0")) << messages(result);
        }
    }

    TEST(PapyrusReader, an_unknown_opcode_is_refused)
    {
        for (const std::uint8_t opcode : { std::uint8_t{ 51 }, std::uint8_t{ 99 }, std::uint8_t{ 255 } })
        {
            ScriptBuilder script("Unknown");
            script.function("F").op(opcode, {}).ret(Nil());
            const Pex::ParseResult result = parseBytes(script.build(optionsFor(Target::Fallout4)).mBytes);
            EXPECT_FALSE(result.mOk);
            EXPECT_TRUE(contains(messages(result), "opcode")) << messages(result);
        }
    }

    TEST(PapyrusReader, fallout4_opcodes_are_refused_in_a_skyrim_file_and_accepted_in_a_fallout4_file)
    {
        // "is" exists only in Fallout 4's version 3.9.
        ScriptBuilder script("IsTest");
        script.function("F").local("::b", "Bool").op(Op::Is, { Id("::b"), Id("self"), Str("Quest") }).ret(Nil());
        const Pex::ParseResult skyrim = parseBytes(script.build(optionsFor(Target::SkyrimSE)).mBytes);
        EXPECT_FALSE(skyrim.mOk);
        EXPECT_TRUE(contains(messages(skyrim), "opcode")) << messages(skyrim);
        EXPECT_TRUE(parseBytes(script.build(optionsFor(Target::Fallout4)).mBytes).mOk);
    }

    TEST(PapyrusReader, the_type_name_of_an_is_test_may_be_an_identifier_or_a_string)
    {
        // An installed Fallout 4 mod script writes it as an identifier that names no variable.
        for (const bool asIdentifier : { false, true })
        {
            ScriptBuilder script("IsName");
            script.function("F")
                .local("::b", "Bool")
                .op(Op::Is, { Id("::b"), Id("self"), asIdentifier ? Id("keyword") : Str("keyword") })
                .ret(Nil());
            EXPECT_TRUE(parseBytes(script.build(optionsFor(Target::Fallout4)).mBytes).mOk);
        }
    }

    TEST(PapyrusReader, every_opcode_of_the_two_games_is_read_with_its_operand_layout)
    {
        // One instruction per opcode with the operand counts the reader expects (pexfile.hpp); this pins the layout
        // table: a wrong count would desynchronise the next instruction and fail the file.
        struct Shape
        {
            std::uint8_t mOpcode = 0;
            int mOperands = 0;
        };
        const Shape shapes[] = { { Op::Nop, 0 }, { Op::IAdd, 3 }, { Op::FAdd, 3 }, { Op::ISub, 3 }, { Op::FSub, 3 },
            { Op::IMul, 3 }, { Op::FMul, 3 }, { Op::IDiv, 3 }, { Op::FDiv, 3 }, { Op::IMod, 3 }, { Op::Not, 2 },
            { Op::INeg, 2 }, { Op::FNeg, 2 }, { Op::Assign, 2 }, { Op::Cast, 2 }, { Op::CompareEq, 3 },
            { Op::CompareLt, 3 }, { Op::CompareLe, 3 }, { Op::CompareGt, 3 }, { Op::CompareGe, 3 }, { Op::StrCat, 3 },
            { Op::PropGet, 3 }, { Op::PropSet, 3 }, { Op::ArrayCreate, 2 }, { Op::ArrayLength, 2 },
            { Op::ArrayGetElement, 3 }, { Op::ArraySetElement, 3 }, { Op::ArrayFindElement, 4 },
            { Op::ArrayRFindElement, 4 } };
        const Shape fallout4Only[] = { { Op::Is, 3 }, { Op::StructCreate, 1 }, { Op::StructGet, 3 },
            { Op::StructSet, 3 }, { Op::ArrayFindStruct, 5 }, { Op::ArrayRFindStruct, 5 }, { Op::ArrayAdd, 3 },
            { Op::ArrayInsert, 3 }, { Op::ArrayRemoveLast, 1 }, { Op::ArrayRemove, 3 }, { Op::ArrayClear, 1 },
            { Op::ArrayGetAllMatchingStructs, 4 } };
        for (const Target target : kTargets)
        {
            ScriptBuilder script("AllOps");
            script.function("F").local("::a", "Int").local("::b", "Int").local("::c", "Int").local("::d", "Int");
            script.local("::e", "Int");
            std::vector<Shape> all(std::begin(shapes), std::end(shapes));
            if (target == Target::Fallout4)
            {
                all.insert(all.end(), std::begin(fallout4Only), std::end(fallout4Only));
            }
            for (const Shape& shape : all)
            {
                std::vector<Operand> operands;
                const char* names[] = { "::a", "::b", "::c", "::d", "::e" };
                for (int i = 0; i < shape.mOperands; ++i)
                {
                    operands.push_back(Id(names[i]));
                }
                script.op(shape.mOpcode, operands);
            }
            script.callMethod("M", Id("self"), Id("::a"), { Int(1) });
            script.callParent("P", Id("::a"), {});
            script.callStatic("C", "S", Id("::a"), { Id("::b"), Id("::c") });
            script.ret(Nil());
            const Pex::ParseResult result = parseBytes(script.build(optionsFor(target)).mBytes);
            EXPECT_TRUE(result.mOk) << messages(result);
        }
    }

    // ---------------------------------------------------------------- the library's own rules

    TEST(PapyrusReader, the_sources_use_no_raw_new_or_delete)
    {
        namespace fs = std::filesystem;
        const fs::path directory
            = fs::path(__FILE__).parent_path() / ".." / ".." / ".." / "components" / "script_papyrus";
        std::size_t checked = 0;
        for (const auto& entry : fs::directory_iterator(directory))
        {
            const std::string extension = entry.path().extension().string();
            if (extension != ".cpp" && extension != ".hpp")
            {
                continue;
            }
            std::ifstream in(entry.path(), std::ios::binary);
            std::string line;
            while (std::getline(in, line))
            {
                const std::size_t comment = line.find("//");
                const std::string code = comment == std::string::npos ? line : line.substr(0, comment);
                for (const char* word : { " new ", " delete ", "(new ", "=new ", "delete[" })
                {
                    EXPECT_EQ(code.find(word), std::string::npos) << entry.path().filename() << ": " << line;
                }
            }
            ++checked;
        }
        EXPECT_GT(checked, 3u);
    }
}
