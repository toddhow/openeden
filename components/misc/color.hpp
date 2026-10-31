#ifndef COMPONENTS_MISC_COLOR
#define COMPONENTS_MISC_COLOR

#include <string>

#include <vsg/maths/vec4.h>

namespace Misc
{
    class Color
    {
        explicit Color(vsg::vec4&& value)
            : mValue(value)
        {
        }

    public:
        Color() = default;
        Color(float r, float g, float b, float a);

        float r() const { return mValue.r; }
        float g() const { return mValue.g; }
        float b() const { return mValue.b; }
        float a() const { return mValue.a; }

        std::string toString() const;

        static Color fromHex(std::string_view hex);
        static Color fromRGB(unsigned int value);
        static Color fromVec(vsg::vec4 value);

        std::string toHex() const;
        unsigned int toRGBA() const;
        vsg::vec4 toVec() const { return mValue; }

        friend bool operator==(const Color& l, const Color& r);

    private:
        vsg::vec4 mValue;
    };
}

#endif // !COMPONENTS_MISC_COLOR
