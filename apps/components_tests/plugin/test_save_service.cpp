#include <components/plugin/save_service.hpp>
#include <openeden/save.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
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
} // namespace
