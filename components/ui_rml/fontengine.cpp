#include "fontengine.hpp"

#include <algorithm>
#include <cctype>

#include <stdexcept>

#include <RmlUi/Core.h>

namespace UiRml
{
    namespace
    {
        std::string lowerCase(std::string text)
        {
            std::transform(
                text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        bool endsWithFnt(const std::string& name)
        {
            return name.size() > 4 && lowerCase(name.substr(name.size() - 4)) == ".fnt";
        }
    }

    // One bitmap font family: the parsed files and the two ways its sheet is turned into a texture.
    struct BitmapFontEngine::Font
    {
        FontFile mFile;
        std::unique_ptr<GlyphMap> mMap;
        FontTexture mTexture;
        // The sheet as a texture RmlUi makes on first use, with linear or nearest sampling.
        Rml::CallbackTextureSource mLinear;
        Rml::CallbackTextureSource mNearest;
    };

    // One font at one requested pixel size.
    struct BitmapFontEngine::Face
    {
        const Font* mFont = nullptr;
        SizeChoice mChoice;
        Rml::FontMetrics mMetrics{};
    };

    BitmapFontEngine::BitmapFontEngine(
        Rml::FontEngineInterface* fallback, ReadFn read, ToUTF8::FromType gameEncoding, TextureFilterHint* hint)
        : mFallback(fallback)
        , mRead(std::move(read))
        , mGameEncoding(gameEncoding)
        , mHint(hint)
    {
    }

    BitmapFontEngine::~BitmapFontEngine() = default;

    bool BitmapFontEngine::hasBitmapFamily(const std::string& family) const
    {
        return mFonts.contains(lowerCase(family));
    }

    bool BitmapFontEngine::addBitmapFont(const std::string& family, const std::string& fntPath)
    {
        try
        {
            std::vector<char> bytes;
            if (!mRead(fntPath, bytes))
                throw std::runtime_error("the font file does not exist");
            auto font = std::make_unique<Font>();
            font->mFile = parseFontFile(bytes, fntPath);
            const std::string texturePath = "fonts/" + font->mFile.mTextureName + ".tex";
            std::vector<char> textureBytes;
            if (!mRead(texturePath, textureBytes))
                throw std::runtime_error("the font bitmap " + texturePath + " does not exist");
            font->mTexture = parseFontTexture(textureBytes, texturePath);
            if (font->mTexture.mTruncated)
                Rml::Log::Message(
                    Rml::Log::LT_WARNING, "Font bitmap %s ended prematurely, using partial data", texturePath.c_str());
            premultiply(font->mTexture);
            // Whether two glyphs may claim one code point and which wins is pinned against the MyGUI loader by
            // apps/openmw_tests/ui_rml/testbitmapfontmygui.cpp (task 0104).
            font->mMap = std::make_unique<GlyphMap>(mGameEncoding, DuplicatePolicy::FirstWins);

            Font* raw = font.get();
            const auto upload = [this, raw](bool nearest) {
                return [this, raw, nearest](const Rml::CallbackTextureInterface& textures) {
                    if (mHint != nullptr)
                        mHint->setNextTextureNearest(nearest);
                    const bool ok = textures.GenerateTexture(
                        Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(raw->mTexture.mPixels.data()),
                            raw->mTexture.mPixels.size()),
                        { raw->mTexture.mWidth, raw->mTexture.mHeight });
                    if (mHint != nullptr)
                        mHint->setNextTextureNearest(false);
                    return ok;
                };
            };
            font->mLinear = Rml::CallbackTextureSource(upload(false));
            font->mNearest = Rml::CallbackTextureSource(upload(true));
            mFonts[lowerCase(family)] = std::move(font);
            return true;
        }
        catch (const std::exception& e)
        {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Bitmap font %s (%s): %s", family.c_str(), fntPath.c_str(), e.what());
            return false;
        }
    }

    void BitmapFontEngine::Shutdown()
    {
        ReleaseFontResources();
        mFonts.clear();
        mFallback->Shutdown();
        mFontBlobs.clear(); // only now: the default engine reads memory fonts until it has shut down
    }

    void BitmapFontEngine::ReleaseFontResources()
    {
        mFallback->ReleaseFontResources();
        mFaces.clear();
        mOurFaces.clear();
    }

    bool BitmapFontEngine::LoadFontFace(
        const Rml::String& fileName, int faceIndex, bool fallbackFace, Rml::Style::FontWeight weight)
    {
        return mFallback->LoadFontFace(fileName, faceIndex, fallbackFace, weight);
    }

