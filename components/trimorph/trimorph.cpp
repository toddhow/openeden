#include "trimorph.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace TriMorph
{
    namespace
    {
        constexpr std::size_t kHeaderBytes = 64; // magic, 12 i32, 8 bytes not read
        constexpr std::size_t kCountsAt = 8;

        std::uint32_t readLe32(std::string_view bytes, std::size_t at)
        {
            std::uint32_t v = 0;
            for (std::size_t i = 0; i < 4; ++i)
                v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
            return v;
        }

        std::int16_t readLe16(std::string_view bytes, std::size_t at)
        {
            const auto v = static_cast<std::uint16_t>(
                static_cast<unsigned char>(bytes[at]) | (static_cast<unsigned char>(bytes[at + 1]) << 8));
            return static_cast<std::int16_t>(v);
        }

        Result fail(std::string_view name, std::uint64_t offset, const std::string& what)
        {
            Result r;
            r.mError = (name.empty() ? std::string("tri") : std::string(name)) + ": " + what + " (offset "
                + std::to_string(offset) + ")";
            return r;
        }

        bool equalNoCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }
    }

    Result read(
        std::string_view bytes, std::string_view name, std::span<const std::string_view> wanted, const Limits& limits)
    {
        if (bytes.size() > limits.mMaxBytes)
            return fail(name, 0,
                "the file is " + std::to_string(bytes.size()) + " bytes, over the limit of "
                    + std::to_string(limits.mMaxBytes));
        if (bytes.size() < kHeaderBytes)
            return fail(name, 0, "shorter than the 64 byte header");
        if (bytes.substr(0, 8) != "FRTRI003")
            return fail(name, 0, "the magic is not FRTRI003");

        std::uint32_t counts[12];
        for (std::size_t i = 0; i < 12; ++i)
            counts[i] = readLe32(bytes, kCountsAt + 4 * i);
        const std::uint32_t vertices = counts[0];
        const std::uint32_t triangles = counts[1];
        const std::uint32_t quads = counts[2];
        const std::uint32_t labelVerts = counts[3];
        const std::uint32_t surfLabelVerts = counts[4];
        const std::uint32_t texCoords = counts[5];
        const std::uint32_t flags = counts[6];
        const std::uint32_t morphs = counts[7];
        const std::uint32_t staticMorphs = counts[8];
        const std::uint32_t modifiers = counts[9];
        const std::uint32_t modifierVerts = counts[10];

        if (vertices > limits.mMaxVertices)
            return fail(name, kCountsAt, std::to_string(vertices) + " vertices, over the limit");
        if (triangles > limits.mMaxTriangles)
            return fail(name, kCountsAt + 4, std::to_string(triangles) + " triangles, over the limit");
        if (morphs > limits.mMaxMorphsInFile)
            return fail(name, kCountsAt + 28, std::to_string(morphs) + " morphs, over the limit");
        if (quads != 0 || labelVerts != 0 || surfLabelVerts != 0 || staticMorphs != 0 || modifiers != 0
            || modifierVerts != 0)
            return fail(name, kCountsAt,
                "quads, label vertices, static morphs or modifiers are present; their layout has not been seen in any "
                "file and is not read");
        if ((flags & 1) != 0 && texCoords > limits.mMaxVertices)
            return fail(name, kCountsAt + 20, std::to_string(texCoords) + " texture coordinates, over the limit");

        // All sizes below are far under 2^63 after the caps above, so no overflow.
        std::uint64_t pos = kHeaderBytes;
        pos += static_cast<std::uint64_t>(vertices) * 12;
        pos += static_cast<std::uint64_t>(triangles) * 12;
        if ((flags & 1) != 0)
        {
            pos += static_cast<std::uint64_t>(texCoords) * 8;
            pos += static_cast<std::uint64_t>(triangles) * 12;
        }
        if (pos > bytes.size())
            return fail(name, bytes.size(), "ends inside the vertices, triangles or texture coordinates");

        Result r;
        r.mFile.mVertices = vertices;
        r.mFile.mMorphsInFile = morphs;
        const std::uint64_t deltaBytes = static_cast<std::uint64_t>(vertices) * 6;
        for (std::uint32_t m = 0; m < morphs; ++m)
        {
            if (pos + 4 > bytes.size())
                return fail(name, pos, "ends before the name length of morph " + std::to_string(m));
            const std::uint32_t nameLength = readLe32(bytes, static_cast<std::size_t>(pos));
            if (nameLength > limits.mMaxNameLength)
                return fail(name, pos, "the name of morph " + std::to_string(m) + " is too long");
            pos += 4;
            if (pos + nameLength + 4 + deltaBytes > bytes.size())
                return fail(name, pos, "ends inside morph " + std::to_string(m));
            std::string_view morphName = bytes.substr(static_cast<std::size_t>(pos), nameLength);
            while (!morphName.empty() && morphName.back() == '\0')
                morphName.remove_suffix(1);
            pos += nameLength;
            float scale;
            const std::uint32_t scaleBits = readLe32(bytes, static_cast<std::size_t>(pos));
            std::memcpy(&scale, &scaleBits, 4);
            if (!std::isfinite(scale))
                return fail(name, pos, "the scale of morph " + std::to_string(m) + " is not a finite number");
            pos += 4;

            const bool keep = wanted.empty() || std::any_of(wanted.begin(), wanted.end(), [&](std::string_view w) {
                return equalNoCase(w, morphName);
            });
            if (keep)
            {
                Morph morph;
                morph.mName = std::string(morphName);
                morph.mScale = scale;
                morph.mDeltas.resize(static_cast<std::size_t>(vertices) * 3);
                for (std::size_t i = 0; i < morph.mDeltas.size(); ++i)
                    morph.mDeltas[i] = readLe16(bytes, static_cast<std::size_t>(pos) + 2 * i);
                r.mFile.mMorphs.push_back(std::move(morph));
            }
            pos += deltaBytes;
        }
        r.mFile.mTrailingBytes = bytes.size() - pos;
        r.mOk = true;
        return r;
    }

    bool apply(const File& file, std::span<const std::array<float, 3>> base, std::span<const Term> terms,
        std::span<std::array<float, 3>> out)
    {
        const std::size_t n = file.mVertices;
        if (base.size() != n || out.size() != n)
            return false;
        for (const Term& t : terms)
            if (t.mMorph >= file.mMorphs.size() || file.mMorphs[t.mMorph].mDeltas.size() != n * 3)
                return false;
        for (std::size_t i = 0; i < n; ++i)
        {
            float x = base[i][0];
            float y = base[i][1];
            float z = base[i][2];
            for (const Term& t : terms)
            {
                if (!std::isfinite(t.mWeight))
                    continue;
                const Morph& m = file.mMorphs[t.mMorph];
                const float f = t.mWeight * m.mScale;
                x += f * static_cast<float>(m.mDeltas[3 * i]);
                y += f * static_cast<float>(m.mDeltas[3 * i + 1]);
                z += f * static_cast<float>(m.mDeltas[3 * i + 2]);
            }
            out[i] = { x, y, z };
        }
        return true;
    }
}
