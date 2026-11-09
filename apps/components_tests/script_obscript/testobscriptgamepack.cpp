// Task 0092 item 4: the glue between a game pack and the ObScript runtime (task 0084's game services).
//
// In plain words: a game pack owns an ObScript runtime. The glue does two things for it. It declares the language
// "obscript" through the game script host service, and it registers one save chunk, "script.obscript", whose bytes are
// the runtime's own versioned save blob. These tests play the part of a game pack (StubGamePack below): it is started
// with the two services the host gives game packs (here the real implementations from components/extension), saves,
// and a second pack loads what the first one saved. Damaged and oversized chunks must be refused cleanly, and a pack
// that is not a game pack (no services) must get a clear message instead of a crash.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <components/extension/game_services.hpp>
#include <components/extension/save_chunk_store.hpp>
#include <components/script_obscript/gamepackglue.hpp>
#include <components/script_obscript/obscriptlimits.hpp>

#include "stubworld.hpp"

namespace
{
    using namespace ObScriptTest;
    using openeden::extension::SaveChunkServiceImpl;
    using openeden::extension::SavedChunk;
    using openeden::extension::ScriptHostServiceImpl;
    using openeden::script::InstanceId;
    using openeden::script::ScriptId;
    using openeden::script::Value;

    const char* const sCounter = R"(ScriptName Counter
short count
long total
ref target
Begin GameMode
    Set count to count + 1
    Set total to total + count * 100
End
)";

    // The host's side of SaveChunkService for one pack: it forwards to the real store under the pack's id.
    class PackSaveChunks final : public openeden::sdk::SaveChunkService
    {
    public:
        PackSaveChunks(SaveChunkServiceImpl& store, std::string packId)
            : mStore(store)
            , mPackId(std::move(packId))
        {
        }

        bool registerHandler(openeden::sdk::SaveChunkHandler& handler) override
        {
            return mStore.registerHandler(mPackId, handler);
        }

    private:
        SaveChunkServiceImpl& mStore;
        std::string mPackId;
    };

    // What the host owns for one session: the two game services.
    struct Host
    {
        ScriptHostServiceImpl mScriptHost;
        SaveChunkServiceImpl mSaveChunks;
        PackSaveChunks mPackChunks{ mSaveChunks, "stubpack" };
    };

    // A game pack in miniature: it owns the world, the runtime and the chunk handler, as a real one would, and attaches
    // them in start().
    struct StubGamePack
    {
        explicit StubGamePack(Host* host)
            : mFixture()
        {
            mChunk = std::make_unique<ScriptObscript::ObScriptSaveChunk>(*mFixture.mRuntime);
            if (host != nullptr)
            {
                mResult = ScriptObscript::attachToGamePack(&host->mScriptHost, &host->mPackChunks, *mChunk);
            }
            else
            {
                mResult = ScriptObscript::attachToGamePack(nullptr, nullptr, *mChunk);
            }
        }

        RuntimeFixture mFixture;
        std::unique_ptr<ScriptObscript::ObScriptSaveChunk> mChunk;
        ScriptObscript::GlueResult mResult;
    };

    // Compile the counter script and run it, so that there is state to save.
    InstanceId makeState(StubGamePack& pack, int runs, ScriptId* script = nullptr)
    {
        const ScriptId id = pack.mFixture.compileOk("Counter", sCounter);
        if (script != nullptr)
        {
            *script = id;
        }
        const InstanceId instance = pack.mFixture.mRuntime->createInstance(id, sCrate);
        for (int i = 0; i < runs; ++i)
        {
            pack.mFixture.mRuntime->run(instance, "GameMode");
        }
        pack.mFixture.mRuntime->setLocal(instance, "target", Value::makeForm(sPlayer));
        return instance;
    }

    TEST(ObScriptGamePack, attaching_declares_the_language_and_registers_the_chunk)
    {
        Host host;
        StubGamePack pack(&host);
        ASSERT_TRUE(pack.mResult.ok) << pack.mResult.error;
        EXPECT_TRUE(host.mScriptHost.isLanguageDeclared("obscript"));
        EXPECT_EQ(host.mSaveChunks.handlerCount(), 1u);
        EXPECT_EQ(pack.mChunk->chunkName(), "script.obscript");
        EXPECT_EQ(pack.mChunk->chunkVersion(), ScriptObscript::Limits::kSaveFormatVersion);
    }

    TEST(ObScriptGamePack, a_pack_without_the_game_services_gets_a_clear_message)
    {
        StubGamePack pack(nullptr);
        EXPECT_FALSE(pack.mResult.ok);
        EXPECT_NE(pack.mResult.error.find("game pack"), std::string::npos) << pack.mResult.error;
    }

    TEST(ObScriptGamePack, a_missing_save_service_alone_is_refused_before_anything_is_declared)
    {
        Host host;
        RuntimeFixture fixture;
        ScriptObscript::ObScriptSaveChunk chunk(*fixture.mRuntime);
        const auto result = ScriptObscript::attachToGamePack(&host.mScriptHost, nullptr, chunk);
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(host.mScriptHost.isLanguageDeclared("obscript"));
        const auto noHost = ScriptObscript::attachToGamePack(nullptr, &host.mPackChunks, chunk);
        EXPECT_FALSE(noHost.ok);
        EXPECT_EQ(host.mSaveChunks.handlerCount(), 0u);
    }

    TEST(ObScriptGamePack, attaching_twice_is_refused_with_a_reason)
    {
        Host host;
        StubGamePack first(&host);
        ASSERT_TRUE(first.mResult.ok) << first.mResult.error;
        StubGamePack second(&host);
        EXPECT_FALSE(second.mResult.ok);
        EXPECT_FALSE(second.mResult.error.empty());
        EXPECT_EQ(host.mSaveChunks.handlerCount(), 1u);
    }

    TEST(ObScriptGamePack, the_saved_chunk_carries_the_runtimes_blob)
    {
        Host host;
        StubGamePack pack(&host);
        ASSERT_TRUE(pack.mResult.ok);
        makeState(pack, 3);
        const auto collected = host.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok) << collected.error;
        ASSERT_EQ(collected.chunks.size(), 1u);
        const SavedChunk& chunk = collected.chunks.front();
        EXPECT_EQ(chunk.packId, "stubpack");
        EXPECT_EQ(chunk.name, "script.obscript");
        EXPECT_EQ(chunk.version, ScriptObscript::Limits::kSaveFormatVersion);
        const auto blob = pack.mFixture.mRuntime->serialise();
        ASSERT_EQ(chunk.bytes.size(), blob.bytes.size());
        for (std::size_t i = 0; i < blob.bytes.size(); ++i)
        {
            ASSERT_EQ(static_cast<std::uint8_t>(chunk.bytes[i]), blob.bytes[i]) << i;
        }
    }

    TEST(ObScriptGamePack, saving_twice_gives_the_same_bytes)
    {
        Host host;
        StubGamePack pack(&host);
        ASSERT_TRUE(pack.mResult.ok);
        makeState(pack, 2);
        const auto a = host.mSaveChunks.collect();
        const auto b = host.mSaveChunks.collect();
        ASSERT_TRUE(a.ok);
        ASSERT_TRUE(b.ok);
        EXPECT_EQ(openeden::extension::encodeChunks(a.chunks), openeden::extension::encodeChunks(b.chunks));
    }

    TEST(ObScriptGamePack, a_second_pack_loads_what_the_first_one_saved)
    {
        Host first;
        StubGamePack saver(&first);
        ASSERT_TRUE(saver.mResult.ok);
        const InstanceId instance = makeState(saver, 3);
        const auto collected = first.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        // Through the byte codec the engine embeds in the save, as a real save would travel.
        const auto bytes = openeden::extension::encodeChunks(collected.chunks);
        std::vector<SavedChunk> loaded;
        std::string error;
        ASSERT_TRUE(openeden::extension::decodeChunks(bytes, loaded, error)) << error;

        Host second;
        StubGamePack loader(&second);
        ASSERT_TRUE(loader.mResult.ok);
        ASSERT_NE(loader.mFixture.compileOk("Counter", sCounter), 0u);
        const auto restored = second.mSaveChunks.restore(loaded);
        ASSERT_TRUE(restored.ok) << restored.error;

        EXPECT_EQ(loader.mFixture.mRuntime->getLocal(instance, "count").integer, 3);
        EXPECT_EQ(loader.mFixture.mRuntime->getLocal(instance, "total").integer, 600);
        EXPECT_EQ(static_cast<openeden::script::FormId>(loader.mFixture.mRuntime->getLocal(instance, "target").integer),
            sPlayer);
        ASSERT_TRUE(saver.mFixture.mRuntime->run(instance, "GameMode").ok);
        ASSERT_TRUE(loader.mFixture.mRuntime->run(instance, "GameMode").ok);
        EXPECT_EQ(saver.mFixture.mRuntime->serialise().bytes, loader.mFixture.mRuntime->serialise().bytes);
        EXPECT_TRUE(loader.mChunk->lastError().empty());
    }

    TEST(ObScriptGamePack, a_truncated_chunk_is_refused_and_changes_nothing)
    {
        Host first;
        StubGamePack saver(&first);
        makeState(saver, 2);
        auto collected = first.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        collected.chunks.front().bytes.resize(collected.chunks.front().bytes.size() / 2);

        Host second;
        StubGamePack loader(&second);
        ScriptId script = 0;
        const InstanceId instance = makeState(loader, 1, &script);
        const auto before = loader.mFixture.mRuntime->serialise().bytes;
        const auto restored = second.mSaveChunks.restore(collected.chunks);
        EXPECT_FALSE(restored.ok);
        EXPECT_NE(restored.error.find("stubpack"), std::string::npos) << restored.error;
        EXPECT_FALSE(loader.mChunk->lastError().empty());
        EXPECT_EQ(loader.mFixture.mRuntime->serialise().bytes, before);
        EXPECT_EQ(loader.mFixture.mRuntime->getLocal(instance, "count").integer, 1);
    }

    TEST(ObScriptGamePack, a_chunk_of_garbage_bytes_is_refused)
    {
        Host host;
        StubGamePack pack(&host);
        makeState(pack, 1);
        SavedChunk chunk;
        chunk.packId = "stubpack";
        chunk.name = "script.obscript";
        chunk.version = ScriptObscript::Limits::kSaveFormatVersion;
        for (int i = 0; i < 64; ++i)
        {
            chunk.bytes.push_back(static_cast<std::byte>(0xA5 ^ i));
        }
        EXPECT_FALSE(host.mSaveChunks.restore({ chunk }).ok);
        chunk.bytes.clear();
        EXPECT_FALSE(host.mSaveChunks.restore({ chunk }).ok);
    }

    TEST(ObScriptGamePack, a_chunk_from_a_newer_runtime_is_refused_before_any_handler_reads)
    {
        Host host;
        StubGamePack pack(&host);
        const InstanceId instance = makeState(pack, 1);
        auto collected = host.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        collected.chunks.front().version += 1;
        const auto restored = host.mSaveChunks.restore(collected.chunks);
        EXPECT_FALSE(restored.ok);
        EXPECT_EQ(pack.mFixture.mRuntime->getLocal(instance, "count").integer, 1);
    }

    TEST(ObScriptGamePack, an_oversized_chunk_is_refused_without_reading_it_into_the_runtime)
    {
        Host host;
        StubGamePack pack(&host);
        makeState(pack, 1);
        SavedChunk chunk;
        chunk.packId = "stubpack";
        chunk.name = "script.obscript";
        chunk.version = ScriptObscript::Limits::kSaveFormatVersion;
        chunk.bytes.assign(ScriptObscript::Limits::kMaxSaveBlobBytes + 1, std::byte{ 1 });
        const auto restored = host.mSaveChunks.restore({ chunk });
        EXPECT_FALSE(restored.ok);
        EXPECT_NE(pack.mChunk->lastError().find("too large"), std::string::npos) << pack.mChunk->lastError();
    }

    TEST(ObScriptGamePack, an_empty_runtime_saves_and_loads)
    {
        Host first;
        StubGamePack saver(&first);
        const auto collected = first.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        ASSERT_EQ(collected.chunks.size(), 1u);
        Host second;
        StubGamePack loader(&second);
        EXPECT_TRUE(second.mSaveChunks.restore(collected.chunks).ok);
    }

    TEST(ObScriptGamePack, the_chunk_is_not_written_by_a_pack_that_did_not_attach)
    {
        Host host;
        RuntimeFixture fixture;
        ScriptObscript::ObScriptSaveChunk chunk(*fixture.mRuntime);
        (void)chunk;
        EXPECT_EQ(host.mSaveChunks.handlerCount(), 0u);
        const auto collected = host.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        EXPECT_TRUE(collected.chunks.empty());
    }
    // Review A2: a runtime that could not write its state must not look like one with nothing to save.
    class FailingRuntime final : public openeden::script::ObScriptContract
    {
    public:
        bool registerFunction(const openeden::script::FunctionDescriptor&, openeden::script::FunctionHandler,
            openeden::script::ErrorReport&) noexcept override
        {
            return false;
        }
        openeden::script::CompileResult compile(const openeden::script::ScriptSource&) noexcept override { return {}; }
        InstanceId createInstance(ScriptId, openeden::script::FormId) noexcept override { return 0; }
        openeden::script::RunResult run(
            InstanceId, std::string_view, const openeden::script::Budget&) noexcept override
        {
            return {};
        }
        Value getLocal(InstanceId, std::string_view) const noexcept override { return {}; }
        bool setLocal(InstanceId, std::string_view, Value) noexcept override { return false; }
        openeden::script::SaveBlob serialise() const noexcept override { return {}; } // version 0, no bytes: failed
        openeden::script::RestoreResult restore(const openeden::script::SaveBlob&) noexcept override { return {}; }
    };

    class NullWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void*, std::size_t size) override { mWritten += size; }
        std::size_t mWritten = 0;
    };

    TEST(ObScriptGamePack, a_failed_serialise_makes_the_chunk_write_throw_so_the_save_is_refused)
    {
        FailingRuntime runtime;
        ScriptObscript::ObScriptSaveChunk chunk(runtime);
        NullWriter writer;
        EXPECT_THROW(chunk.write(writer), std::runtime_error);
        EXPECT_EQ(writer.mWritten, 0u);
    }

    TEST(ObScriptGamePack, the_real_runtime_writes_a_non_empty_chunk_even_with_no_instances)
    {
        Host host;
        StubGamePack pack(&host);
        NullWriter writer;
        pack.mChunk->write(writer);
        EXPECT_GT(writer.mWritten, 0u);
    }
}
