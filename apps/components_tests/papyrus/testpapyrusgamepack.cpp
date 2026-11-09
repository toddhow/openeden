// Task 0093 (H1): the glue between a game pack and the Papyrus runtime (task 0084's game services).
//
// In plain words: a game pack owns a Papyrus runtime. The glue declares the language "papyrus" through the game script
// host service and registers one save chunk, "script.papyrus", whose bytes are the runtime's own versioned save blob.
// These tests play the part of a game pack (StubGamePack): it is started with the two services the host gives game
// packs (here the real implementations from components/extension), saves, and a second pack loads what the first one
// saved. Damaged, oversized and wrong-version chunks must be refused cleanly and change nothing, a runtime that
// cannot write its state must make the save fail, and a pack with no game services must get a clear message.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <components/extension/game_services.hpp>
#include <components/extension/save_chunk_store.hpp>
#include <components/script_papyrus/gamepackglue.hpp>
#include <components/script_papyrus/papyrussave.hpp>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;
    using openeden::extension::SaveChunkServiceImpl;
    using openeden::extension::SavedChunk;
    using openeden::extension::ScriptHostServiceImpl;

    ScriptBuilder counterScript()
    {
        ScriptBuilder script("Counter");
        script.var("n", "Int");
        script.function("Inc").iadd(Id("n"), Id("n"), Int(1)).ret(Nil());
        return script;
    }

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

    struct Host
    {
        ScriptHostServiceImpl mScriptHost;
        SaveChunkServiceImpl mSaveChunks;
        PackSaveChunks mPackChunks{ mSaveChunks, "stubpack" };
    };

    // A game pack in miniature: it owns the world, the runtime and the chunk handler, as a real one would.
    struct StubGamePack
    {
        explicit StubGamePack(Host* host)
        {
            EXPECT_TRUE(mVm.add(counterScript()));
            mChunk = std::make_unique<ScriptPapyrus::PapyrusSaveChunk>(mVm.rt());
            mResult = host != nullptr ? ScriptPapyrus::attachToGamePack(&host->mScriptHost, &host->mPackChunks, *mChunk)
                                      : ScriptPapyrus::attachToGamePack(nullptr, nullptr, *mChunk);
        }

        Vm mVm;
        std::unique_ptr<ScriptPapyrus::PapyrusSaveChunk> mChunk;
        ScriptPapyrus::GlueResult mResult;
    };

    InstanceId makeState(StubGamePack& pack, int increments)
    {
        const InstanceId id = pack.mVm.makeAndInit("Counter");
        for (int i = 0; i < increments; ++i)
        {
            pack.mVm.run(id, "Inc");
        }
        return id;
    }

    TEST(PapyrusGamePack, attaching_declares_the_language_and_registers_the_chunk)
    {
        Host host;
        StubGamePack pack(&host);
        ASSERT_TRUE(pack.mResult.ok) << pack.mResult.error;
        EXPECT_TRUE(host.mScriptHost.isLanguageDeclared("papyrus"));
        EXPECT_EQ(host.mSaveChunks.handlerCount(), 1u);
        EXPECT_EQ(pack.mChunk->chunkName(), "script.papyrus");
        EXPECT_EQ(pack.mChunk->chunkVersion(), ScriptPapyrus::Save::kFormatVersion);
    }

    TEST(PapyrusGamePack, a_pack_without_the_game_services_gets_a_clear_message)
    {
        StubGamePack pack(nullptr);
        EXPECT_FALSE(pack.mResult.ok);
        EXPECT_NE(pack.mResult.error.find("game pack"), std::string::npos) << pack.mResult.error;
    }

    TEST(PapyrusGamePack, a_missing_service_alone_is_refused_before_anything_is_declared)
    {
        Host host;
        Vm vm;
        ScriptPapyrus::PapyrusSaveChunk chunk(vm.rt());
        EXPECT_FALSE(ScriptPapyrus::attachToGamePack(&host.mScriptHost, nullptr, chunk).ok);
        EXPECT_FALSE(host.mScriptHost.isLanguageDeclared("papyrus"));
        EXPECT_FALSE(ScriptPapyrus::attachToGamePack(nullptr, &host.mPackChunks, chunk).ok);
        EXPECT_EQ(host.mSaveChunks.handlerCount(), 0u);
    }

    TEST(PapyrusGamePack, attaching_twice_is_refused_with_a_reason)
    {
        Host host;
        StubGamePack first(&host);
        ASSERT_TRUE(first.mResult.ok) << first.mResult.error;
        StubGamePack second(&host);
        EXPECT_FALSE(second.mResult.ok);
        EXPECT_FALSE(second.mResult.error.empty());
        EXPECT_EQ(host.mSaveChunks.handlerCount(), 1u);
    }

    TEST(PapyrusGamePack, the_saved_chunk_carries_the_runtimes_blob)
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
        EXPECT_EQ(chunk.name, "script.papyrus");
        EXPECT_EQ(chunk.version, ScriptPapyrus::Save::kFormatVersion);
        const auto blob = pack.mVm.rt().serialise();
        ASSERT_EQ(chunk.bytes.size(), blob.bytes.size());
        for (std::size_t i = 0; i < blob.bytes.size(); ++i)
        {
            ASSERT_EQ(static_cast<std::uint8_t>(chunk.bytes[i]), blob.bytes[i]) << i;
        }
    }

    TEST(PapyrusGamePack, a_second_pack_loads_what_the_first_one_saved)
    {
        Host first;
        StubGamePack saver(&first);
        ASSERT_TRUE(saver.mResult.ok);
        const InstanceId instance = makeState(saver, 3);
        const auto collected = first.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        const auto bytes = openeden::extension::encodeChunks(collected.chunks);
        std::vector<SavedChunk> loaded;
        std::string error;
        ASSERT_TRUE(openeden::extension::decodeChunks(bytes, loaded, error)) << error;

        Host second;
        StubGamePack loader(&second);
        ASSERT_TRUE(loader.mResult.ok);
        const auto restored = second.mSaveChunks.restore(loaded);
        ASSERT_TRUE(restored.ok) << restored.error;
        EXPECT_TRUE(loader.mChunk->lastError().empty());
        EXPECT_EQ(loader.mVm.varInt(instance, "n"), 3);
        // The restore reports what it left out: nothing dropped, and the (stub) world has no attach key check.
        EXPECT_EQ(loader.mChunk->lastDetails().droppedParkedStacks, 0u);
        EXPECT_EQ(loader.mChunk->lastDetails().changedScripts, 0u);
        loader.mVm.run(instance, "Inc");
        saver.mVm.run(instance, "Inc");
        EXPECT_EQ(saver.mVm.rt().serialise().bytes, loader.mVm.rt().serialise().bytes);
    }

    TEST(PapyrusGamePack, a_truncated_chunk_is_refused_and_changes_nothing)
    {
        Host first;
        StubGamePack saver(&first);
        makeState(saver, 2);
        auto collected = first.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        collected.chunks.front().bytes.resize(collected.chunks.front().bytes.size() / 2);

        Host second;
        StubGamePack loader(&second);
        const InstanceId instance = makeState(loader, 1);
        const auto before = loader.mVm.rt().serialise().bytes;
        const auto restored = second.mSaveChunks.restore(collected.chunks);
        EXPECT_FALSE(restored.ok);
        EXPECT_NE(restored.error.find("stubpack"), std::string::npos) << restored.error;
        EXPECT_FALSE(loader.mChunk->lastError().empty());
        EXPECT_EQ(loader.mVm.rt().serialise().bytes, before);
        EXPECT_EQ(loader.mVm.varInt(instance, "n"), 1);
    }

    TEST(PapyrusGamePack, a_chunk_of_garbage_bytes_is_refused)
    {
        Host host;
        StubGamePack pack(&host);
        makeState(pack, 1);
        SavedChunk chunk;
        chunk.packId = "stubpack";
        chunk.name = "script.papyrus";
        chunk.version = ScriptPapyrus::Save::kFormatVersion;
        for (int i = 0; i < 64; ++i)
        {
            chunk.bytes.push_back(static_cast<std::byte>(0xA5 ^ i));
        }
        EXPECT_FALSE(host.mSaveChunks.restore({ chunk }).ok);
        EXPECT_FALSE(pack.mChunk->lastError().empty());
        chunk.bytes.clear();
        EXPECT_FALSE(host.mSaveChunks.restore({ chunk }).ok);
    }

    TEST(PapyrusGamePack, a_chunk_from_a_newer_runtime_is_refused_and_changes_nothing)
    {
        Host host;
        StubGamePack pack(&host);
        const InstanceId instance = makeState(pack, 1);
        auto collected = host.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);
        collected.chunks.front().version += 1;
        EXPECT_FALSE(host.mSaveChunks.restore(collected.chunks).ok);
        EXPECT_EQ(pack.mVm.varInt(instance, "n"), 1);
    }

    TEST(PapyrusGamePack, an_oversized_chunk_is_refused_without_reading_it_into_the_runtime)
    {
        Host host;
        StubGamePack pack(&host);
        makeState(pack, 1);
        SavedChunk chunk;
        chunk.packId = "stubpack";
        chunk.name = "script.papyrus";
        chunk.version = ScriptPapyrus::Save::kFormatVersion;
        chunk.bytes.assign(ScriptPapyrus::Save::kMaxBlobBytes + 1, std::byte{ 1 });
        EXPECT_FALSE(host.mSaveChunks.restore({ chunk }).ok);
        EXPECT_NE(pack.mChunk->lastError().find("too large"), std::string::npos) << pack.mChunk->lastError();
    }

    TEST(PapyrusGamePack, an_empty_runtime_saves_and_loads)
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

    // A changed script keeps the saved instance's matching variables; the pack can see what was left out.
    TEST(PapyrusGamePack, a_restore_into_a_pack_that_lacks_the_script_is_refused)
    {
        Host first;
        StubGamePack saver(&first);
        makeState(saver, 1);
        const auto collected = first.mSaveChunks.collect();
        ASSERT_TRUE(collected.ok);

        Host second;
        Vm vm; // no Counter loaded
        ScriptPapyrus::PapyrusSaveChunk chunk(vm.rt());
        PackSaveChunks chunks(second.mSaveChunks, "stubpack");
        ASSERT_TRUE(ScriptPapyrus::attachToGamePack(&second.mScriptHost, &chunks, chunk).ok);
        EXPECT_FALSE(second.mSaveChunks.restore(collected.chunks).ok);
        EXPECT_FALSE(chunk.lastError().empty());
    }

    // A runtime that could not write its state must not look like one with nothing to save.
    class FailingRuntime final : public openeden::script::PapyrusContract
    {
    public:
        openeden::script::LoadResult loadScript(std::span<const std::uint8_t>, std::string_view) noexcept override
        {
            return {};
        }
        openeden::script::LinkReport link() const noexcept override { return {}; }
        bool registerNative(const openeden::script::NativeDescriptor&, openeden::script::NativeHandler,
            openeden::script::ErrorReport&) noexcept override
        {
            return false;
        }
        void setPolicy(openeden::script::UnknownFunctionPolicy) noexcept override {}
        InstanceId attach(const openeden::script::AttachKey&, std::string_view) noexcept override { return 0; }
        openeden::script::CallResult send(
            InstanceId, std::string_view, std::span<const openeden::script::PapyrusValue>) noexcept override
        {
            return {};
        }
        openeden::script::CallResult call(
            InstanceId, std::string_view, std::span<const openeden::script::PapyrusValue>) noexcept override
        {
            return {};
        }
        openeden::script::TickResult tick(double, const openeden::script::Budget&) noexcept override { return {}; }
        openeden::script::SaveBlob serialise() const noexcept override { return {}; } // version 0: failed
        openeden::script::RestoreResult restore(const openeden::script::SaveBlob&) noexcept override { return {}; }
    };

    class NullWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void*, std::size_t size) override { mWritten += size; }
        std::size_t mWritten = 0;
    };

    TEST(PapyrusGamePack, a_failed_serialise_makes_the_chunk_write_throw_so_the_save_is_refused)
    {
        FailingRuntime runtime;
        ScriptPapyrus::PapyrusSaveChunk chunk(runtime);
        NullWriter writer;
        EXPECT_THROW(chunk.write(writer), std::runtime_error);
        EXPECT_EQ(writer.mWritten, 0u);
    }

    TEST(PapyrusGamePack, the_real_runtime_writes_a_non_empty_chunk_even_with_no_instances)
    {
        Host host;
        StubGamePack pack(&host);
        NullWriter writer;
        pack.mChunk->write(writer);
        EXPECT_GT(writer.mWritten, 0u);
    }
}
