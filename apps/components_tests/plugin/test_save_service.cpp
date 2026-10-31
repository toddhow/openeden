#include <components/plugin/save_service.hpp>
#include <openeden/save.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::plugin::SaveServiceImpl;
    using openeden::sdk::SaveChunkHandler;
    using openeden::sdk::SaveChunkReader;
    using openeden::sdk::SaveChunkWriter;
    using openeden::sdk::SaveService;

    // Helper handler that records read/written binary payloads
    class TestSaveHandler : public SaveChunkHandler
    {
    public:
        explicit TestSaveHandler(std::uint32_t version, std::vector<std::uint8_t> payload = {})
            : mVersion(version)
            , mPayload(std::move(payload))
        {
        }

        std::uint32_t currentVersion() const noexcept override { return mVersion; }

        void writeSaveChunk(SaveChunkWriter& writer) override
        {
            if (!mPayload.empty())
            {
                writer.write(mPayload.data(), mPayload.size());
            }
        }

        void readSaveChunk(std::uint32_t chunkVersion, SaveChunkReader& reader) override
        {
            mLastReadVersion = chunkVersion;
            mPayload.resize(reader.size());
            if (reader.size() > 0)
            {
                reader.read(mPayload.data(), reader.size());
            }
        }

        std::uint32_t mVersion = 1;
        std::uint32_t mLastReadVersion = 0;
        std::vector<std::uint8_t> mPayload;
    };

    // Handler that performs version migration from v1 to v2
    class MigratingSaveHandler : public SaveChunkHandler
    {
    public:
        std::uint32_t currentVersion() const noexcept override { return 2; }

        void writeSaveChunk(SaveChunkWriter& writer) override
        {
            // V2 format: 8-byte payload (e.g. 64-bit state)
            const std::uint64_t v2State = mStateV2;
            writer.write(&v2State, sizeof(v2State));
        }

        void readSaveChunk(std::uint32_t chunkVersion, SaveChunkReader& reader) override
        {
            mMigrationInvoked = true;
            mLoadedVersion = chunkVersion;

            if (chunkVersion == 1)
            {
                // V1 format: 4-byte payload (32-bit state migrated to 64-bit)
                std::uint32_t v1State = 0;
                reader.read(&v1State, sizeof(v1State));
                mStateV2 = static_cast<std::uint64_t>(v1State) * 1000; // Migration logic
            }
            else if (chunkVersion == 2)
            {
                reader.read(&mStateV2, sizeof(mStateV2));
            }
        }

        bool mMigrationInvoked = false;
        std::uint32_t mLoadedVersion = 0;
        std::uint64_t mStateV2 = 0;
    };

    TEST(SaveServiceTest, PluginSaveChunkWriteAndRead)
    {
        SaveServiceImpl serviceA;

        const std::vector<std::uint8_t> testData = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
        auto handlerA = std::make_shared<TestSaveHandler>(1, testData);
        serviceA.registerHandler("plugin_physics", handlerA);

        const std::vector<std::uint8_t> saveBinary = serviceA.serialize();
        ASSERT_FALSE(saveBinary.empty());

        SaveServiceImpl serviceB;
        auto handlerB = std::make_shared<TestSaveHandler>(1);
        serviceB.registerHandler("plugin_physics", handlerB);

        EXPECT_TRUE(serviceB.deserialize(saveBinary));
        EXPECT_EQ(handlerB->mLastReadVersion, 1u);
        EXPECT_EQ(handlerB->mPayload, testData);
    }

    TEST(SaveServiceTest, UnreadChunkPreservationAcrossSaveRoundTrip)
    {
        // Session 1: Both "plugin_active" and "plugin_thirdparty" are active and write save chunks
        SaveServiceImpl session1;

        const std::vector<std::uint8_t> activeData = { 0x11, 0x22, 0x33, 0x44 };
        const std::vector<std::uint8_t> thirdpartyData = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

        session1.registerHandler("plugin_active", std::make_shared<TestSaveHandler>(1, activeData));
        session1.registerHandler("plugin_thirdparty", std::make_shared<TestSaveHandler>(3, thirdpartyData));

        const std::vector<std::uint8_t> saveBinary1 = session1.serialize();

        // Session 2: "plugin_thirdparty" is NOT installed / loaded. Only "plugin_active" is registered
        SaveServiceImpl session2;
        auto activeHandler2 = std::make_shared<TestSaveHandler>(1);
        session2.registerHandler("plugin_active", activeHandler2);

        EXPECT_TRUE(session2.deserialize(saveBinary1));
        EXPECT_EQ(activeHandler2->mPayload, activeData);

        // Invariant: unread chunk for missing plugin is detected and preserved
        EXPECT_TRUE(session2.hasUnreadChunk("plugin_thirdparty"));
        EXPECT_EQ(session2.getUnreadChunkRawBytes("plugin_thirdparty"), thirdpartyData);

        // Session 2 saves back out without thirdparty plugin present
        const std::vector<std::uint8_t> saveBinary2 = session2.serialize();

        // Session 3: "plugin_thirdparty" is re-installed and registered again
        SaveServiceImpl session3;
        auto activeHandler3 = std::make_shared<TestSaveHandler>(1);
        auto thirdpartyHandler3 = std::make_shared<TestSaveHandler>(3);

        session3.registerHandler("plugin_active", activeHandler3);
        session3.registerHandler("plugin_thirdparty", thirdpartyHandler3);

        EXPECT_TRUE(session3.deserialize(saveBinary2));

        // Invariant: unread chunk was preserved uncorrupted byte-for-byte across save round trip!
        EXPECT_EQ(activeHandler3->mPayload, activeData);
        EXPECT_EQ(thirdpartyHandler3->mLastReadVersion, 3u);
        EXPECT_EQ(thirdpartyHandler3->mPayload, thirdpartyData);
        EXPECT_FALSE(session3.hasUnreadChunk("plugin_thirdparty"));
    }

    TEST(SaveServiceTest, MultipleUnreadChunksPreservedTogether)
    {
        SaveServiceImpl session1;
        session1.registerHandler("plugin_a", std::make_shared<TestSaveHandler>(1, std::vector<std::uint8_t>{ 1, 2 }));
        session1.registerHandler("plugin_b", std::make_shared<TestSaveHandler>(2, std::vector<std::uint8_t>{ 3, 4 }));
        session1.registerHandler("plugin_c", std::make_shared<TestSaveHandler>(1, std::vector<std::uint8_t>{ 5, 6 }));

        const std::vector<std::uint8_t> binary1 = session1.serialize();

        // Session 2 only knows plugin_b
        SaveServiceImpl session2;
        auto handlerB = std::make_shared<TestSaveHandler>(2);
        session2.registerHandler("plugin_b", handlerB);

        ASSERT_TRUE(session2.deserialize(binary1));
        EXPECT_TRUE(session2.hasUnreadChunk("plugin_a"));
        EXPECT_TRUE(session2.hasUnreadChunk("plugin_c"));

        const std::vector<std::string> unread = session2.getUnreadChunkPluginIds();
        EXPECT_EQ(unread.size(), 2u);

        const std::vector<std::uint8_t> binary2 = session2.serialize();

        // Session 3 has all three plugins registered
        SaveServiceImpl session3;
        auto handlerA3 = std::make_shared<TestSaveHandler>(1);
        auto handlerB3 = std::make_shared<TestSaveHandler>(2);
        auto handlerC3 = std::make_shared<TestSaveHandler>(1);
        session3.registerHandler("plugin_a", handlerA3);
        session3.registerHandler("plugin_b", handlerB3);
        session3.registerHandler("plugin_c", handlerC3);

        ASSERT_TRUE(session3.deserialize(binary2));
        EXPECT_EQ(handlerA3->mPayload, (std::vector<std::uint8_t>{ 1, 2 }));
        EXPECT_EQ(handlerB3->mPayload, (std::vector<std::uint8_t>{ 3, 4 }));
        EXPECT_EQ(handlerC3->mPayload, (std::vector<std::uint8_t>{ 5, 6 }));
    }

    TEST(SaveServiceTest, VersionMigrationCalledWhenChunkVersionIsOlder)
    {
        // Session 1: Written by an older version of the plugin (v1)
        SaveServiceImpl sessionV1;
        const std::uint32_t v1State = 42;
        std::vector<std::uint8_t> v1Bytes(sizeof(v1State));
        std::memcpy(v1Bytes.data(), &v1State, sizeof(v1State));

        sessionV1.registerHandler("plugin_migrator", std::make_shared<TestSaveHandler>(1, v1Bytes));
        const std::vector<std::uint8_t> saveBinaryV1 = sessionV1.serialize();

        // Session 2: Loaded by newer version of plugin (v2)
        SaveServiceImpl sessionV2;
        auto migratingHandler = std::make_shared<MigratingSaveHandler>();
        sessionV2.registerHandler("plugin_migrator", migratingHandler);

        EXPECT_TRUE(sessionV2.deserialize(saveBinaryV1));

        // Invariant: migration handler is called with old version and data
        EXPECT_TRUE(migratingHandler->mMigrationInvoked);
        EXPECT_EQ(migratingHandler->mLoadedVersion, 1u);
        EXPECT_EQ(migratingHandler->mStateV2, 42000u); // 42 * 1000 migration formula

        // When saved back, chunk is written with v2 format
        const std::vector<std::uint8_t> saveBinaryV2 = sessionV2.serialize();

        SaveServiceImpl sessionV2Check;
        auto handlerV2Check = std::make_shared<MigratingSaveHandler>();
        sessionV2Check.registerHandler("plugin_migrator", handlerV2Check);

        EXPECT_TRUE(sessionV2Check.deserialize(saveBinaryV2));
        EXPECT_EQ(handlerV2Check->mLoadedVersion, 2u);
        EXPECT_EQ(handlerV2Check->mStateV2, 42000u);
    }

    TEST(SaveServiceTest, CorruptOrTruncatedDataReturnsFalseWithoutThrowing)
    {
        SaveServiceImpl service;
        service.registerHandler("dummy", std::make_shared<TestSaveHandler>(1));

        // Empty data
        EXPECT_FALSE(service.deserialize({}));

        // Truncated data
        const std::vector<std::uint8_t> garbage = { 0x01, 0x02, 0x03 };
        EXPECT_NO_THROW({
            const bool result = service.deserialize(garbage);
            EXPECT_FALSE(result);
        });
    }

    TEST(SaveServiceTest, HandlerUnregistration)
    {
        SaveServiceImpl service;
        auto handler = std::make_shared<TestSaveHandler>(1, std::vector<std::uint8_t>{ 99 });
        service.registerHandler("plugin_temp", handler);

        const std::vector<std::uint8_t> binary = service.serialize();

        service.unregisterHandler("plugin_temp");

        // Loading after unregistering stores it as an unread chunk
        EXPECT_TRUE(service.deserialize(binary));
        EXPECT_TRUE(service.hasUnreadChunk("plugin_temp"));
    }

    TEST(SaveServiceTest, UnreadChunkRetainedIfHandlerThrowsOnRegister)
    {
        SaveServiceImpl session1;
        session1.registerHandler(
            "plugin_faulty", std::make_shared<TestSaveHandler>(1, std::vector<std::uint8_t>{ 10, 20, 30 }));
        const auto binary = session1.serialize();

        SaveServiceImpl session2;
        ASSERT_TRUE(session2.deserialize(binary));
        EXPECT_TRUE(session2.hasUnreadChunk("plugin_faulty"));

        class ExplodingHandler : public SaveChunkHandler
        {
        public:
            uint32_t currentVersion() const noexcept override { return 1; }
            void writeSaveChunk(SaveChunkWriter&) override {}
            void readSaveChunk(uint32_t, SaveChunkReader&) override
            {
                throw std::runtime_error("Disk corrupted / parser bug!");
            }
        };

        // Registering exploding handler must not throw and must retain unread chunk
        EXPECT_NO_THROW(session2.registerHandler("plugin_faulty", std::make_shared<ExplodingHandler>()));
        EXPECT_TRUE(session2.hasUnreadChunk("plugin_faulty"));

        // Chunk is preserved across serialize
        const auto binary2 = session2.serialize();
        SaveServiceImpl session3;
        ASSERT_TRUE(session3.deserialize(binary2));
        EXPECT_TRUE(session3.hasUnreadChunk("plugin_faulty"));
        EXPECT_EQ(session3.getUnreadChunkRawBytes("plugin_faulty"), (std::vector<std::uint8_t>{ 10, 20, 30 }));
    }

    // ---- Chunks as the game's save file holds them (task 0016) -----------------------------------------------------

    using Bytes = std::vector<std::uint8_t>;

    // A handler whose written bytes do not depend on what it read, so the two can be told apart, and which counts
    // calls and throws on request.
    class ScriptedHandler : public SaveChunkHandler
    {
    public:
        explicit ScriptedHandler(std::uint32_t version, Bytes toWrite = {})
            : mVersion(version)
            , mToWrite(std::move(toWrite))
        {
        }

        std::uint32_t currentVersion() const noexcept override { return mVersion; }

        void writeSaveChunk(SaveChunkWriter& writer) override
        {
            ++mWrites;
            if (!mToWrite.empty())
                writer.write(mToWrite.data(), mToWrite.size());
            if (mThrowOnWrite)
                throw std::runtime_error("write failed");
        }

        void readSaveChunk(std::uint32_t chunkVersion, SaveChunkReader& reader) override
        {
            ++mReads;
            mReadVersion = chunkVersion;
            mReadBytes.resize(reader.size());
            if (!mReadBytes.empty())
                reader.read(mReadBytes.data(), mReadBytes.size());
            if (mThrowOnRead)
                throw std::runtime_error("read failed");
        }

        std::uint32_t mVersion;
        Bytes mToWrite;
        bool mThrowOnRead = false;
        bool mThrowOnWrite = false;
        int mReads = 0;
        int mWrites = 0;
        std::uint32_t mReadVersion = 0;
        Bytes mReadBytes;
    };

    SaveServiceImpl::Chunk makeChunk(std::string id, std::uint32_t version, Bytes bytes)
    {
        SaveServiceImpl::Chunk chunk;
        chunk.pluginId = std::move(id);
        chunk.version = version;
        chunk.bytes = std::move(bytes);
        return chunk;
    }

    void expectChunk(
        const SaveServiceImpl::Chunk& chunk, std::string_view id, std::uint32_t version, const Bytes& bytes)
    {
        EXPECT_EQ(chunk.pluginId, id);
        EXPECT_EQ(chunk.version, version);
        EXPECT_EQ(chunk.bytes, bytes);
    }

    TEST(SaveServiceChunksTest, NoPluginsAndNoChunksCollectNothing)
    {
        // Saves without plugins stay byte-identical only if there is nothing to write
        SaveServiceImpl service;
        EXPECT_TRUE(service.collectChunks().empty());
    }

    TEST(SaveServiceChunksTest, CollectIsSortedByPluginIdWithEachHandlersCurrentVersion)
    {
        SaveServiceImpl service;
        service.registerHandler("zeta", std::make_shared<ScriptedHandler>(3, Bytes{ 3 }));
        service.registerHandler("alpha", std::make_shared<ScriptedHandler>(1, Bytes{ 1 }));
        service.registerHandler("mid", std::make_shared<ScriptedHandler>(2, Bytes{ 2 }));

        const auto chunks = service.collectChunks();

        ASSERT_EQ(chunks.size(), 3u);
        expectChunk(chunks[0], "alpha", 1, Bytes{ 1 });
        expectChunk(chunks[1], "mid", 2, Bytes{ 2 });
        expectChunk(chunks[2], "zeta", 3, Bytes{ 3 });
    }

    TEST(SaveServiceChunksTest, SeveralWritesFromAHandlerAreJoinedInOrder)
    {
        class TwoWritesHandler : public SaveChunkHandler
        {
        public:
            std::uint32_t currentVersion() const noexcept override { return 1; }
            void writeSaveChunk(SaveChunkWriter& writer) override
            {
                const std::uint8_t first[] = { 1, 2 };
                const std::uint8_t second[] = { 0, 3 };
                writer.write(first, sizeof(first));
                writer.write(second, sizeof(second));
            }
            void readSaveChunk(std::uint32_t, SaveChunkReader&) override {}
        };

        SaveServiceImpl service;
        service.registerHandler("plugin", std::make_shared<TwoWritesHandler>());

        const auto chunks = service.collectChunks();

        ASSERT_EQ(chunks.size(), 1u);
        expectChunk(chunks[0], "plugin", 1, Bytes{ 1, 2, 0, 3 });
    }

    TEST(SaveServiceChunksTest, PreservedChunksAreCollectedSortedAmongHandlersByteForByte)
    {
        const Bytes absentBytes{ 0, 0xFF, 0, 1, 0 };

        SaveServiceImpl service;
        service.registerHandler("zeta", std::make_shared<ScriptedHandler>(1, Bytes{ 9 }));
        service.registerHandler("alpha", std::make_shared<ScriptedHandler>(1, Bytes{ 8 }));
        service.loadChunk(makeChunk("mid_absent", 7, absentBytes));

        const auto chunks = service.collectChunks();

        ASSERT_EQ(chunks.size(), 3u);
        expectChunk(chunks[0], "alpha", 1, Bytes{ 8 });
        // The stored version, which is the absent plugin's, not one this host knows
        expectChunk(chunks[1], "mid_absent", 7, absentBytes);
        expectChunk(chunks[2], "zeta", 1, Bytes{ 9 });
    }

    TEST(SaveServiceChunksTest, AbsentPluginsChunkIsWrittenBackUnchangedAcrossSaves)
    {
        const Bytes bytes{ 1, 0, 2, 0, 0, 3 };

        SaveServiceImpl first;
        first.loadChunk(makeChunk("absent", 4, bytes));
        EXPECT_TRUE(first.hasUnreadChunk("absent"));
        const auto saved = first.collectChunks();
        ASSERT_EQ(saved.size(), 1u);
        expectChunk(saved[0], "absent", 4, bytes);

        // Collecting does not use the chunk up: the game can save twice from one load
        const auto savedAgain = first.collectChunks();
        ASSERT_EQ(savedAgain.size(), 1u);
        expectChunk(savedAgain[0], "absent", 4, bytes);

        // The next session loads what the last one saved
        SaveServiceImpl second;
        second.loadChunk(saved[0]);
        const auto savedBySecond = second.collectChunks();
        ASSERT_EQ(savedBySecond.size(), 1u);
        expectChunk(savedBySecond[0], "absent", 4, bytes);
    }

    TEST(SaveServiceChunksTest, AbsentPluginsEmptyChunkIsStillWrittenBack)
    {
        SaveServiceImpl service;
        service.loadChunk(makeChunk("absent", 2, {}));

        const auto chunks = service.collectChunks();

        ASSERT_EQ(chunks.size(), 1u);
        expectChunk(chunks[0], "absent", 2, {});
    }

    TEST(SaveServiceChunksTest, LoadChunkReachesTheHandlerWithTheStoredVersion)
    {
        const Bytes bytes{ 4, 0, 5 };
        auto handler = std::make_shared<ScriptedHandler>(1);
        SaveServiceImpl service;
        service.registerHandler("plugin", handler);

        service.loadChunk(makeChunk("plugin", 1, bytes));

        EXPECT_EQ(handler->mReads, 1);
        EXPECT_EQ(handler->mReadVersion, 1u);
        EXPECT_EQ(handler->mReadBytes, bytes);
        EXPECT_FALSE(service.hasUnreadChunk("plugin"));
    }

    TEST(SaveServiceChunksTest, OlderVersionReachesTheMigrationPathAndSavesAsCurrent)
    {
        const std::uint32_t v1State = 42;
        Bytes v1Bytes(sizeof(v1State));
        std::memcpy(v1Bytes.data(), &v1State, sizeof(v1State));

        auto handler = std::make_shared<MigratingSaveHandler>();
        SaveServiceImpl service;
        service.registerHandler("plugin", handler);

        service.loadChunk(makeChunk("plugin", 1, v1Bytes));

        EXPECT_TRUE(handler->mMigrationInvoked);
        EXPECT_EQ(handler->mLoadedVersion, 1u);
        EXPECT_EQ(handler->mStateV2, 42000u); // MigratingSaveHandler multiplies a v1 state by 1000

        const auto chunks = service.collectChunks();
        ASSERT_EQ(chunks.size(), 1u);
        EXPECT_EQ(chunks[0].version, 2u);
        EXPECT_EQ(chunks[0].bytes.size(), sizeof(std::uint64_t));
    }

    TEST(SaveServiceChunksTest, HandlerRegisteredLaterConsumesThePreservedChunk)
    {
        const Bytes bytes{ 1, 0, 2 };
        SaveServiceImpl service;
        service.loadChunk(makeChunk("plugin", 3, bytes));
        ASSERT_TRUE(service.hasUnreadChunk("plugin"));

        auto handler = std::make_shared<ScriptedHandler>(5, Bytes{ 7 });
        service.registerHandler("plugin", handler);

        EXPECT_EQ(handler->mReads, 1);
        EXPECT_EQ(handler->mReadVersion, 3u);
        EXPECT_EQ(handler->mReadBytes, bytes);
        EXPECT_FALSE(service.hasUnreadChunk("plugin"));
    }

    TEST(SaveServiceChunksTest, LiveHandlerWinsOverAPreservedChunkWithItsId)
    {
        SaveServiceImpl service;
        service.loadChunk(makeChunk("plugin", 3, Bytes{ 1, 2 }));
        service.registerHandler("plugin", std::make_shared<ScriptedHandler>(5, Bytes{ 7 }));

        const auto chunks = service.collectChunks();

        // Exactly one chunk with that ID, and it is the handler's
        ASSERT_EQ(chunks.size(), 1u);
        expectChunk(chunks[0], "plugin", 5, Bytes{ 7 });
    }

    TEST(SaveServiceChunksTest, ThrowingWriteIsContainedAndOtherPluginsStillSave)
    {
        auto bad = std::make_shared<ScriptedHandler>(1, Bytes{ 1, 2 });
        bad->mThrowOnWrite = true;

        SaveServiceImpl service;
        service.registerHandler("bad", bad);
        service.registerHandler("good", std::make_shared<ScriptedHandler>(2, Bytes{ 3 }));

        std::vector<SaveServiceImpl::Chunk> chunks;
        EXPECT_NO_THROW(chunks = service.collectChunks());

        // bad wrote two bytes and then threw: half a chunk must not reach the file
        ASSERT_EQ(chunks.size(), 1u);
        expectChunk(chunks[0], "good", 2, Bytes{ 3 });
        EXPECT_EQ(bad->mWrites, 1);
    }

    TEST(SaveServiceChunksTest, ThrowingHandlerRegisteredLaterKeepsThePreservedChunk)
    {
        const Bytes bytes{ 1, 0, 2 };
        auto bad = std::make_shared<ScriptedHandler>(5, Bytes{ 9, 9 });
        bad->mThrowOnRead = true;
        bad->mThrowOnWrite = true;

        SaveServiceImpl service;
        service.loadChunk(makeChunk("bad", 3, bytes));
        EXPECT_NO_THROW(service.registerHandler("bad", bad));

        std::vector<SaveServiceImpl::Chunk> chunks;
        EXPECT_NO_THROW(chunks = service.collectChunks());

        ASSERT_EQ(chunks.size(), 1u);
        expectChunk(chunks[0], "bad", 3, bytes);
    }

    TEST(SaveServiceChunksTest, ThrowingReadOnLoadKeepsTheBytesAndWithdrawsTheHandler)
    {
        const Bytes bytes{ 1, 0, 2, 3 };
        auto bad = std::make_shared<ScriptedHandler>(5, Bytes{ 9 });
        bad->mThrowOnRead = true;

        SaveServiceImpl service;
        service.registerHandler("bad", bad);

        EXPECT_NO_THROW(service.loadChunk(makeChunk("bad", 2, bytes)));
        EXPECT_EQ(bad->mReads, 1);
        EXPECT_TRUE(service.hasUnreadChunk("bad"));
        EXPECT_EQ(service.getUnreadChunkRawBytes("bad"), bytes);

        // Withdrawn: it is not asked to write, so its bytes cannot overwrite the ones it failed to read
        const auto chunks = service.collectChunks();
        EXPECT_EQ(bad->mWrites, 0);
        ASSERT_EQ(chunks.size(), 1u);
        expectChunk(chunks[0], "bad", 2, bytes);

        // And it is not offered the next chunk either
        const Bytes newer{ 6 };
        service.loadChunk(makeChunk("bad", 3, newer));
        EXPECT_EQ(bad->mReads, 1);
        const auto newerChunks = service.collectChunks();
        ASSERT_EQ(newerChunks.size(), 1u);
        expectChunk(newerChunks[0], "bad", 3, newer);
    }

    TEST(SaveServiceChunksTest, ThrowingReadDoesNotAffectOtherPlugins)
    {
        auto bad = std::make_shared<ScriptedHandler>(1);
        bad->mThrowOnRead = true;
        auto good = std::make_shared<ScriptedHandler>(1, Bytes{ 5 });

        SaveServiceImpl service;
        service.registerHandler("bad", bad);
        service.registerHandler("good", good);

        service.loadChunk(makeChunk("bad", 1, Bytes{ 1 }));
        service.loadChunk(makeChunk("good", 1, Bytes{ 2 }));

        EXPECT_EQ(good->mReadBytes, Bytes{ 2 });
        const auto chunks = service.collectChunks();
        ASSERT_EQ(chunks.size(), 2u);
        expectChunk(chunks[0], "bad", 1, Bytes{ 1 });
        expectChunk(chunks[1], "good", 1, Bytes{ 5 });
    }

    TEST(SaveServiceChunksTest, ClearUnreadChunksForgetsPreservedChunksAndKeepsHandlers)
    {
        SaveServiceImpl service;
        service.registerHandler("live", std::make_shared<ScriptedHandler>(1, Bytes{ 5 }));
        service.loadChunk(makeChunk("absent_a", 1, Bytes{ 1 }));
        service.loadChunk(makeChunk("absent_b", 1, Bytes{ 2 }));

        service.clearUnreadChunks();

        EXPECT_FALSE(service.hasUnreadChunk("absent_a"));
        EXPECT_FALSE(service.hasUnreadChunk("absent_b"));
        EXPECT_TRUE(service.getUnreadChunkPluginIds().empty());
        const auto chunks = service.collectChunks();
        ASSERT_EQ(chunks.size(), 1u);
        expectChunk(chunks[0], "live", 1, Bytes{ 5 });

        // A handler registered after the clear finds nothing to read
        auto late = std::make_shared<ScriptedHandler>(1);
        service.registerHandler("absent_a", late);
        EXPECT_EQ(late->mReads, 0);
    }
} // namespace
