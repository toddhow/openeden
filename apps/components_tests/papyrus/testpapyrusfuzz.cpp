// Task 0093 stage b: a deterministic fuzz of the .pex reader.
//
// In plain words: 100,000 damaged copies of valid generated files (flipped bytes, cut, grown, counts set to huge
// numbers, chunks copied and swapped) are read. Each must give either a clean accept or a located error, never a
// crash or a hang; and when the reader accepts a damaged file, the model it returns must be safe to use: every string
// index inside the table, every jump inside its function, every operand run inside the operand pool. The sequence of
// damage comes from a fixed-seed generator, so a failure repeats.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "papyrustestutil.hpp"

namespace
{
    using namespace PapyrusTest;
    using namespace PexAsm;
    namespace Pex = ScriptPapyrus::Pex;

    class Rng
    {
    public:
        explicit Rng(std::uint64_t seed)
            : mState(seed)
        {
        }
        std::uint64_t next()
        {
            mState ^= mState << 13;
            mState ^= mState >> 7;
            mState ^= mState << 17;
            return mState;
        }
        std::size_t below(std::size_t limit) { return limit == 0 ? 0 : static_cast<std::size_t>(next() % limit); }

    private:
        std::uint64_t mState;
    };

    void mutateOnce(Bytes& bytes, Rng& rng)
    {
        if (bytes.empty())
        {
            bytes.push_back(static_cast<std::uint8_t>(rng.next()));
            return;
        }
        const std::size_t position = rng.below(bytes.size());
        switch (rng.below(10))
        {
            case 0:
                bytes[position] ^= static_cast<std::uint8_t>(1u << rng.below(8));
                break;
            case 1:
                bytes[position] = static_cast<std::uint8_t>(rng.next());
                break;
            case 2:
            {
                const std::size_t count = 1 + rng.below(8);
                for (std::size_t i = 0; i < count; ++i)
                {
                    bytes[rng.below(bytes.size())] = static_cast<std::uint8_t>(rng.next());
                }
                break;
            }
            case 3:
                bytes.resize(position);
                break;
            case 4:
            {
                const std::size_t count = 1 + rng.below(16);
                std::vector<std::uint8_t> extra(count);
                for (std::uint8_t& b : extra)
                {
                    b = static_cast<std::uint8_t>(rng.next());
                }
                bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(position), extra.begin(), extra.end());
                break;
            }
            case 5:
            {
                const std::size_t count = std::min<std::size_t>(1 + rng.below(16), bytes.size() - position);
                bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(position),
                    bytes.begin() + static_cast<std::ptrdiff_t>(position + count));
                break;
            }
            case 6:
                if (position + 1 < bytes.size())
                {
                    bytes[position] = 0xFF;
                    bytes[position + 1] = 0xFF;
                }
                break;
            case 7:
                if (position + 1 < bytes.size())
                {
                    bytes[position] = 0;
                    bytes[position + 1] = 0;
                }
                break;
            case 8:
                if (position + 3 < bytes.size())
                {
                    static const std::uint8_t patterns[3][4]
                        = { { 0xFF, 0xFF, 0xFF, 0xFF }, { 0x7F, 0xFF, 0xFF, 0xFF }, { 0x80, 0x00, 0x00, 0x00 } };
                    const auto& pattern = patterns[rng.below(3)];
                    for (std::size_t i = 0; i < 4; ++i)
                    {
                        bytes[position + i] = pattern[i];
                    }
                }
                break;
            default:
            {
                const std::size_t count = std::min<std::size_t>(1 + rng.below(32), bytes.size() - position);
                const std::size_t to = rng.below(bytes.size());
                std::vector<std::uint8_t> chunk(bytes.begin() + static_cast<std::ptrdiff_t>(position),
                    bytes.begin() + static_cast<std::ptrdiff_t>(position + count));
                for (std::size_t i = 0; i < chunk.size() && to + i < bytes.size(); ++i)
                {
                    bytes[to + i] = chunk[i];
                }
                break;
            }
        }
    }

    bool validString(const Pex::File& file, std::uint32_t index)
    {
        return index < file.mStrings.size();
    }

    void checkValue(const Pex::File& file, const Pex::Value& value, bool& ok)
    {
        if (value.mType == Pex::ValueType::Identifier || value.mType == Pex::ValueType::String)
        {
            ok = ok && validString(file, value.mString);
        }
    }

    void checkFunction(const Pex::File& file, const Pex::Function& function, bool named, bool& ok)
    {
        if (named)
        {
            ok = ok && validString(file, function.mName);
        }
        ok = ok && validString(file, function.mReturnType) && validString(file, function.mDoc);
        for (const auto& p : function.mParameters)
        {
            ok = ok && validString(file, p.mName) && validString(file, p.mType);
        }
        for (const auto& p : function.mLocals)
        {
            ok = ok && validString(file, p.mName) && validString(file, p.mType);
        }
        const std::int64_t count = static_cast<std::int64_t>(function.mInstructions.size());
        for (std::size_t i = 0; i < function.mInstructions.size(); ++i)
        {
            const Pex::Instruction& instr = function.mInstructions[i];
            const bool call = instr.mOpcode == Pex::Op::CallMethod || instr.mOpcode == Pex::Op::CallParent
                || instr.mOpcode == Pex::Op::CallStatic;
            const std::size_t total = instr.mFixedCount + (call ? 1u + instr.mExtraCount : 0u);
            ok = ok && instr.mFirstOperand + total <= function.mOperands.size();
            if (!ok)
            {
                return;
            }
            for (std::size_t k = 0; k < total; ++k)
            {
                checkValue(file, function.mOperands[instr.mFirstOperand + k], ok);
            }
            if (instr.mOpcode == Pex::Op::Jmp || instr.mOpcode == Pex::Op::JmpT || instr.mOpcode == Pex::Op::JmpF)
            {
                const Pex::Value& offset = function.mOperands[instr.mFirstOperand + instr.mFixedCount - 1];
                const std::int64_t target = static_cast<std::int64_t>(i) + offset.mInteger;
                ok = ok && offset.mType == Pex::ValueType::Integer && target >= 0 && target <= count;
            }
            ok = ok && instr.mOpcode <= (file.mGame == Pex::Game::Fallout4 ? 47 : 35);
        }
    }

    // True when every index the model holds is safe to use.
    bool modelIsSafe(const Pex::File& file)
    {
        bool ok = !file.mObjects.empty();
        for (const Pex::Object& object : file.mObjects)
        {
            ok = ok && validString(file, object.mName) && validString(file, object.mParent)
                && validString(file, object.mDoc) && validString(file, object.mAutoState);
            for (const auto& s : object.mStructs)
            {
                ok = ok && validString(file, s.mName);
                for (const auto& m : s.mMembers)
                {
                    ok = ok && validString(file, m.mName) && validString(file, m.mType);
                    checkValue(file, m.mInitial, ok);
                }
            }
            for (const auto& v : object.mVariables)
            {
                ok = ok && validString(file, v.mName) && validString(file, v.mType);
                checkValue(file, v.mInitial, ok);
            }
            for (const auto& p : object.mProperties)
            {
                ok = ok && validString(file, p.mName) && validString(file, p.mType);
                if ((p.mFlags & Pex::Property::kAuto) != 0)
                {
                    ok = ok && validString(file, p.mAutoVariable);
                }
                else
                {
                    if ((p.mFlags & Pex::Property::kRead) != 0)
                    {
                        checkFunction(file, p.mGetter, false, ok);
                    }
                    if ((p.mFlags & Pex::Property::kWrite) != 0)
                    {
                        checkFunction(file, p.mSetter, false, ok);
                    }
                }
            }
            for (const auto& s : object.mStates)
            {
                ok = ok && validString(file, s.mName);
                for (const auto& f : s.mFunctions)
                {
                    checkFunction(file, f, true, ok);
                }
            }
        }
        return ok;
    }

    std::vector<Bytes> corpus()
    {
        std::vector<Bytes> files;
        for (const Target target : { Target::SkyrimSE, Target::Fallout4 })
        {
            for (const SizeConv conv :
                { SizeConv::IncludesSelf, SizeConv::ExcludesSelf, SizeConv::ExcludesSelfOtherOrder })
            {
                Options options = optionsFor(target);
                options.mSizeConv = conv;
                files.push_back(richScript(target).build(options).mBytes);
            }
            Options debug = optionsFor(target);
            debug.mDebugInfo = true;
            debug.mShuffleSeed = 99;
            files.push_back(richScript(target).build(debug).mBytes);
            Options extended = optionsFor(target);
            extended.mExtendedStringCount = true;
            files.push_back(richScript(target).build(extended).mBytes);
        }
        return files;
    }

    TEST(PapyrusReaderFuzz, the_corpus_itself_is_valid_and_safe)
    {
        for (const Bytes& bytes : corpus())
        {
            const Pex::ParseResult result = parseBytes(bytes);
            ASSERT_TRUE(result.mOk) << messages(result);
            EXPECT_TRUE(modelIsSafe(result.mFile));
        }
    }

    TEST(PapyrusReaderFuzz, one_hundred_thousand_mutations_give_a_located_error_or_a_safe_model)
    {
        const std::vector<Bytes> files = corpus();
        Rng rng(0x0093'5EED'F00Dull);
        std::size_t accepted = 0;
        std::size_t refused = 0;
        std::size_t unsafe = 0;
        std::size_t unlocated = 0;
        std::size_t nondeterministic = 0;
        constexpr std::size_t kCases = 100000;
        for (std::size_t i = 0; i < kCases; ++i)
        {
            Bytes bytes = files[rng.below(files.size())];
            const std::size_t rounds = 1 + rng.below(3);
            for (std::size_t r = 0; r < rounds; ++r)
            {
                mutateOnce(bytes, rng);
            }
            const Pex::ParseResult result = parseBytes(bytes);
            if (result.mOk)
            {
                ++accepted;
                if (!modelIsSafe(result.mFile))
                {
                    ++unsafe;
                }
            }
            else
            {
                ++refused;
                if (result.mReports.empty() || result.mReports[0].message.empty()
                    || result.mReports[0].script != "test.pex")
                {
                    ++unlocated;
                }
            }
            if (i % 50 == 0 && messages(parseBytes(bytes)) != messages(result))
            {
                ++nondeterministic;
            }
        }
        std::cout << "PAPYRUS-FUZZ cases " << kCases << ", accepted " << accepted << ", refused " << refused << '\n';
        EXPECT_EQ(unsafe, 0u);
        EXPECT_EQ(unlocated, 0u);
        EXPECT_EQ(nondeterministic, 0u);
        EXPECT_GT(accepted, 100u) << "the damage was too heavy to show anything accepted";
        EXPECT_GT(refused, 1000u);
        EXPECT_EQ(accepted + refused, kCases);
    }
}
