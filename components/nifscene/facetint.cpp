#include "facetint.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace NifScene::FaceTint
{
    namespace
    {
        bool consistent(const Image& image)
        {
            return image.mWidth != 0 && image.mHeight != 0
                && image.mRgba.size() == static_cast<std::size_t>(image.mWidth) * image.mHeight * 4;
        }

        // One channel of the tint at normalised coordinates (u, v in 0..1 over the whole image), bilinear with the
        // texel centres at half steps and the edges clamped.
        float sampleChannel(const Image& tint, float u, float v, std::size_t channel)
        {
            const float x = u * static_cast<float>(tint.mWidth) - 0.5f;
            const float y = v * static_cast<float>(tint.mHeight) - 0.5f;
            const float fx = std::floor(x);
            const float fy = std::floor(y);
            const float tx = x - fx;
            const float ty = y - fy;
            auto at = [&](float px, float py) {
                const auto ix = static_cast<std::uint32_t>(std::clamp(px, 0.f, static_cast<float>(tint.mWidth - 1)));
                const auto iy = static_cast<std::uint32_t>(std::clamp(py, 0.f, static_cast<float>(tint.mHeight - 1)));
                return static_cast<float>(tint.mRgba[(static_cast<std::size_t>(iy) * tint.mWidth + ix) * 4 + channel]);
            };
            const float top = at(fx, fy) * (1.f - tx) + at(fx + 1.f, fy) * tx;
            const float bottom = at(fx, fy + 1.f) * (1.f - tx) + at(fx + 1.f, fy + 1.f) * tx;
            return top * (1.f - ty) + bottom * ty;
        }
    }

    bool composite(const Image& base, const Image& tint, Image& out, std::string& error)
    {
        out = {};
        if (!consistent(base))
        {
            error = "the base image is empty or its byte count does not match its size";
            return false;
        }
        if (!consistent(tint))
        {
            error = "the tint image is empty or its byte count does not match its size";
            return false;
        }
        out.mWidth = base.mWidth;
        out.mHeight = base.mHeight;
        out.mRgba.resize(base.mRgba.size());
        const bool sameSize = base.mWidth == tint.mWidth && base.mHeight == tint.mHeight;
        for (std::uint32_t y = 0; y < base.mHeight; ++y)
            for (std::uint32_t x = 0; x < base.mWidth; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * base.mWidth + x) * 4;
                for (std::size_t c = 0; c < 3; ++c)
                {
                    float t;
                    if (sameSize)
                        t = static_cast<float>(tint.mRgba[i + c]);
                    else
                        t = sampleChannel(tint, (static_cast<float>(x) + 0.5f) / static_cast<float>(base.mWidth),
                            (static_cast<float>(y) + 0.5f) / static_cast<float>(base.mHeight), c);
                    const float value = static_cast<float>(base.mRgba[i + c]) * t * kTintGain / 255.f;
                    out.mRgba[i + c] = static_cast<std::uint8_t>(std::clamp(value + 0.5f, 0.f, 255.f));
                }
                out.mRgba[i + 3] = base.mRgba[i + 3];
            }
        error.clear();
        return true;
    }

    std::string normaliseTexturePath(const std::string& stored)
    {
        std::string path;
        path.reserve(stored.size() + 9);
        for (char c : stored)
        {
            if (c == '\\')
                c = '/';
            else if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            path.push_back(c);
        }
        if (path.empty())
            return path;
        // Task skyext: 60 vanilla Skyrim SE models name textures under the build machine's folder
        // "skyrimhd/build/pc/data/", which no archive holds; the file is under the plain "textures/..." (119 of the 120
        // distinct paths exist there, the 120th is in no archive). The marker needs its whole folder chain at the
        // start of a path component.
        static constexpr std::string_view kBuildFolder = "skyrimhd/build/pc/data/";
        for (std::size_t at = path.find(kBuildFolder); at != std::string::npos; at = path.find(kBuildFolder, at + 1))
            if (at == 0 || path[at - 1] == '/')
            {
                path.erase(0, at + kBuildFolder.size());
                break;
            }
        if (path.rfind("data/", 0) == 0)
            path.erase(0, 5);
        if (path.rfind("textures/", 0) != 0)
            path.insert(0, "textures/");
        return path;
    }
}
