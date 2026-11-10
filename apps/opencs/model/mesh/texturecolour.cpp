#include "texturecolour.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <components/vsgutil/bc4.hpp>
#include <components/vsgutil/bc7decode.hpp>

namespace CSMMesh
{
    namespace
    {
        // Largest picture a diffuse texture may claim (Skyrim's biggest are 4096; 16384 is far past any real file).
        constexpr std::uint32_t kMaxDimension = 16384;
        constexpr std::uint32_t kMaxMipCount = 16;
        // One mip level is read at most this many blocks (or texels, for an uncompressed picture) at a time; a top
        // level with more is sampled evenly.
        constexpr std::uint64_t kMaxBlocks = 1024;
        constexpr std::uint64_t kMaxTexels = 4096;

        constexpr std::size_t kHeaderSize = 128;
        constexpr std::size_t kDx10Size = 20;

        std::uint32_t read32(const unsigned char* p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        enum class Codec
        {
            Bc1,
            Bc2,
            Bc3,
            Bc4,
            Bc5,
            Bc7,
            Raw,
        };

        struct Layout
        {
            Codec codec = Codec::Bc1;
            std::uint64_t blockBytes = 8;
            // Raw only: bytes a texel and where each channel lives.
            std::uint32_t texelBytes = 0;
            std::uint32_t shift[4] = {};
            bool hasAlpha = false;
            bool grey = false;
        };

        // The shift of an 8 bit mask (0xFF << s); false for any other mask.
        bool byteShift(std::uint32_t mask, std::uint32_t& shift)
        {
            for (std::uint32_t s = 0; s <= 24; s += 8)
                if (mask == (0xFFu << s))
                {
                    shift = s;
                    return true;
                }
            return false;
        }

        // The layout from the header, or false for anything unsupported.
        bool layoutOf(const unsigned char* header, bool dx10, std::uint32_t dxgi, Layout& out)
        {
            const std::uint32_t pfFlags = read32(header + 80);
            const unsigned char* four = header + 84;
            if (dx10)
            {
                switch (dxgi)
                {
                    case 70:
                    case 71:
                    case 72:
                        out.codec = Codec::Bc1;
                        out.blockBytes = 8;
                        return true;
                    case 73:
                    case 74:
                    case 75:
                        out.codec = Codec::Bc2;
                        out.blockBytes = 16;
                        return true;
                    case 76:
                    case 77:
                    case 78:
                        out.codec = Codec::Bc3;
                        out.blockBytes = 16;
                        return true;
                    case 79:
                    case 80:
                        out.codec = Codec::Bc4;
                        out.blockBytes = 8;
                        return true;
                    case 82:
                    case 83:
                        out.codec = Codec::Bc5;
                        out.blockBytes = 16;
                        return true;
                    case 97:
                    case 98:
                    case 99:
                        out.codec = Codec::Bc7;
                        out.blockBytes = 16;
                        return true;
                    case 28:
                    case 29: // R8G8B8A8
                        out.codec = Codec::Raw;
                        out.texelBytes = 4;
                        out.shift[0] = 0;
                        out.shift[1] = 8;
                        out.shift[2] = 16;
                        out.shift[3] = 24;
                        out.hasAlpha = true;
                        return true;
                    case 87:
                    case 91: // B8G8R8A8
                        out.codec = Codec::Raw;
                        out.texelBytes = 4;
                        out.shift[0] = 16;
                        out.shift[1] = 8;
                        out.shift[2] = 0;
                        out.shift[3] = 24;
                        out.hasAlpha = true;
                        return true;
                    case 88:
                    case 93: // B8G8R8X8
                        out.codec = Codec::Raw;
                        out.texelBytes = 4;
                        out.shift[0] = 16;
                        out.shift[1] = 8;
                        out.shift[2] = 0;
                        return true;
                    default:
                        return false;
                }
            }
            if ((pfFlags & 0x4) != 0) // FOURCC
            {
                auto is = [&](const char* name) { return std::memcmp(four, name, 4) == 0; };
                if (is("DXT1"))
                {
                    out.codec = Codec::Bc1;
                    out.blockBytes = 8;
                    return true;
                }
                if (is("DXT2") || is("DXT3"))
                {
                    out.codec = Codec::Bc2;
                    out.blockBytes = 16;
                    return true;
                }
                if (is("DXT4") || is("DXT5"))
                {
                    out.codec = Codec::Bc3;
                    out.blockBytes = 16;
                    return true;
                }
                if (is("ATI1") || is("BC4U"))
                {
                    out.codec = Codec::Bc4;
                    out.blockBytes = 8;
                    return true;
                }
                if (is("ATI2") || is("BC5U"))
                {
                    out.codec = Codec::Bc5;
                    out.blockBytes = 16;
                    return true;
                }
                return false;
            }
            if ((pfFlags & 0x40) != 0) // RGB
            {
                const std::uint32_t bits = read32(header + 88);
                if (bits != 24 && bits != 32)
                    return false;
                std::uint32_t masks[4]
                    = { read32(header + 92), read32(header + 96), read32(header + 100), read32(header + 104) };
                for (int c = 0; c < 3; ++c)
                    if (!byteShift(masks[c], out.shift[c]))
                        return false;
                out.codec = Codec::Raw;
                out.texelBytes = bits / 8;
                if (bits == 32 && (pfFlags & 0x1) != 0 && masks[3] != 0 && byteShift(masks[3], out.shift[3]))
                    out.hasAlpha = true;
                for (int c = 0; c < 3; ++c)
                    if (out.shift[c] >= bits)
                        return false;
                return true;
            }
            return false;
        }

        struct Accumulator
        {
            double sum[3] = {};
            double weight = 0;
            double plain[3] = {};
            std::uint64_t texels = 0;

            void add(float r, float g, float b, float alpha)
            {
                const double w = std::max(alpha, 1.f / 255.f);
                sum[0] += r * w;
                sum[1] += g * w;
                sum[2] += b * w;
                weight += w;
                plain[0] += r;
                plain[1] += g;
                plain[2] += b;
                ++texels;
            }
        };

        // 5:6:5 to 8 bit per channel, the usual expansion.
        void expand565(unsigned c, unsigned (&rgb)[3])
        {
            const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
            rgb[0] = (r << 3) | (r >> 2);
            rgb[1] = (g << 2) | (g >> 4);
            rgb[2] = (b << 3) | (b >> 2);
        }

        // The colour half of a BC1/2/3 block. `fourColour` forces the four colour mode (BC2/BC3); otherwise c0 <= c1
        // gives three colours and index 3 is transparent. Writes 16 texels of RGB and a validity flag (false:
        // transparent).
        void decodeBc1(const unsigned char* block, bool fourColour, unsigned (&rgb)[16][3], bool (&opaque)[16])
        {
            const unsigned c0 = block[0] | (block[1] << 8), c1 = block[2] | (block[3] << 8);
            unsigned p[4][3];
            expand565(c0, p[0]);
            expand565(c1, p[1]);
            const bool four = fourColour || c0 > c1;
            for (int k = 0; k < 3; ++k)
            {
                if (four)
                {
                    p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
                    p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
                }
                else
                {
                    p[2][k] = (p[0][k] + p[1][k]) / 2;
                    p[3][k] = 0;
                }
            }
            const std::uint32_t indices = read32(block + 4);
            for (int t = 0; t < 16; ++t)
            {
                const unsigned i = (indices >> (2 * t)) & 3;
                for (int k = 0; k < 3; ++k)
                    rgb[t][k] = p[i][k];
                opaque[t] = four || i != 3;
            }
        }

        void addBlock(Accumulator& acc, Codec codec, const unsigned char* block)
        {
            unsigned rgb[16][3] = {};
            bool opaque[16];
            for (bool& o : opaque)
                o = true;
            float alpha[16];
            for (float& a : alpha)
                a = 1.f;
            switch (codec)
            {
                case Codec::Bc1:
                    decodeBc1(block, false, rgb, opaque);
                    break;
                case Codec::Bc2:
                    decodeBc1(block + 8, true, rgb, opaque);
                    for (int t = 0; t < 16; ++t)
                    {
                        const unsigned nibble = (block[t / 2] >> (4 * (t % 2))) & 15;
                        alpha[t] = static_cast<float>(nibble) / 15.f;
                    }
                    break;
                case Codec::Bc3:
                {
                    decodeBc1(block + 8, true, rgb, opaque);
                    std::uint8_t a[16];
                    vsgUtil::Bc4::decodeBlock(block, a);
                    for (int t = 0; t < 16; ++t)
                        alpha[t] = static_cast<float>(a[t]) / 255.f;
                    break;
                }
                case Codec::Bc4:
                {
                    std::uint8_t v[16];
                    vsgUtil::Bc4::decodeBlock(block, v);
                    for (int t = 0; t < 16; ++t)
                        rgb[t][0] = rgb[t][1] = rgb[t][2] = v[t];
                    break;
                }
                case Codec::Bc5:
                {
                    // Two channels (a normal map's x and y, or a pair of masks): shown as red and green, blue 0.
                    std::uint8_t r[16], g[16];
                    vsgUtil::Bc4::decodeBlock(block, r);
                    vsgUtil::Bc4::decodeBlock(block + 8, g);
                    for (int t = 0; t < 16; ++t)
                    {
                        rgb[t][0] = r[t];
                        rgb[t][1] = g[t];
                    }
                    break;
                }
                case Codec::Bc7:
                {
                    std::uint8_t px[64];
                    vsgUtil::Bc7::decodeBlock(block, px);
                    for (int t = 0; t < 16; ++t)
                    {
                        rgb[t][0] = px[4 * t];
                        rgb[t][1] = px[4 * t + 1];
                        rgb[t][2] = px[4 * t + 2];
                        alpha[t] = static_cast<float>(px[4 * t + 3]) / 255.f;
                    }
                    break;
                }
                case Codec::Raw:
                    return;
            }
            for (int t = 0; t < 16; ++t)
                if (opaque[t])
                    acc.add(rgb[t][0] / 255.f, rgb[t][1] / 255.f, rgb[t][2] / 255.f, alpha[t]);
        }
    }

