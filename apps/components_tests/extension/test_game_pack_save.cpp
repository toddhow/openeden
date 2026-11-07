// Task 0084: the save chunk service, its byte codec, the game-pack check of a save, and the game services a game pack
// registers. Written from the spec (docs/tasks/0084-game-pack-tier-sdk-4.md) before the code.
#include <components/extension/game_services.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/save_chunk_store.hpp>
#include <openeden/extension.hpp>
#include <openeden/game/save_chunks.hpp>
#include <openeden/host.hpp>
#include <openeden/pack_kind.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::extension;
    using openeden::sdk::ChunkReader;
    using openeden::sdk::ChunkWriter;
    using openeden::sdk::SaveChunkHandler;

    // A chunk with a counter and a name, in the shape a game pack's state record would have.
    class CounterChunk final : public SaveChunkHandler
    {
    public:
        CounterChunk(std::string name, std::uint32_t version)
            : mName(std::move(name))
            , mVersion(version)
        {
        }

        std::string_view chunkName() const override { return mName; }
        std::uint32_t chunkVersion() const override { return mVersion; }

        void write(ChunkWriter& writer) override
        {
            if (mThrowOnWrite)
                throw std::runtime_error("write boom");
            writer.writeValue(mCounter);
            writer.writeString(mLabel);
        }

        bool read(ChunkReader& reader, std::uint32_t version) override
        {
            ++mReads;
            mReadVersion = version;
            if (mThrowOnRead)
                throw std::runtime_error("read boom");
            if (mRefuse)
                return false;
            return reader.readValue(mCounter) && reader.readString(mLabel);
        }

        std::int32_t mCounter = 0;
        std::string mLabel;
        int mReads = 0;
        std::uint32_t mReadVersion = 0;
        bool mThrowOnWrite = false;
        bool mThrowOnRead = false;
        bool mRefuse = false;

    private:
        std::string mName;
        std::uint32_t mVersion;
    };

    SavedChunk makeChunk(std::string pack, std::string name, std::uint32_t version, std::vector<std::uint8_t> payload)
    {
        SavedChunk chunk;
        chunk.packId = std::move(pack);
        chunk.name = std::move(name);
        chunk.version = version;
        for (const std::uint8_t byte : payload)
            chunk.bytes.push_back(static_cast<std::byte>(byte));
        return chunk;
    }
}

// ---- The service ----

TEST(SaveChunkService, RoundTripsAChunkThroughAFreshServiceLikeASaveAndLoad)
{
    SaveChunkServiceImpl writing;
    CounterChunk source("state", 3);
    source.mCounter = 42;
    source.mLabel = "hello";
    ASSERT_TRUE(writing.registerHandler("game_test", source));

    const ChunkCollectResult collected = writing.collect();
    ASSERT_TRUE(collected.ok) << collected.error;
    ASSERT_EQ(collected.chunks.size(), 1u);
    EXPECT_EQ(collected.chunks[0].packId, "game_test");
    EXPECT_EQ(collected.chunks[0].name, "state");
    EXPECT_EQ(collected.chunks[0].version, 3u);
    EXPECT_FALSE(collected.chunks[0].bytes.empty());

    // The bytes go through the codec, as they would into a save record
    const std::vector<std::byte> bytes = encodeChunks(collected.chunks);
    std::vector<SavedChunk> decoded;
    std::string error;
    ASSERT_TRUE(decodeChunks(bytes, decoded, error)) << error;

    SaveChunkServiceImpl reading;
    CounterChunk target("state", 3);
    ASSERT_TRUE(reading.registerHandler("game_test", target));
    const ChunkRestoreResult restored = reading.restore(decoded);
    ASSERT_TRUE(restored.ok) << restored.error;
    EXPECT_EQ(target.mCounter, 42);
    EXPECT_EQ(target.mLabel, "hello");
    EXPECT_EQ(target.mReadVersion, 3u);
}

TEST(SaveChunkService, ChunksAreWrittenInRegistrationOrder)
{
    SaveChunkServiceImpl service;
    CounterChunk zebra("zebra", 1);
    CounterChunk alpha("alpha", 1);
    ASSERT_TRUE(service.registerHandler("p", zebra));
    ASSERT_TRUE(service.registerHandler("p", alpha));
    const ChunkCollectResult collected = service.collect();
    ASSERT_TRUE(collected.ok);
    ASSERT_EQ(collected.chunks.size(), 2u);
    EXPECT_EQ(collected.chunks[0].name, "zebra");
    EXPECT_EQ(collected.chunks[1].name, "alpha");
}

