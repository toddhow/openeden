// Task 0086, textures in the cell viewer: the mean colour of a diffuse texture, the cache with its budgets, and the
// pass that gives shapes their texture colour. Generated .dds files only; no game data.
#include <apps/opencs/model/mesh/texturecolour.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <map>
#include <random>

namespace
{
    using namespace CSMMesh;
    using Bytes = std::vector<unsigned char>;

    void put32(Bytes& b, std::size_t at, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            b[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>(v >> (8 * i));
    }

    // A DDS header (128 bytes), optionally with the DX10 extension (20 bytes), then `payload`.
    Bytes dds(std::uint32_t width, std::uint32_t height, std::uint32_t mips, const char* fourCC, std::uint32_t dxgi,
        const Bytes& payload, std::uint32_t rgbBits = 0, std::uint32_t rMask = 0, std::uint32_t gMask = 0,
        std::uint32_t bMask = 0, std::uint32_t aMask = 0)
    {
        const bool dx10 = dxgi != 0;
        Bytes b(128 + (dx10 ? 20 : 0), 0);
        std::memcpy(b.data(), "DDS ", 4);
        put32(b, 4, 124);
        put32(b, 8, 0x1007 | (mips > 1 ? 0x20000u : 0));
        put32(b, 12, height);
        put32(b, 16, width);
        put32(b, 28, mips);
        put32(b, 76, 32);
        if (fourCC != nullptr)
        {
            put32(b, 80, 0x4);
            std::memcpy(b.data() + 84, dx10 ? "DX10" : fourCC, 4);
        }
        else
        {
            put32(b, 80, aMask != 0 ? 0x41u : 0x40u);
            put32(b, 88, rgbBits);
            put32(b, 92, rMask);
            put32(b, 96, gMask);
            put32(b, 100, bMask);
            put32(b, 104, aMask);
        }
        put32(b, 108, 0x1000);
        if (dx10)
        {
            put32(b, 128, dxgi);
            put32(b, 132, 3); // texture2D
            put32(b, 140, 1); // array size
        }
        b.insert(b.end(), payload.begin(), payload.end());
        return b;
    }

    // A BC1 block of one RGB565 colour (both endpoints equal, every index 0).
    Bytes bc1Block(unsigned r, unsigned g, unsigned b)
    {
        const unsigned c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        return { static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8), static_cast<unsigned char>(c),
            static_cast<unsigned char>(c >> 8), 0, 0, 0, 0 };
    }

    Bytes repeat(const Bytes& block, std::size_t count)
    {
        Bytes out;
        for (std::size_t i = 0; i < count; ++i)
            out.insert(out.end(), block.begin(), block.end());
        return out;
    }

    void expectColour(const std::optional<std::array<float, 3>>& c, float r, float g, float b, float tolerance = 0.02f)
    {
        ASSERT_TRUE(c.has_value());
        EXPECT_NEAR((*c)[0], r, tolerance);
        EXPECT_NEAR((*c)[1], g, tolerance);
        EXPECT_NEAR((*c)[2], b, tolerance);
    }

    TEST(TextureColourDds, aSolidBc1TextureHasItsColour)
    {
        expectColour(averageColourOfDds(dds(16, 16, 1, "DXT1", 0, repeat(bc1Block(255, 0, 0), 16))), 1.f, 0.f, 0.f);
        expectColour(
            averageColourOfDds(dds(8, 8, 1, "DXT1", 0, repeat(bc1Block(0, 128, 64), 4))), 0.f, 128 / 255.f, 64 / 255.f);
    }

    TEST(TextureColourDds, theDx10HeaderIsReadToo)
    {
        expectColour(averageColourOfDds(dds(8, 8, 1, "DX10", 71, repeat(bc1Block(0, 255, 0), 4))), 0.f, 1.f, 0.f);
        expectColour(averageColourOfDds(dds(8, 8, 1, "DX10", 72, repeat(bc1Block(0, 0, 255), 4))), 0.f, 0.f, 1.f);
    }