    bool BitmapFontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
        Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace)
    {
        if (endsWithFnt(fileName))
            return addBitmapFont(family, fileName);
        // A TrueType file: read through the virtual file system and handed to the default engine as memory.
        // RmlUi's rule for memory fonts: the bytes must stay available until after Rml::Shutdown, so they are kept
        // here.
        std::vector<char> bytes;
        if (mRead(fileName, bytes))
        {
            const std::vector<char>& kept = mFontBlobs.emplace_back(std::move(bytes));
            return mFallback->LoadFontFace(
                Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(kept.data()), kept.size()), faceIndex,
                family, style, weight, fallbackFace);
        }
        return mFallback->LoadFontFace(fileName, faceIndex, family, style, weight, fallbackFace);
    }

    bool BitmapFontEngine::LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family,
        Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace)
    {
        return mFallback->LoadFontFace(data, faceIndex, family, style, weight, fallbackFace);
    }

    Rml::FontFaceHandle BitmapFontEngine::GetFontFaceHandle(
        const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size)
    {
        const auto fontIt = mFonts.find(lowerCase(family));
        if (fontIt == mFonts.end())
            return mFallback->GetFontFaceHandle(family, style, weight, size);

        const Font* font = fontIt->second.get();
        const auto key = std::make_pair(font, size);
        auto faceIt = mFaces.find(key);
        if (faceIt == mFaces.end())
        {
            auto face = std::make_unique<Face>();
            face->mFont = font;
            face->mChoice = chooseSize(font->mFile.mSize, size);
            const Metrics m = metricsFor(font->mFile, *font->mMap, face->mChoice.mPlacement.mScale);
            face->mMetrics.size = m.mSize;
            face->mMetrics.ascent = m.mAscent;
            face->mMetrics.descent = m.mDescent;
            face->mMetrics.line_spacing = m.mLineSpacing;
            face->mMetrics.x_height = m.mXHeight;
            face->mMetrics.underline_position = m.mUnderlinePosition;
            face->mMetrics.underline_thickness = m.mUnderlineThickness;
            face->mMetrics.has_ellipsis = false;
            mOurFaces.insert(face.get());
            faceIt = mFaces.emplace(key, std::move(face)).first;
        }
        return reinterpret_cast<Rml::FontFaceHandle>(faceIt->second.get());
    }

    Rml::FontEffectsHandle BitmapFontEngine::PrepareFontEffects(
        Rml::FontFaceHandle handle, const Rml::FontEffectList& effects)
    {
        // A bitmap font has no font effects (outline, glow ...); the handle 0 means none.
        return isOurs(handle) ? 0 : mFallback->PrepareFontEffects(handle, effects);
    }

    const Rml::FontMetrics& BitmapFontEngine::GetFontMetrics(Rml::FontFaceHandle handle)
    {
        return isOurs(handle) ? asFace(handle)->mMetrics : mFallback->GetFontMetrics(handle);
    }

    int BitmapFontEngine::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
        const Rml::TextShapingContext& context, Rml::Character priorCharacter)
    {
        if (!isOurs(handle))
            return mFallback->GetStringWidth(handle, string, context, priorCharacter);
        const Face* face = asFace(handle);
        return stringWidth(face->mFont->mFile, *face->mFont->mMap, face->mChoice.mPlacement,
            std::string_view(string.begin(), string.size()));
    }

    int BitmapFontEngine::GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle handle,
        Rml::FontEffectsHandle effects, Rml::StringView string, Rml::Vector2f position,
        Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& context,
        Rml::TexturedMeshList& meshes)
    {
        if (!isOurs(handle))
            return mFallback->GenerateString(
                renderManager, handle, effects, string, position, colour, opacity, context, meshes);

        const Face* face = asFace(handle);
        const Font& font = *face->mFont;
        QuadMesh quads;
        quads.mVertices.reserve(string.size() * 4);
        quads.mIndices.reserve(string.size() * 6);
        const int width = layoutString(font.mFile, *font.mMap, face->mChoice.mPlacement,
            std::string_view(string.begin(), string.size()), position.x, position.y, quads);

        Rml::ColourbPremultiplied tint = colour;
        if (opacity < 1.f)
        {
            tint.red = Rml::byte(float(tint.red) * opacity);
            tint.green = Rml::byte(float(tint.green) * opacity);
            tint.blue = Rml::byte(float(tint.blue) * opacity);
            tint.alpha = Rml::byte(float(tint.alpha) * opacity);
        }
        meshes.resize(1);
        Rml::Mesh& mesh = meshes[0].mesh;
        mesh.vertices.reserve(quads.mVertices.size());
        for (const Vertex2& v : quads.mVertices)
            mesh.vertices.push_back({ { v.mX, v.mY }, tint, { v.mU, v.mV } });
        mesh.indices = quads.mIndices;
        meshes[0].texture = face->mChoice.mTextureNearest ? font.mNearest.GetTexture(renderManager)
                                                          : font.mLinear.GetTexture(renderManager);
        return width;
    }

    int BitmapFontEngine::GetVersion(Rml::FontFaceHandle handle)
    {
        return isOurs(handle) ? 0 : mFallback->GetVersion(handle);
    }
}
