#include "color.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <sstream>
#include <system_error>

namespace Misc
{
    Color::Color(float r, float g, float b, float a)
        : mValue(std::clamp(r, 0.f, 1.f), std::clamp(g, 0.f, 1.f), std::clamp(b, 0.f, 1.f), std::clamp(a, 0.f, 1.f))
    {
    }

    std::string Color::toString() const
    {
        std::ostringstream ss;
        ss << "(" << r() << ", " << g() << ", " << b() << ", " << a() << ')';
        return ss.str();
    }

    Color Color::fromHex(std::string_view hex)
    {
        if (hex.size() != 6)
            throw std::logic_error(std::string("Invalid hex color: ") += hex);
        Color col;
        col.mValue.a = 1;
        for (unsigned i = 0; i < 3; i++)
        {
            auto sub = hex.substr(i * 2, 2);
            unsigned char v = 0;
            auto [_, ec] = std::from_chars(sub.data(), sub.data() + sub.size(), v, 16);
            if (ec != std::errc())
                throw std::logic_error(std::string("Invalid hex color: ") += hex);
            col.mValue[i] = v / 255.0f;
        }
        return col;
    }

    Color Color::fromRGB(unsigned int value)
    {
        // openeden-vsg: SceneUtil::colourFromRGB is not built with VSG; same unpacking, inlined.
        return Color(
            ((value >> 0) & 0xFF) / 255.0f, ((value >> 8) & 0xFF) / 255.0f, ((value >> 16) & 0xFF) / 255.0f, 1.f);
    }

    Color Color::fromVec(vsg::vec4 value)
    {
        return Color(std::move(value));
    }

    unsigned int Color::toRGBA() const
    {
        const auto clampedByte
            = [](float value) { return static_cast<unsigned int>(std::clamp(value * 255.0f, 0.0f, 255.0f)); };
        return clampedByte(mValue[3]) << 24 | clampedByte(mValue[2]) << 16 | clampedByte(mValue[1]) << 8
            | clampedByte(mValue[0]);
    }

    std::string Color::toHex() const
    {
        std::string result(6, '0');
        for (unsigned i = 0; i < 3; i++)
        {
            int b = static_cast<int>(mValue[i] * 255.0f);
            char* start = result.data() + i * 2;
            if (b < 16)
                start++;
            auto [_, ec] = std::to_chars(start, result.data() + (i + 1) * 2, b, 16);
            if (ec != std::errc())
                throw std::logic_error("Error when converting number to base 16");
        }
        return result;
    }

    bool operator==(const Color& l, const Color& r)
    {
        return l.mValue == r.mValue;
    }
}