    std::optional<std::array<float, 3>> averageColourOfDds(std::span<const unsigned char> file)
    {
        if (file.size() < kHeaderSize || std::memcmp(file.data(), "DDS ", 4) != 0)
            return std::nullopt;
        const unsigned char* h = file.data();
        if (read32(h + 4) != 124 || read32(h + 76) != 32)
            return std::nullopt;
        const std::uint32_t flags = read32(h + 8), height = read32(h + 12), width = read32(h + 16);
        const std::uint32_t depth = read32(h + 24);
        std::uint32_t mips = read32(h + 28);
        const std::uint32_t caps2 = read32(h + 112);
        if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension)
            return std::nullopt;
        if ((caps2 & 0x200) != 0 || (caps2 & 0x200000) != 0 || ((flags & 0x800000) != 0 && depth > 1))
            return std::nullopt; // cube map or volume
        if ((flags & 0x20000) == 0 || mips == 0)
            mips = 1;
        mips = std::min(mips, kMaxMipCount);

        const bool dx10 = std::memcmp(h + 84, "DX10", 4) == 0 && (read32(h + 80) & 0x4) != 0;
        std::size_t offset = kHeaderSize;
        std::uint32_t dxgi = 0;
        if (dx10)
        {
            if (file.size() < kHeaderSize + kDx10Size)
                return std::nullopt;
            dxgi = read32(h + kHeaderSize);
            if (read32(h + kHeaderSize + 4) != 3 || read32(h + kHeaderSize + 12) > 1) // not a 2D single texture
                return std::nullopt;
            offset += kDx10Size;
        }
        Layout layout;
        if (!layoutOf(h, dx10, dxgi, layout))
            return std::nullopt;
        const bool raw = layout.codec == Codec::Raw;