TEST(SaveChunkService, ReadsAnOlderVersionAndPassesItsVersion)
{
    SaveChunkServiceImpl service;
    CounterChunk handler("state", 5);
    ASSERT_TRUE(service.registerHandler("p", handler));
    CounterChunk older("state", 2);
    older.mCounter = 7;
    older.mLabel = "x";
    SaveChunkServiceImpl other;
    ASSERT_TRUE(other.registerHandler("p", older));
    auto chunks = other.collect().chunks;
    ASSERT_EQ(chunks.size(), 1u);

    ASSERT_TRUE(service.restore(chunks).ok);
    EXPECT_EQ(handler.mReadVersion, 2u);
    EXPECT_EQ(handler.mCounter, 7);
}

TEST(SaveChunkService, RefusesRegistrationWithEmptyIdEmptyNameOrADuplicate)
{
    SaveChunkServiceImpl service;
    CounterChunk a("state", 1);
    CounterChunk b("state", 1);
    CounterChunk unnamed("", 1);
    EXPECT_FALSE(service.registerHandler("", a));
    EXPECT_FALSE(service.registerHandler("p", unnamed));
    EXPECT_TRUE(service.registerHandler("p", a));
    EXPECT_FALSE(service.registerHandler("p", b));
    // The same name under another pack is another chunk
    EXPECT_TRUE(service.registerHandler("q", b));
    EXPECT_EQ(service.handlerCount(), 2u);
}

TEST(SaveChunkService, UnknownPackChunkIsPreservedOnReSave)
{
    // A save made with the game pack "other_game" loaded by a host where only "game_test" is registered
    const SavedChunk foreign = makeChunk("other_game", "stuff", 9, { 1, 2, 3, 4, 5 });
    const SavedChunk known = makeChunk("game_test", "state", 1, {});

    SaveChunkServiceImpl first;
    CounterChunk source("state", 1);
    source.mCounter = 5;
    source.mLabel = "kept";
    ASSERT_TRUE(first.registerHandler("game_test", source));
    auto knownChunks = first.collect().chunks;
    ASSERT_EQ(knownChunks.size(), 1u);

    SaveChunkServiceImpl service;
    CounterChunk handler("state", 1);
    ASSERT_TRUE(service.registerHandler("game_test", handler));
    ASSERT_TRUE(service.restore({ foreign, knownChunks[0] }).ok);
    EXPECT_EQ(service.preservedCount(), 1u);
    EXPECT_EQ(handler.mCounter, 5);

    // Saving again keeps the foreign chunk byte for byte
    const ChunkCollectResult again = service.collect();
    ASSERT_TRUE(again.ok) << again.error;
    ASSERT_EQ(again.chunks.size(), 2u);
    EXPECT_EQ(again.chunks[0].packId, "game_test");
    EXPECT_EQ(again.chunks[1].packId, "other_game");
    EXPECT_EQ(again.chunks[1].name, "stuff");
    EXPECT_EQ(again.chunks[1].version, 9u);
    EXPECT_EQ(again.chunks[1].bytes, foreign.bytes);
    (void)known;
}

TEST(SaveChunkService, UnknownChunkOfARegisteredPackIsAlsoPreserved)
{
    SaveChunkServiceImpl service;
    CounterChunk handler("state", 1);
    ASSERT_TRUE(service.registerHandler("p", handler));
    const SavedChunk extra = makeChunk("p", "added_in_a_later_version", 4, { 9, 9 });
    ASSERT_TRUE(service.restore({ extra }).ok);
    EXPECT_EQ(handler.mReads, 0);
    const ChunkCollectResult out = service.collect();
    ASSERT_TRUE(out.ok);
    ASSERT_EQ(out.chunks.size(), 2u);
    EXPECT_EQ(out.chunks[1].name, "added_in_a_later_version");
}

TEST(SaveChunkService, ChunkNewerThanItsHandlerRefusesTheWholeLoadBeforeAnyHandlerReads)
{
    SaveChunkServiceImpl service;
    CounterChunk first("first", 1);
    CounterChunk second("second", 2);
    ASSERT_TRUE(service.registerHandler("p", first));
    ASSERT_TRUE(service.registerHandler("p", second));
    const ChunkRestoreResult result
        = service.restore({ makeChunk("p", "first", 1, {}), makeChunk("p", "second", 3, {}) });
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("second"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("p"), std::string::npos) << result.error;
    EXPECT_EQ(first.mReads, 0);
    EXPECT_EQ(second.mReads, 0);
}

