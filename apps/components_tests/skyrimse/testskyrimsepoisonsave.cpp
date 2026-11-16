// Task 0233: the poison on a weapon is saved. A coating is the weapon item, the poison item, its name, its charges and
// effects; the chunk is `poisons` version 1 and the `crafting` chunk (name, version, bytes) is untouched, so a save
// made before this build has no such chunk and loads with clean weapons. Written by hand from the format in poison.hpp.
#include <game_skyrimse/crafting.hpp>
#include <game_skyrimse/poison.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::crafting;

    class SaveWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class SaveReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit SaveReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    WeaponPoison poison(FormId item, const char* name, int charges)
    {
        WeaponPoison p;
        p.mPoison = item;
        p.mName = name;
        p.mCharges = charges;
        p.mEffects.push_back({ 0x2000, 10.f, 0, 5 });
        p.mEffects.push_back({ 0x2001, 2.5f, 3, 0 });
        return p;
    }

    std::vector<unsigned char> saved(const PoisonCoatings& coatings)
    {
        SaveWriter writer;
        coatings.write(writer);
        return writer.mBytes;
    }

    struct PoisonSave : ::testing::Test
    {
        PoisonCoatings mCoatings;
        PoisonChunk mChunk{ mCoatings };
    };

    TEST_F(PoisonSave, theChunkIsNamedAndVersionedAndTheCraftingChunkIsUntouched)
    {
        EXPECT_EQ(mChunk.chunkName(), "poisons");
        EXPECT_EQ(mChunk.chunkVersion(), 1u);
        EXPECT_EQ(CraftingChunk::kName, "crafting");
        EXPECT_EQ(CraftingChunk::kVersion, 1u);
    }

    TEST_F(PoisonSave, aTableRoundTripsWithItsChargesAndEffects)
    {
        ASSERT_EQ(mCoatings.apply(0x10, poison(0x500, "Deadly Poison", 3)), CoatStatus::Ok);
        ASSERT_EQ(mCoatings.apply(0xFF000001, poison(0x501, "Frostbite Venom", 1)), CoatStatus::Ok);
        const std::vector<unsigned char> bytes = saved(mCoatings);
        EXPECT_FALSE(bytes.empty());

        PoisonCoatings other;
        PoisonChunk chunk(other);
        SaveReader reader(bytes);
        ASSERT_TRUE(chunk.read(reader, 1));
        EXPECT_EQ(reader.remaining(), 0u);
        ASSERT_EQ(other.size(), 2u);
        const WeaponPoison* a = other.find(0x10);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->mPoison, 0x500u);
        EXPECT_EQ(a->mName, "Deadly Poison");
        EXPECT_EQ(a->mCharges, 3);
        ASSERT_EQ(a->mEffects.size(), 2u);
        EXPECT_EQ(a->mEffects[0].mEffect, 0x2000u);
        EXPECT_FLOAT_EQ(a->mEffects[0].mMagnitude, 10.f);
        EXPECT_EQ(a->mEffects[0].mDuration, 5u);
        EXPECT_EQ(a->mEffects[1].mArea, 3u);
        const WeaponPoison* b = other.find(0xFF000001);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(b->mCharges, 1);
        // what it saves is what it loaded, byte for byte
        EXPECT_EQ(saved(other), bytes);
    }

    TEST_F(PoisonSave, aRestoredCoatingStillSpendsItsCharges)
    {
        ASSERT_EQ(mCoatings.apply(0x10, poison(0x500, "Deadly Poison", 2)), CoatStatus::Ok);
        PoisonCoatings other;
        PoisonChunk chunk(other);
        const std::vector<unsigned char> bytes = saved(mCoatings);
        SaveReader reader(bytes);
        ASSERT_TRUE(chunk.read(reader, 1));
        EXPECT_EQ(other.spend(0x10), 1);
        EXPECT_EQ(other.spend(0x10), 0);
        EXPECT_EQ(other.find(0x10), nullptr);
    }

    TEST_F(PoisonSave, anEmptyTableLoadsAsAnEmptyTableAndClearsWhatWasThere)
    {
        PoisonCoatings empty;
        const std::vector<unsigned char> bytes = saved(empty);
        ASSERT_EQ(mCoatings.apply(0x10, poison(0x500, "Old", 1)), CoatStatus::Ok);
        SaveReader reader(bytes);
        ASSERT_TRUE(mChunk.read(reader, 1));
        EXPECT_EQ(mCoatings.size(), 0u);
    }

    TEST_F(PoisonSave, theRestoredSinkHearsTheCount)
    {
        ASSERT_EQ(mCoatings.apply(0x10, poison(0x500, "A", 1)), CoatStatus::Ok);
        ASSERT_EQ(mCoatings.apply(0x11, poison(0x501, "B", 1)), CoatStatus::Ok);
        const std::vector<unsigned char> bytes = saved(mCoatings);
        PoisonCoatings other;
        PoisonChunk chunk(other);
        std::size_t heard = 99;
        chunk.setRestoredSink([&](std::size_t n) { heard = n; });
        SaveReader reader(bytes);
        ASSERT_TRUE(chunk.read(reader, 1));
        EXPECT_EQ(heard, 2u);
    }

    TEST_F(PoisonSave, everyTruncationIsRefusedAndChangesNothing)
    {
        ASSERT_EQ(mCoatings.apply(0x10, poison(0x500, "Deadly Poison", 3)), CoatStatus::Ok);
        const std::vector<unsigned char> good = saved(mCoatings);
        for (std::size_t length = 0; length < good.size(); ++length)
        {
            PoisonCoatings other;
            ASSERT_EQ(other.apply(0x77, poison(0x600, "Keep", 4)), CoatStatus::Ok);
            PoisonChunk chunk(other);
            const std::vector<unsigned char> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
            SaveReader reader(cut);
            EXPECT_FALSE(chunk.read(reader, 1)) << "length " << length;
            ASSERT_EQ(other.size(), 1u) << "length " << length;
            EXPECT_NE(other.find(0x77), nullptr) << "length " << length;
        }
    }

    TEST_F(PoisonSave, aWrongVersionIsRefusedWithAReasonAndTheSinkHearsIt)
    {
        std::string heard;
        mChunk.setRefusalSink([&](const std::string& why) { heard = why; });
        const std::vector<unsigned char> none;
        SaveReader reader(none);
        EXPECT_FALSE(mChunk.read(reader, 2));
        EXPECT_NE(heard.find("version 2"), std::string::npos);
        EXPECT_EQ(mChunk.refusal(), heard);
    }

    TEST_F(PoisonSave, trailingBytesAreRefused)
    {
        ASSERT_EQ(mCoatings.apply(0x10, poison(0x500, "A", 1)), CoatStatus::Ok);
        std::vector<unsigned char> bytes = saved(mCoatings);
        bytes.push_back(0);
        PoisonCoatings other;
        PoisonChunk chunk(other);
        SaveReader reader(bytes);
        EXPECT_FALSE(chunk.read(reader, 1));
        EXPECT_EQ(other.size(), 0u);
    }

    // Hand-built bytes: count, then per coating weapon, poison, name, charges, effect count, effects.
    std::vector<unsigned char> handBuilt(std::uint32_t count, std::uint32_t weapon, std::int32_t charges,
        std::uint8_t effects, float magnitude, std::uint32_t weapon2 = 0)
    {
        SaveWriter w;
        w.writeValue(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            w.writeValue(i == 1 && weapon2 != 0 ? weapon2 : weapon);
            w.writeValue(static_cast<std::uint32_t>(0x500 + i));
            w.writeString("Poison");
            w.writeValue(charges);
            w.writeValue(effects);
            for (std::uint8_t k = 0; k < effects; ++k)
            {
                w.writeValue(static_cast<std::uint32_t>(0x2000 + k));
                w.writeValue(magnitude);
                w.writeValue(static_cast<std::uint32_t>(0));
                w.writeValue(static_cast<std::uint32_t>(3));
            }
        }
        return w.mBytes;
    }

    TEST_F(PoisonSave, theHandBuiltGoodBytesLoadSoTheRefusalsBelowAreAboutTheirOneFault)
    {
        const std::vector<unsigned char> good = handBuilt(1, 0x10, 2, 1, 5.f);
        SaveReader reader(good);
        ASSERT_TRUE(mChunk.read(reader, 1));
        ASSERT_NE(mCoatings.find(0x10), nullptr);
        EXPECT_EQ(mCoatings.find(0x10)->mCharges, 2);
    }

    TEST_F(PoisonSave, aDamagedTableIsRefusedWhole)
    {
        const struct
        {
            const char* what;
            std::vector<unsigned char> bytes;
        } cases[] = {
            { "no weapon", handBuilt(1, 0, 2, 1, 5.f) },
            { "no charges", handBuilt(1, 0x10, 0, 1, 5.f) },
            { "negative charges", handBuilt(1, 0x10, -1, 1, 5.f) },
            { "too many charges", handBuilt(1, 0x10, PoisonCoatings::kMaxCharges + 1, 1, 5.f) },
            { "no effects", handBuilt(1, 0x10, 2, 0, 5.f) },
            { "too many effects", handBuilt(1, 0x10, 2, 9, 5.f) },
            { "a magnitude that is not a number", handBuilt(1, 0x10, 2, 1, std::numeric_limits<float>::quiet_NaN()) },
            { "a weapon listed twice", handBuilt(2, 0x10, 2, 1, 5.f, 0x10) },
            { "more coatings than the table holds",
                handBuilt(static_cast<std::uint32_t>(PoisonCoatings::kMax + 1), 0x10, 2, 1, 5.f) },
        };
        for (const auto& c : cases)
        {
            PoisonCoatings other;
            ASSERT_EQ(other.apply(0x77, poison(0x600, "Keep", 4)), CoatStatus::Ok);
            PoisonChunk chunk(other);
            SaveReader reader(c.bytes);
            EXPECT_FALSE(chunk.read(reader, 1)) << c.what;
            EXPECT_EQ(other.size(), 1u) << c.what;
            EXPECT_NE(other.find(0x77), nullptr) << c.what;
        }
    }

    TEST_F(PoisonSave, aNameThatIsTooLongIsRefused)
    {
        SaveWriter w;
        w.writeValue(static_cast<std::uint32_t>(1));
        w.writeValue(static_cast<std::uint32_t>(0x10));
        w.writeValue(static_cast<std::uint32_t>(0x500));
        w.writeString(std::string(PoisonCoatings::kMaxName + 1, 'x'));
        w.writeValue(static_cast<std::int32_t>(1));
        w.writeValue(static_cast<std::uint8_t>(1));
        w.writeValue(static_cast<std::uint32_t>(0x2000));
        w.writeValue(1.f);
        w.writeValue(static_cast<std::uint32_t>(0));
        w.writeValue(static_cast<std::uint32_t>(0));
        SaveReader reader(w.mBytes);
        EXPECT_FALSE(mChunk.read(reader, 1));
        EXPECT_EQ(mCoatings.size(), 0u);
    }
}