        // Walk the chain to find the level to read: the first with few enough blocks (texels), else the last one.
        struct Level
        {
            std::uint64_t offset = 0, size = 0, blocksX = 0, blocksY = 0;
        };
        Level chosen;
        bool found = false;
        std::uint64_t at = offset;
        std::uint32_t w = width, hh = height;
        for (std::uint32_t level = 0; level < mips; ++level)
        {
            Level l;
            l.offset = at;
            if (raw)
            {
                l.blocksX = w;
                l.blocksY = hh;
                l.size = static_cast<std::uint64_t>(w) * hh * layout.texelBytes;
            }
            else
            {
                l.blocksX = std::max<std::uint64_t>(1, (w + 3) / 4);
                l.blocksY = std::max<std::uint64_t>(1, (hh + 3) / 4);
                l.size = l.blocksX * l.blocksY * layout.blockBytes;
            }
            if (l.offset + l.size > file.size())
            {
                if (level == 0)
                    return std::nullopt; // the top level itself is cut off
                break; // a later level is missing: use the one before
            }
            chosen = l;
            found = true;
            const std::uint64_t count = l.blocksX * l.blocksY;
            if (count <= (raw ? kMaxTexels : kMaxBlocks))
                break;
            at += l.size;
            w = std::max(1u, w / 2);
            hh = std::max(1u, hh / 2);
        }
        if (!found)
            return std::nullopt;