TEST(SaveChunkService, HandlerThatRefusesFailsTheLoadNamingPackAndChunk)
{
    SaveChunkServiceImpl service;
    CounterChunk handler("state", 1);
    handler.mRefuse = true;
    ASSERT_TRUE(service.registerHandler("game_test", handler));
    const ChunkRestoreResult result = service.restore({ makeChunk("game_test", "state", 1, {}) });
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("game_test"), std::string::npos) << result.error;
    EXPECT_NE(result.error.find("state"), std::string::npos) << result.error;
}

TEST(SaveChunkService, ExceptionsInHandlersNeverEscape)
{
    SaveChunkServiceImpl service;
    CounterChunk handler("state", 1);
    handler.mThrowOnWrite = true;
    ASSERT_TRUE(service.registerHandler("game_test", handler));
    ChunkCollectResult collected;
    EXPECT_NO_THROW(collected = service.collect());
    EXPECT_FALSE(collected.ok);
    EXPECT_NE(collected.error.find("game_test"), std::string::npos) << collected.error;
    EXPECT_NE(collected.error.find("write boom"), std::string::npos) << collected.error;

    handler.mThrowOnWrite = false;
    handler.mThrowOnRead = true;
    ChunkRestoreResult restored;
    EXPECT_NO_THROW(restored = service.restore({ makeChunk("game_test", "state", 1, { 0, 0, 0, 0, 0, 0, 0, 0 }) }));
    EXPECT_FALSE(restored.ok);
    EXPECT_NE(restored.error.find("read boom"), std::string::npos) << restored.error;
}

TEST(SaveChunkService, ReadPastTheEndFailsWithoutConsuming)
{
    SaveChunkServiceImpl service;
    CounterChunk handler("state", 1);
    ASSERT_TRUE(service.registerHandler("p", handler));
    // Three bytes cannot hold the 4-byte counter
    const ChunkRestoreResult result = service.restore({ makeChunk("p", "state", 1, { 1, 2, 3 }) });
    EXPECT_FALSE(result.ok);
}

TEST(SaveChunkService, RefusedSavingFailsCollectWithTheReason)
{
    SaveChunkServiceImpl service;
    CounterChunk handler("state", 1);
    ASSERT_TRUE(service.registerHandler("p", handler));
    service.refuseSaving("Game pack 'p' failed at start: boom");
    EXPECT_TRUE(service.isSavingRefused());
    const ChunkCollectResult collected = service.collect();
    EXPECT_FALSE(collected.ok);
    EXPECT_NE(collected.error.find("Game pack 'p' failed"), std::string::npos) << collected.error;
    EXPECT_TRUE(collected.chunks.empty());
}

TEST(SaveChunkService, UnregisteringAPackForgetsItsHandlersButKeepsPreservedChunks)
{
    SaveChunkServiceImpl service;
    CounterChunk handler("state", 1);
    ASSERT_TRUE(service.registerHandler("p", handler));
    ASSERT_TRUE(service.restore({ makeChunk("zzz", "keep", 1, { 1 }) }).ok);
    service.unregisterPack("p");
    EXPECT_EQ(service.handlerCount(), 0u);
    const ChunkCollectResult out = service.collect();
    ASSERT_TRUE(out.ok);
    ASSERT_EQ(out.chunks.size(), 1u);
    EXPECT_EQ(out.chunks[0].packId, "zzz");
}

// ---- The byte codec ----

TEST(SaveChunkCodec, EncodingIsDeterministicAndRoundTrips)
{
    const std::vector<SavedChunk> chunks
        = { makeChunk("a", "one", 1, { 1, 2, 3 }), makeChunk("b", "two", 7, {}), makeChunk("a", "three", 2, { 255 }) };
    const auto bytes = encodeChunks(chunks);
    EXPECT_EQ(bytes, encodeChunks(chunks));
    std::vector<SavedChunk> decoded;
    std::string error;
    ASSERT_TRUE(decodeChunks(bytes, decoded, error)) << error;
    ASSERT_EQ(decoded.size(), chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i)
    {
        EXPECT_EQ(decoded[i].packId, chunks[i].packId);
        EXPECT_EQ(decoded[i].name, chunks[i].name);
        EXPECT_EQ(decoded[i].version, chunks[i].version);
        EXPECT_EQ(decoded[i].bytes, chunks[i].bytes);
    }
    EXPECT_EQ(encodeChunks(decoded), bytes);
}

