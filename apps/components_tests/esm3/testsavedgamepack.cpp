// Task 0084: the save header records the game pack's id and version (additive; a save made with no game pack is
// byte-for-byte what it was before). Written from the spec before the code.
#include <components/esm/fourcc.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/savedgame.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace
{
    constexpr std::uint32_t sRecord = ESM::fourCC("FAKE");

    ESM::SavedGame makeHeader()
    {
        ESM::SavedGame header;
        header.mContentFiles = { "Morrowind.esm" };
        header.mPlayerName = "Tester";
        header.mPlayerLevel = 3;
        header.mPlayerClassName = "Custom";
        header.mPlayerCellName = "Seyda Neen";
        header.mInGameTime = { 12.5f, 3, 4, 427 };
        header.mTimePlayed = 100.0;
        header.mDescription = "d";
        header.mScreenshot = { 1, 2, 3 };
        header.mCurrentDay = 5;
        header.mCurrentHealth = 40.f;
        header.mMaximumHealth = 50.f;
        return header;
    }

    std::string writeHeader(const ESM::SavedGame& header)
    {
        ESM::ESMWriter writer;
        std::stringstream stream;
        writer.setAuthor("test");
        writer.setDescription("test");
        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);
        writer.save(stream);
        writer.startRecord(sRecord);
        header.save(writer);
        writer.endRecord(sRecord);
        writer.close();
        return stream.str();
    }

    ESM::SavedGame readHeader(const std::string& bytes)
    {
        ESM::ESMReader reader;
        reader.open(std::make_unique<std::stringstream>(bytes), "stream");
        EXPECT_TRUE(reader.hasMoreRecs());
        reader.getRecName();
        reader.getRecHeader();
        ESM::SavedGame header;
        header.load(reader);
        return header;
    }
}

TEST(SavedGamePack, NoGamePackWritesNoExtraSubrecords)
{
    const std::string bytes = writeHeader(makeHeader());
    EXPECT_EQ(bytes.find("GPCK"), std::string::npos);
    EXPECT_EQ(bytes.find("GPVR"), std::string::npos);
}

TEST(SavedGamePack, SaveWithoutAPackLoadsWithEmptyPackFields)
{
    const ESM::SavedGame loaded = readHeader(writeHeader(makeHeader()));
    EXPECT_TRUE(loaded.mGamePackId.empty());
    EXPECT_TRUE(loaded.mGamePackVersion.empty());
    EXPECT_EQ(loaded.mPlayerName, "Tester");
    EXPECT_EQ(loaded.mMaximumHealth, 50.f);
}

TEST(SavedGamePack, PackIdAndVersionRoundTrip)
{
    ESM::SavedGame header = makeHeader();
    header.mGamePackId = "game_morrowind";
    header.mGamePackVersion = "1.2.3";
    const std::string bytes = writeHeader(header);
    EXPECT_NE(bytes.find("GPCK"), std::string::npos);
    const ESM::SavedGame loaded = readHeader(bytes);
    EXPECT_EQ(loaded.mGamePackId, "game_morrowind");
    EXPECT_EQ(loaded.mGamePackVersion, "1.2.3");
    // The rest of the header is untouched
    EXPECT_EQ(loaded.mPlayerName, "Tester");
    EXPECT_EQ(loaded.mContentFiles, header.mContentFiles);
    EXPECT_EQ(loaded.mCurrentDay, 5);
    EXPECT_EQ(loaded.mCurrentHealth, 40.f);
    EXPECT_EQ(loaded.mMaximumHealth, 50.f);
}

TEST(SavedGamePack, HeaderWithAPackIsTheHeaderWithoutOneExtended)
{
    const std::string plain = writeHeader(makeHeader());
    ESM::SavedGame header = makeHeader();
    header.mGamePackId = "g";
    const std::string withPack = writeHeader(header);
    // Additive: the bytes of every earlier subrecord are the same
    const std::size_t from = plain.find("PLNA"); // after the record's size, which differs
    const std::size_t tail = plain.find("MHLT");
    ASSERT_NE(from, std::string::npos);
    ASSERT_NE(tail, std::string::npos);
    EXPECT_EQ(withPack.substr(from, tail - from), plain.substr(from, tail - from));
}