    TEST(TextureColourDds, bc3UsesTheColourHalfAndWeightsByAlpha)
    {
        // BC3 block: alpha endpoints 255, 255, indices 0 (all opaque), then a BC1 colour block.
        Bytes block = { 255, 255, 0, 0, 0, 0, 0, 0 };
        const Bytes colour = bc1Block(200, 100, 50);
        block.insert(block.end(), colour.begin(), colour.end());
        expectColour(averageColourOfDds(dds(4, 4, 1, "DXT5", 0, block)), 200 / 255.f, 100 / 255.f, 50 / 255.f, 0.04f);

        // Half of the texels fully transparent: they carry no weight, so the visible colour stays.
        Bytes half = { 255, 0, 0, 0, 0, 0, 0, 0 };
        // 3 index bits per texel: texels 0..7 keep index 0 (alpha 255), texels 8..15 get index 1 (alpha 0).
        std::uint64_t bits = 0;
        for (int t = 8; t < 16; ++t)
            bits |= static_cast<std::uint64_t>(1) << (3 * t);
        for (int k = 0; k < 6; ++k)
            half[2 + static_cast<std::size_t>(k)] = static_cast<unsigned char>(bits >> (8 * k));
        // The transparent half's colour differs: use a colour block with two endpoints, rows 2-3 index 3 (the far end).
        Bytes colour2 = { 0, 0xF8, 0x1F, 0x00, 0, 0, 0xFF, 0xFF }; // c0 red 565, c1 blue 565, texels 8..15 index 3
        half.insert(half.end(), colour2.begin(), colour2.end());
        const auto c = averageColourOfDds(dds(4, 4, 1, "DXT5", 0, half));
        ASSERT_TRUE(c.has_value());
        EXPECT_GT((*c)[0], 0.9f); // red: the opaque half
        EXPECT_LT((*c)[2], 0.15f);
    }

    TEST(TextureColourDds, bc4GivesGreyAndBc7DecodesWithoutDamage)
    {
        Bytes bc4 = { 200, 200, 0, 0, 0, 0, 0, 0 };
        expectColour(averageColourOfDds(dds(4, 4, 1, "ATI1", 0, bc4)), 200 / 255.f, 200 / 255.f, 200 / 255.f);
        // BC7 mode 6 block, endpoints all 127, index 0: a solid, opaque colour.
        Bytes bc7(16, 0);
        bc7[0] = 0x40; // mode 6: seventh bit set
        const auto c = averageColourOfDds(dds(4, 4, 1, "DX10", 98, bc7));
        ASSERT_TRUE(c.has_value());
        for (float v : *c)
            EXPECT_TRUE(v >= 0.f && v <= 1.f);
    }

    TEST(TextureColourDds, uncompressedBgraAndBgr)
    {
        // 2x2 BGRA: every texel (r 10, g 20, b 30, a 255).
        Bytes px;
        for (int i = 0; i < 4; ++i)
            for (int v : { 30, 20, 10, 255 })
                px.push_back(static_cast<unsigned char>(v));
        expectColour(averageColourOfDds(dds(2, 2, 1, nullptr, 0, px, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000)),
            10 / 255.f, 20 / 255.f, 30 / 255.f);
        Bytes px24;
        for (int i = 0; i < 4; ++i)
            for (int v : { 30, 20, 10 })
                px24.push_back(static_cast<unsigned char>(v));
        expectColour(averageColourOfDds(dds(2, 2, 1, nullptr, 0, px24, 24, 0xFF0000, 0xFF00, 0xFF, 0)), 10 / 255.f,
            20 / 255.f, 30 / 255.f);
    }

    TEST(TextureColourDds, aSmallMipLevelIsUsedWhenTheTopHasTooManyBlocks)
    {
        // 256x256 BC1 (4,096 blocks) with a chain: the top level is green, every smaller level red. The reader must
        // not touch the top level.
        Bytes payload = repeat(bc1Block(0, 255, 0), 64 * 64);
        std::uint32_t size = 128;
        std::uint32_t levels = 1;
        while (size >= 1)
        {
            const std::uint32_t blocks = std::max(1u, size / 4) * std::max(1u, size / 4);
            const Bytes level = repeat(bc1Block(255, 0, 0), blocks);
            payload.insert(payload.end(), level.begin(), level.end());
            ++levels;
            if (size == 1)
                break;
            size /= 2;
        }
        expectColour(averageColourOfDds(dds(256, 256, levels, "DXT1", 0, payload)), 1.f, 0.f, 0.f);
    }

    TEST(TextureColourDds, aFileWithoutMipsIsSampledNotRead)
    {
        // 2048x2048 BC1 with one level: 262,144 blocks; a solid colour is found from a spread of them.
        const auto file = dds(2048, 2048, 1, "DXT1", 0, repeat(bc1Block(0, 0, 255), 262144));
        expectColour(averageColourOfDds(file), 0.f, 0.f, 1.f);
    }