TEST(SaveChunkCodec, EmptyListRoundTrips)
{
    const auto bytes = encodeChunks({});
    std::vector<SavedChunk> decoded{ makeChunk("x", "y", 1, {}) };
    std::string error;
    ASSERT_TRUE(decodeChunks(bytes, decoded, error)) << error;
    EXPECT_TRUE(decoded.empty());
}

TEST(SaveChunkCodec, EveryTruncationIsRefusedNotCrashed)
{
    const std::vector<SavedChunk> chunks
        = { makeChunk("pack", "name", 1, { 1, 2, 3, 4 }), makeChunk("p2", "n2", 2, {}) };
    const auto bytes = encodeChunks(chunks);
    ASSERT_GT(bytes.size(), 4u);
    for (std::size_t cut = 0; cut < bytes.size(); ++cut)
    {
        std::vector<SavedChunk> decoded;
        std::string error;
        const std::span<const std::byte> part(bytes.data(), cut);
        EXPECT_FALSE(decodeChunks(part, decoded, error)) << "cut at " << cut;
        EXPECT_FALSE(error.empty()) << "cut at " << cut;
    }
}

TEST(SaveChunkCodec, TrailingBytesAreRefused)
{
    auto bytes = encodeChunks({ makeChunk("a", "b", 1, { 1 }) });
    bytes.push_back(std::byte{ 0 });
    std::vector<SavedChunk> decoded;
    std::string error;
    EXPECT_FALSE(decodeChunks(bytes, decoded, error));
    EXPECT_FALSE(error.empty());
}

TEST(SaveChunkCodec, HostileCountsAndSizesDoNotAllocateWithoutBound)
{
    // count = 0xFFFFFFFF with nothing after it
    std::vector<std::byte> bytes(4, std::byte{ 0xFF });
    std::vector<SavedChunk> decoded;
    std::string error;
    EXPECT_FALSE(decodeChunks(bytes, decoded, error));
    EXPECT_FALSE(error.empty());

    // one chunk whose declared payload is 4 GiB
    auto good = encodeChunks({ makeChunk("a", "b", 1, { 1 }) });
    // The payload size is the last u32 before the single payload byte
    ASSERT_GE(good.size(), 5u);
    for (std::size_t i = good.size() - 5; i < good.size() - 1; ++i)
        good[i] = std::byte{ 0xFF };
    error.clear();
    EXPECT_FALSE(decodeChunks(good, decoded, error));
    EXPECT_FALSE(error.empty());
}

TEST(SaveChunkCodec, EmptyPackIdOrNameIsRefused)
{
    auto bytes = encodeChunks({ makeChunk("", "name", 1, {}) });
    std::vector<SavedChunk> decoded;
    std::string error;
    EXPECT_FALSE(decodeChunks(bytes, decoded, error));
    bytes = encodeChunks({ makeChunk("pack", "", 1, {}) });
    EXPECT_FALSE(decodeChunks(bytes, decoded, error));
}

// ---- A save naming another game ----

TEST(GamePackSaveHeader, SaveOfAnotherGameIsRefusedCleanly)
{
    const std::string message = checkSavedGamePack("game_skyrimse", "1.0.0", "game_morrowind", "1.0.0");
    EXPECT_FALSE(message.empty());
    EXPECT_NE(message.find("game_skyrimse"), std::string::npos) << message;
    EXPECT_NE(message.find("game_morrowind"), std::string::npos) << message;
}

TEST(GamePackSaveHeader, SaveOfTheSamePackLoads)
{
    EXPECT_TRUE(checkSavedGamePack("game_morrowind", "1.0.0", "game_morrowind", "1.0.0").empty());
    EXPECT_TRUE(checkSavedGamePack("game_morrowind", "1.0.0", "game_morrowind", "1.2.0").empty());
    EXPECT_TRUE(checkSavedGamePack("game_morrowind", "1.2.0", "game_morrowind", "1.10.0").empty());
}