        Accumulator acc;
        const std::uint64_t count = chosen.blocksX * chosen.blocksY;
        const std::uint64_t limit = raw ? kMaxTexels : kMaxBlocks;
        const std::uint64_t step = count > limit ? (count + limit - 1) / limit : 1;
        for (std::uint64_t i = 0; i < count; i += step)
        {
            const unsigned char* p = file.data() + chosen.offset + i * (raw ? layout.texelBytes : layout.blockBytes);
            if (raw)
            {
                std::uint32_t texel = 0;
                for (std::uint32_t b = 0; b < layout.texelBytes; ++b)
                    texel |= static_cast<std::uint32_t>(p[b]) << (8 * b);
                const float a = layout.hasAlpha ? static_cast<float>((texel >> layout.shift[3]) & 0xFF) / 255.f : 1.f;
                acc.add(static_cast<float>((texel >> layout.shift[0]) & 0xFF) / 255.f,
                    static_cast<float>((texel >> layout.shift[1]) & 0xFF) / 255.f,
                    static_cast<float>((texel >> layout.shift[2]) & 0xFF) / 255.f, a);
            }
            else
                addBlock(acc, layout.codec, p);
        }
        if (acc.texels == 0)
            return std::nullopt;
        std::array<float, 3> out{};
        for (int c = 0; c < 3; ++c)
        {
            const double v = acc.weight > 0 ? acc.sum[c] / acc.weight : acc.plain[c] / static_cast<double>(acc.texels);
            out[static_cast<std::size_t>(c)] = static_cast<float>(std::clamp(v, 0.0, 1.0));
        }
        return out;
    }

    std::string normaliseTexturePath(const std::string& path)
    {
        std::string key = path;
        for (char& c : key)
        {
            if (c == '\\')
                c = '/';
            else
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        while (!key.empty() && key.front() == '/')
            key.erase(key.begin());
        if (key.rfind("textures/", 0) != 0)
            key = "textures/" + key;
        return key;
    }

    TextureColourCache::TextureColourCache(TextureBytes bytes, TextureColourLimits limits)
        : mBytes(std::move(bytes))
        , mLimits(limits)
    {
    }

    std::optional<std::array<float, 3>> TextureColourCache::colourOf(const std::string& path)
    {
        if (path.empty())
            return std::nullopt;
        const std::string key = normaliseTexturePath(path);
        const auto known = mColours.find(key);
        if (known != mColours.end())
            return known->second;

        ++mStats.requested;
        std::optional<std::array<float, 3>> colour;
        if (mColours.size() >= mLimits.maxTextures || mStats.bytesRead >= mLimits.maxTotalBytes || !mBytes)
        {
            ++mStats.overBudget;
            return mColours.emplace(key, colour).first->second;
        }
        const std::uint64_t allowed = std::min(mLimits.maxFileBytes, mLimits.maxTotalBytes - mStats.bytesRead);
        std::optional<std::vector<unsigned char>> bytes;
        try
        {
            bytes = mBytes(key, allowed);
        }
        catch (const std::exception&)
        {
            bytes.reset();
        }
        if (!bytes)
            ++mStats.missing;
        else if (bytes->size() > allowed)
            ++mStats.overBudget;
        else
        {
            mStats.bytesRead += bytes->size();
            colour = averageColourOfDds(*bytes);
            if (colour)
                ++mStats.loaded;
            else
                ++mStats.unsupported;
        }
        return mColours.emplace(key, colour).first->second;
    }

    void applyTextureColours(SceneModel& model, TextureColourCache& cache)
    {
        for (SceneModelShape& shape : model.shapes)
        {
            if (shape.diffuse.empty() || shape.tinted)
                continue;
            const auto colour = cache.colourOf(shape.diffuse);
            if (!colour)
            {
                ++cache.mutableStats().shapesFallback;
                continue;
            }
            shape.colour = { (*colour)[0], (*colour)[1], (*colour)[2], 1.f };
            shape.textured = true;
            ++cache.mutableStats().shapesTextured;
        }
    }
}