    TEST(TextureColourDds, damagedAndUnsupportedFilesGiveNothing)
    {
        const Bytes good = dds(16, 16, 1, "DXT1", 0, repeat(bc1Block(255, 0, 0), 16));
        ASSERT_TRUE(averageColourOfDds(good).has_value());
        EXPECT_FALSE(averageColourOfDds({}).has_value());
        EXPECT_FALSE(averageColourOfDds(std::span<const unsigned char>(good.data(), 100)).has_value());
        // Every truncation of the data is refused, none crashes.
        for (std::size_t n = 0; n < good.size(); ++n)
            EXPECT_FALSE(averageColourOfDds(std::span<const unsigned char>(good.data(), n)).has_value()) << n;
        Bytes bad = good;
        bad[0] = 'X';
        EXPECT_FALSE(averageColourOfDds(bad).has_value());
        for (std::uint32_t w : { 0u, 65536u, 0xFFFFFFFFu })
        {
            bad = good;
            put32(bad, 16, w);
            EXPECT_FALSE(averageColourOfDds(bad).has_value()) << w;
        }
        bad = good;
        put32(bad, 12, 0);
        EXPECT_FALSE(averageColourOfDds(bad).has_value());
        bad = good;
        put32(bad, 28, 0xFFFFFFFF); // an absurd mip count
        EXPECT_NO_THROW((void)averageColourOfDds(bad));
        bad = good;
        std::memcpy(bad.data() + 84, "ZZZZ", 4); // unknown fourCC
        EXPECT_FALSE(averageColourOfDds(bad).has_value());
        bad = good;
        put32(bad, 112, 0x200); // cube map
        EXPECT_FALSE(averageColourOfDds(bad).has_value());
        EXPECT_FALSE(averageColourOfDds(dds(8, 8, 1, "DX10", 95, repeat(Bytes(16, 0), 4))).has_value()); // BC6H
        EXPECT_FALSE(averageColourOfDds(dds(2, 2, 1, nullptr, 0, Bytes(8, 0), 16, 0xF800, 0x7E0, 0x1F, 0)).has_value());
    }

    TEST(TextureColourDds, randomDamageNeverCrashesAndStaysInRange)
    {
        const Bytes seeds[] = { dds(16, 16, 1, "DXT1", 0, repeat(bc1Block(255, 0, 0), 16)),
            dds(8, 8, 1, "DX10", 98, Bytes(64, 0x55)), dds(8, 8, 2, "DXT5", 0, Bytes(64 + 16, 0xA7)),
            dds(2, 2, 1, nullptr, 0, Bytes(16, 7), 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000) };
        std::mt19937 rng(12345);
        for (int i = 0; i < 20000; ++i)
        {
            Bytes b = seeds[rng() % 4];
            for (int k = 0; k < 1 + static_cast<int>(rng() % 4); ++k)
                b[rng() % b.size()] = static_cast<unsigned char>(rng());
            if (rng() % 5 == 0)
                b.resize(rng() % b.size());
            const auto c = averageColourOfDds(b);
            if (c)
            {
                for (float v : *c)
                {
                    ASSERT_TRUE(v >= 0.f && v <= 1.f);
                }
            }
        }
    }

    // ---- the cache --------------------------------------------------------------------------------------------

    struct Files
    {
        std::map<std::string, Bytes> files;
        std::vector<std::string> asked;
        TextureBytes provider()
        {
            return [this](const std::string& path, std::uint64_t) -> std::optional<Bytes> {
                asked.push_back(path);
                const auto it = files.find(path);
                if (it == files.end())
                    return std::nullopt;
                return it->second;
            };
        }
    };

    Bytes redTexture()
    {
        return dds(8, 8, 1, "DXT1", 0, repeat(bc1Block(255, 0, 0), 4));
    }

    TEST(TextureColourCache, normalisesPathsAndReadsEachTextureOnce)
    {
        EXPECT_EQ(normaliseTexturePath("Textures\\Architecture\\X_d.dds"), "textures/architecture/x_d.dds");
        EXPECT_EQ(normaliseTexturePath("architecture\\x_d.dds"), "textures/architecture/x_d.dds");
        Files f;
        f.files["textures/a/red.dds"] = redTexture();
        TextureColourCache cache(f.provider());
        expectColour(cache.colourOf("textures\\A\\Red.dds"), 1.f, 0.f, 0.f);
        expectColour(cache.colourOf("TEXTURES/a/red.dds"), 1.f, 0.f, 0.f);
        expectColour(cache.colourOf("a/red.dds"), 1.f, 0.f, 0.f);
        EXPECT_EQ(f.asked.size(), 1u);
        EXPECT_EQ(cache.stats().requested, 1u);
        EXPECT_EQ(cache.stats().loaded, 1u);
        EXPECT_EQ(cache.stats().bytesRead, redTexture().size());
    }

