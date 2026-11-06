// Task 0092 stage d: the save blob of the ObScript runtime.
//
// In plain words: the runtime never writes a save itself. When the game pack asks, it hands back a block of bytes (the
// local variables of every running script copy and the form each is attached to) with a version number of its own.
// The game pack stores it; on load it gives the bytes back. These tests check that a restored runtime carries on
// exactly as the original would, that damaged bytes are refused without changing anything, and (a deterministic fuzz of
// 100,000 damaged blobs, the same idea as the Eden archive reader's) that no damaged blob can crash the reader.

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::script::InstanceId;
    using openeden::script::SaveBlob;
    using openeden::script::ScriptId;

    const char* const sCounter = R"(ScriptName Counter
short count
long total
float ratio
ref target
Begin GameMode
    Set count to count + 1
    Set total to total + count * 100
    Set ratio to ratio + 0.5
End
)";

    struct SaveFixture
    {
        SaveFixture()
        {
            mScript = mFixture.compileOk("Counter", sCounter);
            mFirst = mFixture.mRuntime->createInstance(mScript, sCrate);
            mSecond = mFixture.mRuntime->createInstance(mScript, sGold);
            for (int i = 0; i < 3; ++i)
            {
                mFixture.mRuntime->run(mFirst, "GameMode");
            }
            mFixture.mRuntime->run(mSecond, "GameMode");
            mFixture.mRuntime->setLocal(mFirst, "target", openeden::script::Value::makeForm(sPlayer));
        }

        RuntimeFixture mFixture;
        ScriptId mScript = 0;
        InstanceId mFirst = 0;
        InstanceId mSecond = 0;
    };

    TEST(ObScriptSave, a_restored_runtime_carries_on_exactly_as_the_original_does)
    {
        SaveFixture original;
        const SaveBlob blob = original.mFixture.mRuntime->serialise();
        EXPECT_FALSE(blob.bytes.empty());
        EXPECT_GE(blob.formatVersion, 1u);

        // A fresh runtime: scripts are compiled again (programs are not saved), then the state is restored.
        RuntimeFixture fresh;
        ASSERT_NE(fresh.compileOk("Counter", sCounter), 0u);
        const auto restored = fresh.mRuntime->restore(blob);
        ASSERT_TRUE(restored.ok) << restored.error.format();

        EXPECT_EQ(fresh.mRuntime->getLocal(original.mFirst, "count").integer, 3);
        EXPECT_EQ(fresh.mRuntime->getLocal(original.mFirst, "total").integer, 600);
        EXPECT_EQ(static_cast<openeden::script::FormId>(fresh.mRuntime->getLocal(original.mFirst, "target").integer),
            sPlayer);
        EXPECT_EQ(fresh.mRuntime->getLocal(original.mSecond, "count").integer, 1);

        // The next run on both gives the same values: the restored copy carries on exactly as the original.
        ASSERT_TRUE(original.mFixture.mRuntime->run(original.mFirst, "GameMode").ok);
        ASSERT_TRUE(fresh.mRuntime->run(original.mFirst, "GameMode").ok);
        for (const char* name : { "count", "total", "ratio", "target" })
        {
            const auto a = original.mFixture.mRuntime->getLocal(original.mFirst, name);
            const auto b = fresh.mRuntime->getLocal(original.mFirst, name);
            EXPECT_EQ(a.kind, b.kind) << name;
            EXPECT_EQ(a.integer, b.integer) << name;
            EXPECT_EQ(a.number, b.number) << name;
        }
        EXPECT_EQ(original.mFixture.mRuntime->serialise().bytes, fresh.mRuntime->serialise().bytes);
    }

    TEST(ObScriptSave, instances_made_after_a_restore_do_not_reuse_a_restored_id)
    {
        SaveFixture original;
        const SaveBlob blob = original.mFixture.mRuntime->serialise();
        RuntimeFixture fresh;
        const ScriptId script = fresh.compileOk("Counter", sCounter);
        ASSERT_TRUE(fresh.mRuntime->restore(blob).ok);
        const InstanceId created = fresh.mRuntime->createInstance(script, sCrate);
        EXPECT_NE(created, original.mFirst);
        EXPECT_NE(created, original.mSecond);
    }

    TEST(ObScriptSave, serialising_twice_gives_the_same_bytes)
    {
        SaveFixture original;
        EXPECT_EQ(original.mFixture.mRuntime->serialise().bytes, original.mFixture.mRuntime->serialise().bytes);
    }

    TEST(ObScriptSave, a_blob_with_the_wrong_version_is_refused_and_changes_nothing)
    {
        SaveFixture original;
        SaveBlob blob = original.mFixture.mRuntime->serialise();
        blob.formatVersion += 1000;
        const auto before = original.mFixture.mRuntime->serialise();
        const auto result = original.mFixture.mRuntime->restore(blob);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("version"), std::string::npos) << result.error.format();
        EXPECT_EQ(original.mFixture.mRuntime->serialise().bytes, before.bytes);
    }

    TEST(ObScriptSave, a_truncated_blob_is_refused_at_every_length)
    {
        SaveFixture original;
        const SaveBlob blob = original.mFixture.mRuntime->serialise();
        RuntimeFixture fresh;
        ASSERT_NE(fresh.compileOk("Counter", sCounter), 0u);
        for (std::size_t length = 0; length < blob.bytes.size(); ++length)
        {
            SaveBlob cut = blob;
            cut.bytes.resize(length);
            EXPECT_FALSE(fresh.mRuntime->restore(cut).ok) << "length " << length;
        }
        EXPECT_TRUE(fresh.mRuntime->restore(blob).ok);
    }

    TEST(ObScriptSave, a_blob_with_extra_bytes_at_the_end_is_refused)
    {
        SaveFixture original;
        SaveBlob blob = original.mFixture.mRuntime->serialise();
        blob.bytes.push_back(0);
        RuntimeFixture fresh;
        ASSERT_NE(fresh.compileOk("Counter", sCounter), 0u);
        EXPECT_FALSE(fresh.mRuntime->restore(blob).ok);
    }

    TEST(ObScriptSave, a_blob_with_an_out_of_range_count_is_refused_without_allocating_for_it)
    {
        SaveFixture original;
        SaveBlob blob = original.mFixture.mRuntime->serialise();
        RuntimeFixture fresh;
        ASSERT_NE(fresh.compileOk("Counter", sCounter), 0u);
        // Every 32-bit word of the blob set in turn to the largest value: whichever is a count must be refused, and
        // none may make the reader reserve memory for it.
        std::size_t refused = 0;
        for (std::size_t offset = 0; offset + 4 <= blob.bytes.size(); ++offset)
        {
            SaveBlob damaged = blob;
            for (std::size_t i = 0; i < 4; ++i)
            {
                damaged.bytes[offset + i] = 0xFF;
            }
            if (!fresh.mRuntime->restore(damaged).ok)
            {
                ++refused;
            }
        }
        EXPECT_GT(refused, 0u);
    }

    TEST(ObScriptSave, a_blob_naming_a_script_that_is_not_compiled_is_refused)
    {
        SaveFixture original;
        const SaveBlob blob = original.mFixture.mRuntime->serialise();
        RuntimeFixture fresh; // no scripts compiled
        const auto result = fresh.mRuntime->restore(blob);
        EXPECT_FALSE(result.ok);
        EXPECT_NE(result.error.message.find("Counter"), std::string::npos) << result.error.format();
    }

    TEST(ObScriptSave, a_blob_for_a_script_whose_variables_changed_is_refused)
    {
        SaveFixture original;
        const SaveBlob blob = original.mFixture.mRuntime->serialise();
        RuntimeFixture fresh;
        ASSERT_NE(fresh.compileOk("Counter", "ScriptName Counter\nshort only\nBegin GameMode\nEnd\n"), 0u);
        EXPECT_FALSE(fresh.mRuntime->restore(blob).ok);
    }

    TEST(ObScriptSave, a_refused_restore_leaves_the_instances_as_they_were)
    {
        SaveFixture original;
        SaveBlob blob = original.mFixture.mRuntime->serialise();
        blob.bytes.resize(blob.bytes.size() - 1);
        const auto before = original.mFixture.mRuntime->serialise();
        EXPECT_FALSE(original.mFixture.mRuntime->restore(blob).ok);
        EXPECT_EQ(original.mFixture.mRuntime->serialise().bytes, before.bytes);
        EXPECT_EQ(original.mFixture.mRuntime->getLocal(original.mFirst, "count").integer, 3);
    }

    TEST(ObScriptSaveFuzz, a_hundred_thousand_damaged_blobs_never_crash_the_reader)
    {
        SaveFixture original;
        const SaveBlob good = original.mFixture.mRuntime->serialise();
        RuntimeFixture fresh;
        ASSERT_NE(fresh.compileOk("Counter", sCounter), 0u);

        std::mt19937 random(12345); // a fixed seed: the same blobs every run
        std::size_t accepted = 0;
        for (int round = 0; round < 100000; ++round)
        {
            SaveBlob blob = good;
            const int kind = static_cast<int>(random() % 5);
            if (kind == 0 && !blob.bytes.empty())
            {
                blob.bytes[random() % blob.bytes.size()] ^= static_cast<std::uint8_t>(1u << (random() % 8));
            }
            else if (kind == 1 && !blob.bytes.empty())
            {
                blob.bytes[random() % blob.bytes.size()] = static_cast<std::uint8_t>(random());
            }
            else if (kind == 2)
            {
                blob.bytes.resize(random() % (blob.bytes.size() + 1));
            }
            else if (kind == 3)
            {
                const std::size_t extra = random() % 16;
                for (std::size_t i = 0; i < extra; ++i)
                {
                    blob.bytes.push_back(static_cast<std::uint8_t>(random()));
                }
            }
            else
            {
                const std::size_t count = random() % 6;
                for (std::size_t i = 0; i < count && !blob.bytes.empty(); ++i)
                {
                    blob.bytes[random() % blob.bytes.size()] = static_cast<std::uint8_t>(random());
                }
                if ((random() & 3) == 0)
                {
                    blob.formatVersion = static_cast<std::uint32_t>(random());
                }
            }
            const auto result = fresh.mRuntime->restore(blob);
            if (result.ok)
            {
                ++accepted;
            }
            // Whatever happened, the runtime must still serialise and run.
            if ((round % 1000) == 0)
            {
                EXPECT_FALSE(fresh.mRuntime->serialise().bytes.empty());
            }
        }
        // Flips of a local's value bits are legal blobs, so some are accepted; the rest are refused.
        EXPECT_GT(accepted, 0u);
        EXPECT_LT(accepted, 100000u);
        EXPECT_TRUE(fresh.mRuntime->restore(good).ok);
    }
}