TEST(GamePackSaveHeader, SaveFromANewerPackVersionIsRefused)
{
    const std::string message = checkSavedGamePack("game_morrowind", "1.3.0", "game_morrowind", "1.2.9");
    EXPECT_FALSE(message.empty());
    EXPECT_NE(message.find("1.3.0"), std::string::npos) << message;
    EXPECT_FALSE(checkSavedGamePack("game_morrowind", "2.0.0", "game_morrowind", "1.9.9").empty());
    // Numbers, not text: 1.10.0 is newer than 1.9.0
    EXPECT_FALSE(checkSavedGamePack("game_morrowind", "1.10.0", "game_morrowind", "1.9.0").empty());
}

TEST(GamePackSaveHeader, SaveWithNoRecordedPackLoads)
{
    // A save from before game packs, with or without a game pack active now
    EXPECT_TRUE(checkSavedGamePack("", "", "game_morrowind", "1.0.0").empty());
    EXPECT_TRUE(checkSavedGamePack("", "", "", "").empty());
}

TEST(GamePackSaveHeader, SaveOfAPackWhenNoneIsActiveIsRefused)
{
    const std::string message = checkSavedGamePack("game_morrowind", "1.0.0", "", "");
    EXPECT_FALSE(message.empty());
    EXPECT_NE(message.find("game_morrowind"), std::string::npos) << message;
}

TEST(GamePackSaveHeader, VersionTextThatIsNotNumbersIsComparedForEquality)
{
    EXPECT_TRUE(checkSavedGamePack("g", "dev", "g", "dev").empty());
    EXPECT_FALSE(checkSavedGamePack("g", "dev", "g", "1.0.0").empty());
}

// ---- The other game services ----

TEST(GameServices, RecordTypesRegisterOnceAndAreFoundById)
{
    RecordServiceImpl records;
    const auto npc = openeden::sdk::makeRecordTypeId('N', 'P', 'C', '_');
    EXPECT_EQ(npc, 0x5F43504Eu);
    EXPECT_TRUE(records.registerRecordType({ npc, "NPC_", 80 }));
    EXPECT_FALSE(records.registerRecordType({ npc, "NPC_again", 80 }));
    EXPECT_FALSE(records.registerRecordType({ 0, "zero", 4 }));
    EXPECT_FALSE(records.registerRecordType({ 1, "", 4 }));
    EXPECT_FALSE(records.registerRecordType({ 2, "nosize", 0 }));
    const auto* found = records.findRecordType(npc);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "NPC_");
    EXPECT_EQ(found->sizeBytes, 80u);
    EXPECT_EQ(records.findRecordType(npc + 1), nullptr);
    EXPECT_EQ(records.count(), 1u);
}

TEST(GameServices, RecordTypeNameIsCopiedNotBorrowed)
{
    RecordServiceImpl records;
    {
        std::string temporary = "TEMP";
        ASSERT_TRUE(records.registerRecordType({ 7, temporary, 4 }));
        temporary = "overwritten!";
    }
    const auto* found = records.findRecordType(7);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "TEMP");
}

TEST(GameServices, ContentLoadersAreAskedInRegistrationOrder)
{
    struct Loader final : openeden::sdk::ContentLoader
    {
        explicit Loader(std::string tag)
            : mTag(std::move(tag))
        {
        }
        bool probe(const openeden::sdk::ContentHeader& header) const override
        {
            return header.gameTag == mTag || mTag == "*";
        }
        std::string mTag;
    };
    ContentServiceImpl content;
    Loader tes3("TES3");
    Loader any("*");
    content.registerLoader(tes3);
    content.registerLoader(any);
    EXPECT_EQ(content.findLoader({ "TES3", 1 }), &tes3);
    EXPECT_EQ(content.findLoader({ "TES4", 1 }), &any);
    content.clear();
    EXPECT_EQ(content.findLoader({ "TES3", 1 }), nullptr);
}

TEST(GameServices, ScriptLanguagesAndWindowsAreDeclaredOnce)
{
    ScriptHostServiceImpl scripts;
    EXPECT_TRUE(scripts.declareLanguage("mwscript"));
    EXPECT_FALSE(scripts.declareLanguage("mwscript"));
    EXPECT_FALSE(scripts.declareLanguage(""));
    EXPECT_TRUE(scripts.isLanguageDeclared("mwscript"));
    EXPECT_FALSE(scripts.isLanguageDeclared("papyrus"));

    UiServiceImpl ui;
    EXPECT_TRUE(ui.declareWindow("inventory", "Inventory"));
    EXPECT_FALSE(ui.declareWindow("inventory", "Again"));
    EXPECT_FALSE(ui.declareWindow("", "No id"));
    EXPECT_TRUE(ui.isWindowDeclared("inventory"));
    EXPECT_FALSE(ui.isWindowDeclared("map"));
}