    TEST(TextureColourCache, missingAndUnsupportedAreRememberedAndCounted)
    {
        Files f;
        f.files["textures/junk.dds"] = Bytes(300, 1);
        TextureColourCache cache(f.provider());
        EXPECT_FALSE(cache.colourOf("textures/none.dds").has_value());
        EXPECT_FALSE(cache.colourOf("textures/none.dds").has_value());
        EXPECT_FALSE(cache.colourOf("textures/junk.dds").has_value());
        EXPECT_FALSE(cache.colourOf("").has_value());
        EXPECT_EQ(cache.stats().missing, 1u);
        EXPECT_EQ(cache.stats().unsupported, 1u);
        EXPECT_EQ(cache.stats().loaded, 0u);
        EXPECT_EQ(f.asked.size(), 2u);
    }

    TEST(TextureColourCache, theBudgetsStopReadingAndAreCounted)
    {
        Files f;
        for (int i = 0; i < 5; ++i)
            f.files["textures/t" + std::to_string(i) + ".dds"] = redTexture();
        TextureColourLimits limits;
        limits.maxTextures = 3;
        TextureColourCache count(f.provider(), limits);
        int got = 0;
        for (int i = 0; i < 5; ++i)
            got += count.colourOf("textures/t" + std::to_string(i) + ".dds").has_value() ? 1 : 0;
        EXPECT_EQ(got, 3);
        EXPECT_EQ(count.stats().overBudget, 2u);

        limits = TextureColourLimits();
        limits.maxFileBytes = redTexture().size() - 1;
        TextureColourCache small(f.provider(), limits);
        EXPECT_FALSE(small.colourOf("textures/t0.dds").has_value());
        EXPECT_EQ(small.stats().overBudget, 1u);

        limits = TextureColourLimits();
        limits.maxTotalBytes = redTexture().size() + 10;
        TextureColourCache total(f.provider(), limits);
        EXPECT_TRUE(total.colourOf("textures/t0.dds").has_value());
        EXPECT_FALSE(total.colourOf("textures/t1.dds").has_value());
        EXPECT_EQ(total.stats().overBudget, 1u);
        EXPECT_LE(total.stats().bytesRead, limits.maxTotalBytes);
    }

    SceneModel modelWith(const std::string& diffuse, bool tinted)
    {
        SceneModel m;
        SceneModelShape s;
        s.diffuse = diffuse;
        s.tinted = tinted;
        m.shapes.push_back(s);
        return m;
    }

    TEST(TextureColourApply, aTextureColoursAnUntintedShapeAndTheRestKeepTheirs)
    {
        Files f;
        f.files["textures/red.dds"] = redTexture();
        TextureColourCache cache(f.provider());
        SceneModel m = modelWith("textures\\red.dds", false);
        m.shapes.push_back(modelWith("textures\\red.dds", true).shapes[0]); // skin tint: keeps its colour
        m.shapes.push_back(modelWith("", false).shapes[0]); // no texture set
        m.shapes.push_back(modelWith("textures\\gone.dds", false).shapes[0]); // not found
        const auto before = m.shapes[1].colour;
        applyTextureColours(m, cache);
        EXPECT_TRUE(m.shapes[0].textured);
        EXPECT_NEAR(m.shapes[0].colour[0], 1.f, 0.02f);
        EXPECT_NEAR(m.shapes[0].colour[1], 0.f, 0.02f);
        EXPECT_FLOAT_EQ(m.shapes[0].colour[3], 1.f);
        EXPECT_FALSE(m.shapes[1].textured);
        EXPECT_EQ(m.shapes[1].colour, before);
        EXPECT_FALSE(m.shapes[2].textured);
        EXPECT_FALSE(m.shapes[3].textured);
        EXPECT_EQ(cache.stats().shapesTextured, 1u);
        EXPECT_EQ(cache.stats().shapesFallback, 1u); // the one with a path that could not be read
    }
}
