#ifndef OPENMW_COMPONENTS_FX_TECHNIQUE_HPP
#define OPENMW_COMPONENTS_FX_TECHNIQUE_HPP

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <components/vfs/pathutil.hpp>

#include "lexer.hpp"
#include "types.hpp"

namespace VFS
{
    class Manager;
}

namespace Fx
{
    using FlagsType = size_t;

    // Parses an .omwfx file into plain data: no renderer, image loading or settings are involved. The renderer stage
    // builds its pipelines from getPasses(), getSamplers(), getRenderTargetsMap() and getUniformMap().
    class Technique
    {
    public:
        using PassList = std::vector<std::shared_ptr<Types::PassDesc>>;
        using SamplerList = std::vector<Types::SamplerDesc>;

        using UniformMap = std::vector<std::shared_ptr<Types::UniformBase>>;
        using RenderTargetMap = std::unordered_map<std::string_view, Types::RenderTarget>;

        static constexpr std::string_view sExt = "omwfx";
        static constexpr VFS::Path::NormalizedView sSubdir{ "shaders" };

        enum class Status
        {
            Success,
            Uncompiled,
            File_Not_exists,
            Parse_Error
        };

        static constexpr FlagsType Flag_Disable_Interiors = (1 << 0);
        static constexpr FlagsType Flag_Disable_Exteriors = (1 << 1);
        static constexpr FlagsType Flag_Disable_Underwater = (1 << 2);
        static constexpr FlagsType Flag_Disable_Abovewater = (1 << 3);
        static constexpr FlagsType Flag_Disable_SunGlare = (1 << 4);
        static constexpr FlagsType Flag_Hidden = (1 << 5);

        static VFS::Path::Normalized makeFileName(std::string_view name);

        // ubo and supportsNormals are what the renderer can do: without ubo the default GLSL version is 120, without
        // supportsNormals the 'pass_normals' key has no effect.
        Technique(const VFS::Manager& vfs, VFS::Path::NormalizedView fileName, std::string name, bool ubo,
            bool supportsNormals);

        bool compile();

        std::string getName() const;

        const VFS::Path::Normalized& getFileName() const { return mFilePath; }

        bool setLastModificationTime(std::filesystem::file_time_type timeStamp);

        bool isValid() const { return mValid; }

        bool getHDR() const { return mHDR; }

        bool getNormals() const { return mNormals && mSupportsNormals; }

        bool getLights() const { return mLights; }

        const PassList& getPasses() { return mPasses; }

        const SamplerList& getSamplers() const { return mSamplers; }

        // The 'shared' block with its #line directive, empty if there is none. Goes before each pass's source.
        const std::string& getShared() const { return mShared; }

        Status getStatus() const { return mStatus; }

        std::string_view getAuthor() const { return mAuthor; }

        std::string_view getDescription() const { return mDescription; }

        std::string_view getVersion() const { return mVersion; }

        int getGLSLVersion() const { return mGLSLVersion; }

        std::string getGLSLProfile() const { return mGLSLProfile; }

        const std::unordered_set<std::string>& getGLSLExtensions() const { return mGLSLExtensions; }

        FlagsType getFlags() const { return mFlags; }

        bool getHidden() const { return mFlags & Flag_Hidden; }

        UniformMap& getUniformMap() { return mDefinedUniforms; }

        RenderTargetMap& getRenderTargetsMap() { return mRenderTargets; }

        std::string getLastError() const { return mLastError; }

        UniformMap::iterator findUniform(std::string_view name);

        bool getDynamic() const { return mDynamic; }

        void setLocked(bool locked) { mLocked = locked; }
        bool getLocked() const { return mLocked; }

        void setInternal(bool internal) { mInternal = internal; }
        bool getInternal() const { return mInternal; }

    private:
        [[noreturn]] void error(std::string_view msg);

        void clear();

        std::string_view asLiteral() const;

        template <class T>
        void expect(std::string_view err = {});

        template <class T, class T2>
        void expect(std::string_view err = {});

        template <class T>
        bool isNext();

        void parse(std::string&& buffer);

        template <class SrcT, class T>
        void parseUniform();

        template <class T>
        void parseSampler();

        template <class T>
        void parseBlock(bool named = true);

        template <class T>
        void parseBlockImp()
        {
        }

        void parseBlockHeader();

        bool parseBool();

        std::string_view parseString();

        float parseFloat();

        int parseInteger();

        Types::InternalFormat parseInternalFormat();

        Types::SourceType parseSourceType();

        Types::SourceFormat parseSourceFormat();

        template <class SrcT, class T>
        void parseWidgetType(Types::Uniform<SrcT>& uniform);

        template <class SrcT, class T>
        SrcT getUniformValue();

        Types::BlendEquation parseBlendEquation();

        Types::BlendFunc parseBlendFuncMode();

        Types::WrapMode parseWrapMode();

        Types::Compression parseCompression();

        FlagsType parseFlags();

        Types::FilterMode parseFilterMode();

        template <class TDelimeter>
        std::vector<std::string_view> parseLiteralList();

        template <class Vec, class T>
        Vec parseVec();

        std::string getBlockWithLineDirective();

        std::unique_ptr<Lexer::Lexer> mLexer;
        Lexer::Token mToken;

        std::string mShared;
        std::string mName;
        VFS::Path::Normalized mFilePath;
        std::string_view mBlockName;
        std::string_view mAuthor;
        std::string_view mDescription;
        std::string_view mVersion;

        std::unordered_set<std::string> mGLSLExtensions;
        int mGLSLVersion;
        std::string mGLSLProfile;

        FlagsType mFlags;

        Status mStatus;

        bool mEnabled;

        std::filesystem::file_time_type mLastModificationTime;
        bool mValid;
        bool mHDR;
        bool mNormals;
        bool mLights;

        RenderTargetMap mRenderTargets;

        SamplerList mSamplers;
        PassList mPasses;

        std::unordered_map<std::string_view, std::shared_ptr<Types::PassDesc>> mPassMap;
        std::vector<std::string_view> mPassKeys;

        Types::PassDesc::Type mLastAppliedType;

        UniformMap mDefinedUniforms;

        const VFS::Manager& mVFS;
        bool mUBO;
        bool mSupportsNormals;

        std::string mBuffer;

        std::string mLastError;

        bool mDynamic = false;
        bool mLocked = false;
        bool mInternal = false;
    };

    template <>
    void Technique::parseBlockImp<Lexer::Shared>();
    template <>
    void Technique::parseBlockImp<Lexer::Technique>();
    template <>
    void Technique::parseBlockImp<Lexer::Render_Target>();
    template <>
    void Technique::parseBlockImp<Lexer::Vertex>();
    template <>
    void Technique::parseBlockImp<Lexer::Fragment>();
    template <>
    void Technique::parseBlockImp<Lexer::Compute>();
    template <>
    void Technique::parseBlockImp<Lexer::Sampler_1D>();
    template <>
    void Technique::parseBlockImp<Lexer::Sampler_2D>();
    template <>
    void Technique::parseBlockImp<Lexer::Sampler_3D>();
    template <>
    void Technique::parseBlockImp<Lexer::Uniform_Bool>();
    template <>
    void Technique::parseBlockImp<Lexer::Uniform_Float>();
    template <>
    void Technique::parseBlockImp<Lexer::Uniform_Int>();
    template <>
    void Technique::parseBlockImp<Lexer::Uniform_Vec2>();
    template <>
    void Technique::parseBlockImp<Lexer::Uniform_Vec3>();
    template <>
    void Technique::parseBlockImp<Lexer::Uniform_Vec4>();
}

#endif
