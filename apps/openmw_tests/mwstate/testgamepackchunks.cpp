// Task 0084 leftovers, lane I stage I4: a game pack's save chunks in the .omwsave record stream. The rule that matters
// most: a session that writes no chunk produces the same save bytes as before (nothing is written, nothing counted).
#include <apps/openmw/mwbase/extensionmanager.hpp>
#include <apps/openmw/mwstate/gamepackchunks.hpp>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/extension/save_chunk_store.hpp>

#include <openeden/game/save_chunks.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace ext = openeden::extension;

    std::vector<std::byte> bytesOf(const std::string& text)
    {
        std::vector<std::byte> out;
        for (const char c : text)
            out.push_back(static_cast<std::byte>(c));
        return out;
    }

    // Writes a save file the way SaveGame does, with `records` between the header and the end.
    std::string saveBytes(const std::function<void(ESM::ESMWriter&)>& records)
    {
        std::stringstream stream;
        ESM::ESMWriter writer;
        writer.setFormatVersion(ESM::DefaultFormatVersion);
        writer.save(stream);
        records(writer);
        writer.close();
        return stream.str();
    }

    void someOtherRecord(ESM::ESMWriter& writer)
    {
        writer.startRecord(ESM::REC_RAND);
        writer.writeHNString("RAND", "state");
        writer.endRecord(ESM::REC_RAND);
    }

    class CounterHandler final : public openeden::sdk::SaveChunkHandler
    {
    public:
        std::string_view chunkName() const override { return "counter"; }
        std::uint32_t chunkVersion() const override { return 3; }
        void write(openeden::sdk::ChunkWriter& writer) override { writer.writeValue(mValue); }
        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t) override { return reader.readValue(mValue); }
        std::int32_t mValue = 0;
    };

    TEST(GamePackChunkRecord, nothingToWriteMeansNothingIsWrittenOrCounted)
    {
        const std::vector<std::byte> none;
        EXPECT_EQ(MWState::countGamePackChunkRecords(none), 0u);

        const std::string without = saveBytes([](ESM::ESMWriter& writer) { someOtherRecord(writer); });
        const std::string with = saveBytes([&](ESM::ESMWriter& writer) {
            someOtherRecord(writer);
            MWState::writeGamePackChunkRecord(writer, none);
        });
        // The save of a session with no chunk is byte for byte the save that was written before this record existed
        EXPECT_EQ(with, without);
    }

    TEST(GamePackChunkRecord, someBytesAreOneRecordAndComeBackExactly)
    {
        const std::vector<std::byte> payload = bytesOf("chunk bytes \0 with a zero and \xff high");
        EXPECT_EQ(MWState::countGamePackChunkRecords(payload), 1u);

        const std::string bytes = saveBytes([&](ESM::ESMWriter& writer) {
            someOtherRecord(writer);
            MWState::writeGamePackChunkRecord(writer, payload);
            someOtherRecord(writer);
        });

        ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(bytes), "save");
        int seen = 0;
        std::vector<std::byte> got;
        while (reader.hasMoreRecs())
        {
            const ESM::NAME n = reader.getRecName();
            reader.getRecHeader();
            if (n.toInt() == ESM::REC_GPCH)
                got = MWState::readGamePackChunkRecord(reader);
            else
                reader.skipRecord();
            ++seen;
        }
        EXPECT_EQ(seen, 3);
        EXPECT_EQ(got, payload);
    }

    TEST(GamePackChunkRecord, aRecordLargerThanTheCapIsRefusedWithoutReadingIt)
    {
        const std::vector<std::byte> payload(64, std::byte{ 7 });
        const std::string bytes
            = saveBytes([&](ESM::ESMWriter& writer) { MWState::writeGamePackChunkRecord(writer, payload); });
        ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(bytes), "save");
        ASSERT_TRUE(reader.hasMoreRecs());
        const ESM::NAME n = reader.getRecName();
        ASSERT_EQ(n.toInt(), ESM::REC_GPCH);
        reader.getRecHeader();
        EXPECT_THROW((void)MWState::readGamePackChunkRecord(reader, 16), std::runtime_error);
    }

    TEST(GamePackChunkRecord, anOldReaderThatKnowsNothingOfItSkipsTheRecord)
    {
        // LoadGame's default branch skips what it does not know (a warning); the neighbours still read
        const std::vector<std::byte> payload = bytesOf("abc");
        const std::string bytes = saveBytes([&](ESM::ESMWriter& writer) {
            MWState::writeGamePackChunkRecord(writer, payload);
            someOtherRecord(writer);
        });
        ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(bytes), "save");
        int others = 0;
        while (reader.hasMoreRecs())
        {
            const ESM::NAME n = reader.getRecName();
            reader.getRecHeader();
            if (n.toInt() == ESM::REC_RAND)
                ++others;
            reader.skipRecord();
        }
        EXPECT_EQ(others, 1);
    }

    TEST(GamePackChunkRecord, theWholeRoundTripFromHandlerToHandlerThroughTheSaveBytes)
    {
        // collect -> encode -> record -> bytes -> record -> decode -> restore, as the engine does on a save and a load
        CounterHandler saver;
        saver.mValue = 12345;
        ext::SaveChunkServiceImpl savingSide;
        ASSERT_TRUE(savingSide.registerHandler("pack", saver));
        const ext::ChunkCollectResult collected = savingSide.collect();
        ASSERT_TRUE(collected.ok) << collected.error;
        const std::vector<std::byte> encoded = ext::encodeChunks(collected.chunks);

        const std::string bytes
            = saveBytes([&](ESM::ESMWriter& writer) { MWState::writeGamePackChunkRecord(writer, encoded); });

        ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(bytes), "save");
        ASSERT_TRUE(reader.hasMoreRecs());
        const ESM::NAME n = reader.getRecName();
        ASSERT_EQ(n.toInt(), ESM::REC_GPCH);
        reader.getRecHeader();
        const std::vector<std::byte> read = MWState::readGamePackChunkRecord(reader);

        std::vector<ext::SavedChunk> decoded;
        std::string error;
        ASSERT_TRUE(ext::decodeChunks(read, decoded, error)) << error;
        CounterHandler loader;
        ext::SaveChunkServiceImpl loadingSide;
        ASSERT_TRUE(loadingSide.registerHandler("pack", loader));
        const ext::ChunkRestoreResult restored = loadingSide.restore(decoded);
        ASSERT_TRUE(restored.ok) << restored.error;
        EXPECT_EQ(loader.mValue, 12345);
    }

    // Task 0084 leftovers, lane I part 2, stage I2b: a failed game pack refuses a new game and a load.
    class FakeExtensions final : public MWBase::ExtensionManager
    {
    public:
        void objectAddedToScene(const MWWorld::Ptr&) override {}
        void objectRemovedFromScene(const MWWorld::Ptr&) override {}
        void cellLoaded(std::string_view) override {}
        void cellUnloaded(std::string_view) override {}
        void cellChanged(std::string_view) override {}
        void actorDied(const MWWorld::Ptr&) override {}
        void actorRevived(const MWWorld::Ptr&) override {}
        void actorHit(const MWWorld::Ptr&, const MWWorld::Ptr&, float, bool) override {}
        void gameLoaded() override {}
        void playerRebuilt(const MWWorld::Ptr&) override {}
        void gameSaved() override {}

        const std::string& sessionError() const override { return mError; }

        std::string mError;
    };

    TEST(GamePackSessionRefusal, aSessionWithoutAFailedPackRefusesNothing)
    {
        const FakeExtensions extensions;
        EXPECT_TRUE(MWState::gamePackSessionRefusal(extensions).empty());
    }

    TEST(GamePackSessionRefusal, aFailedPackRefusesWithItsLocatedMessage)
    {
        FakeExtensions extensions;
        extensions.mError = "Game pack 'x' failed at start: boom. The game cannot go on and saving is refused.";
        const std::string refusal = MWState::gamePackSessionRefusal(extensions);
        EXPECT_THAT(refusal, testing::HasSubstr("Game pack 'x' failed at start: boom"));
        EXPECT_THAT(refusal, testing::HasSubstr("new game"));
        EXPECT_THAT(refusal, testing::HasSubstr("load"));
    }
}
