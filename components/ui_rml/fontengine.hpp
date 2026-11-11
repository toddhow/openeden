#ifndef OPENEDEN_COMPONENTS_UI_RML_FONTENGINE_H
#define OPENEDEN_COMPONENTS_UI_RML_FONTENGINE_H

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontMetrics.h>

#include <components/toutf8/toutf8.hpp>

#include "bitmapfont.hpp"

namespace UiRml
{
    /// Implemented by the render interface: the next texture it is asked to generate gets nearest instead of linear
    /// sampling (a bitmap font drawn at a whole-number scale). The font engine sets it around its own texture upload.
    class TextureFilterHint
    {
    public:
        virtual ~TextureFilterHint() = default;
        virtual void setNextTextureNearest(bool nearest) = 0;
    };

    /// RmlUi font engine that draws Morrowind's bitmap fonts (.fnt + .tex) itself and hands every other face to the
    /// engine RmlUi brought (FreeType, TrueType). Which one a family belongs to is decided by the family name: a family
    /// added with addBitmapFont is ours, anything else goes to the fallback (decision 0015, note 0045 section 14.3).
    ///
    /// Owner: whoever creates it (UiRml::Overlay); it must outlive the RmlUi core it is installed in (Rml::Shutdown
    /// releases fonts through it). The fallback is not owned (RmlUi owns its default engine).
    /// Thread: all calls come from RmlUi's thread (the one that updates and renders contexts).
    class BitmapFontEngine final : public Rml::FontEngineInterface
    {
    public:
        /// Reads a whole file of the virtual file system; false when it does not exist.
        using ReadFn = std::function<bool(const std::string& path, std::vector<char>& out)>;

        BitmapFontEngine(
            Rml::FontEngineInterface* fallback, ReadFn read, ToUTF8::FromType gameEncoding, TextureFilterHint* hint);
        ~BitmapFontEngine() override;

        /// Reads `fntPath` (and the `fonts/<name>.tex` it names) and makes it the family `family` (case insensitive).
        /// Returns false and logs an RmlUi error when a file is missing or not a font.
        bool addBitmapFont(const std::string& family, const std::string& fntPath);

        /// True when `family` is one of ours.
        bool hasBitmapFamily(const std::string& family) const;

        // Rml::FontEngineInterface
        void Initialize() override {}
        void Shutdown() override;
        bool LoadFontFace(
            const Rml::String& fileName, int faceIndex, bool fallbackFace, Rml::Style::FontWeight weight) override;
        bool LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
            Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) override;
        bool LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family,
            Rml::Style::FontStyle style, Rml::Style::FontWeight weight, bool fallbackFace) override;
        Rml::FontFaceHandle GetFontFaceHandle(
            const Rml::String& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, int size) override;
        Rml::FontEffectsHandle PrepareFontEffects(
            Rml::FontFaceHandle handle, const Rml::FontEffectList& effects) override;
        const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;
        int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& context,
            Rml::Character priorCharacter) override;
        int GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle handle,
            Rml::FontEffectsHandle effects, Rml::StringView string, Rml::Vector2f position,
            Rml::ColourbPremultiplied colour, float opacity, const Rml::TextShapingContext& context,
            Rml::TexturedMeshList& meshes) override;
        int GetVersion(Rml::FontFaceHandle handle) override;
        void ReleaseFontResources() override;

    private:
        struct Font;
        struct Face;

        bool isOurs(Rml::FontFaceHandle handle) const { return mOurFaces.contains(asFace(handle)); }
        const Face* asFace(Rml::FontFaceHandle handle) const { return reinterpret_cast<const Face*>(handle); }

        Rml::FontEngineInterface* mFallback;
        ReadFn mRead;
        ToUTF8::FromType mGameEncoding;
        TextureFilterHint* mHint;
        std::map<std::string, std::unique_ptr<Font>> mFonts; // family, lower case
        std::map<std::pair<const Font*, int>, std::unique_ptr<Face>> mFaces;
        std::unordered_set<const Face*> mOurFaces;
        std::deque<std::vector<char>> mFontBlobs; // TrueType files handed to the fallback as memory
    };
}

#endif