// ---- The whole route: a test game pack saves and loads through the lifecycle manager ----

namespace
{
    class TestGamePack final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar&) override {}
        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

        void start(openeden::sdk::Host& host) override
        {
            auto* chunks = host.getSaveChunkService();
            if (chunks == nullptr)
                throw std::runtime_error("no save chunk service");
            if (!chunks->registerHandler(mChunk))
                throw std::runtime_error("registerHandler refused");
        }

        void stop() override {}

        CounterChunk mChunk{ "state", 1 };
    };
}

TEST(GamePackSave, TestPackWritesReadsAndPreservesThroughTheHost)
{
    std::vector<std::byte> bytes;
    {
        HostImpl host;
        LifecycleManager manager(host);
        auto pack = std::make_unique<TestGamePack>();
        pack->mChunk.mCounter = 11;
        pack->mChunk.mLabel = "saved";
        ExtensionManifest manifest;
        manifest.version = 2;
        manifest.id = "game_test";
        manifest.name = "Test";
        manifest.extensionVersion = "1.0.0";
        manifest.kind = "game";
        manifest.toolchain = "t";
        manager.addExtension(manifest, std::move(pack));
        ASSERT_TRUE(manager.initialize());
        ASSERT_FALSE(manager.hasSessionError()) << manager.getSessionError();

        auto collected = host.saveChunks().collect();
        ASSERT_TRUE(collected.ok) << collected.error;
        ASSERT_EQ(collected.chunks.size(), 1u);
        EXPECT_EQ(collected.chunks[0].packId, "game_test");
        // A chunk of a pack that is not loaded rides along
        collected.chunks.push_back(makeChunk("not_loaded", "x", 1, { 7 }));
        bytes = encodeChunks(collected.chunks);
        manager.shutdown();
        // The pack is gone: its handler must be too, so that nothing points into freed memory
        EXPECT_EQ(host.saveChunks().handlerCount(), 0u);
    }

    HostImpl host;
    LifecycleManager manager(host);
    auto pack = std::make_unique<TestGamePack>();
    TestGamePack* raw = pack.get();
    ExtensionManifest manifest;
    manifest.version = 2;
    manifest.id = "game_test";
    manifest.name = "Test";
    manifest.extensionVersion = "1.0.0";
    manifest.kind = "game";
    manifest.toolchain = "t";
    manager.addExtension(manifest, std::move(pack));
    ASSERT_TRUE(manager.initialize());

    std::vector<SavedChunk> decoded;
    std::string error;
    ASSERT_TRUE(decodeChunks(bytes, decoded, error)) << error;
    const auto restored = host.saveChunks().restore(decoded);
    ASSERT_TRUE(restored.ok) << restored.error;
    EXPECT_EQ(raw->mChunk.mCounter, 11);
    EXPECT_EQ(raw->mChunk.mLabel, "saved");

    const auto again = host.saveChunks().collect();
    ASSERT_TRUE(again.ok);
    ASSERT_EQ(again.chunks.size(), 2u);
    EXPECT_EQ(again.chunks[1].packId, "not_loaded");
    EXPECT_EQ(encodeChunks(again.chunks), bytes);
}

TEST(GamePackSave, ChunkHandlerIsScopedToTheRegisteringPackId)
{
    HostImpl host;
    LifecycleManager manager(host);
    ExtensionManifest manifest;
    manifest.version = 2;
    manifest.id = "game_scoped";
    manifest.name = "Scoped";
    manifest.extensionVersion = "1.0.0";
    manifest.kind = "game";
    manifest.toolchain = "t";
    manager.addExtension(manifest, std::make_unique<TestGamePack>());
    ASSERT_TRUE(manager.initialize());
    const auto collected = host.saveChunks().collect();
    ASSERT_TRUE(collected.ok);
    ASSERT_EQ(collected.chunks.size(), 1u);
    EXPECT_EQ(collected.chunks[0].packId, "game_scoped");
}
