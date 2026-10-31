#ifndef OPENMW_COMPONENTS_FX_TYPES_HPP
#define OPENMW_COMPONENTS_FX_TYPES_HPP

#include <array>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>

#include <components/debug/debuglog.hpp>
#include <components/vfs/pathutil.hpp>

// What a parsed .omwfx file describes, as plain data. Nothing here creates a texture, a shader or a pipeline.
namespace Fx
{
    namespace Types
    {
        enum class FilterMode
        {
            Linear,
            LinearMipmapLinear,
            LinearMipmapNearest,
            Nearest,
            NearestMipmapLinear,
            NearestMipmapNearest
        };

        enum class WrapMode
        {
            ClampToEdge,
            ClampToBorder,
            Repeat,
            Mirror
        };

        enum class SourceFormat
        {
            Red,
            Rg,
            Rgb,
            Bgr,
            Rgba,
            Bgra
        };

        enum class SourceType
        {
            Byte,
            UnsignedByte,
            Short,
            UnsignedShort,
            Int,
            UnsignedInt,
            UnsignedInt24_8,
            HalfFloat,
            Float,
            Double
        };

        enum class InternalFormat
        {
            Red,
            R16f,
            R32f,
            Rg,
            Rg16f,
            Rg32f,
            Rgb,
            Rgb16f,
            Rgb32f,
            Rgba,
            Rgba16f,
            Rgba32f,
            DepthComponent16,
            DepthComponent24,
            DepthComponent32,
            DepthComponent32f
        };

        enum class Compression
        {
            // The default when a sampler names no compression: use the format of the image file.
            ImageData,
            Auto,
            Arb,
            S3tcDxt1,
            S3tcDxt3,
            S3tcDxt5,
            Pvrtc2bpp,
            Pvrtc4bpp,
            Etc,
            Etc2,
            Rgtc1,
            Rgtc2,
            S3tcDxt1c,
            S3tcDxt1a
        };

        enum class BlendFunc
        {
            DstAlpha,
            DstColor,
            One,
            OneMinusDstAlpha,
            OneMinusDstColor,
            OneMinusSrcAlpha,
            OneMinusSrcColor,
            SrcAlpha,
            SrcAlphaSaturate,
            SrcColor,
            ConstantColor,
            OneMinusConstantColor,
            ConstantAlpha,
            OneMinusConstantAlpha,
            Zero
        };

        enum class BlendEquation
        {
            RgbaMin,
            RgbaMax,
            AlphaMin,
            AlphaMax,
            LogicOp,
            Add,
            Subtract,
            ReverseSubtract
        };

        struct SizeProxy
        {
            std::optional<float> mWidthRatio;
            std::optional<float> mHeightRatio;
            std::optional<int> mWidth;
            std::optional<int> mHeight;

            std::tuple<int, int> get(int width, int height) const
            {
                int scaledWidth = width;
                int scaledHeight = height;

                if (mWidthRatio)
                    scaledWidth = static_cast<int>(width * mWidthRatio.value());
                else if (mWidth)
                    scaledWidth = mWidth.value();

                if (mHeightRatio > 0.f)
                    scaledHeight = static_cast<int>(height * mHeightRatio.value());
                else if (mHeight)
                    scaledHeight = mHeight.value();

                return std::make_tuple(scaledWidth, scaledHeight);
            }
        };

        // A 'render_target' block. The defaults are those of OpenMW's Technique::parseBlockImp<Render_Target> and of
        // osg::Texture (commit 442eec6e2f). mSize resolves against the render size with SizeProxy::get.
        struct RenderTarget
        {
            std::string mName;
            SizeProxy mSize;
            SourceFormat mSourceFormat = SourceFormat::Rgb;
            InternalFormat mInternalFormat = InternalFormat::Rgb;
            SourceType mSourceType = SourceType::UnsignedByte;
            WrapMode mWrapS = WrapMode::ClampToEdge;
            WrapMode mWrapT = WrapMode::ClampToEdge;
            FilterMode mMinFilter = FilterMode::LinearMipmapLinear;
            FilterMode mMagFilter = FilterMode::Linear;
            bool mMipMap = false;
            vsg::vec4 mClearColor = vsg::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        };

        enum SamplerType
        {
            Texture_1D,
            Texture_2D,
            Texture_3D
        };

        // A 'sampler_1d', 'sampler_2d' or 'sampler_3d' block. The image is only named here, whoever creates the
        // texture loads it.
        struct SamplerDesc
        {
            std::string mName;
            VFS::Path::Normalized mSource;
            SamplerType mType = Texture_2D;
            WrapMode mWrapS = WrapMode::ClampToEdge;
            WrapMode mWrapT = WrapMode::ClampToEdge;
            WrapMode mWrapR = WrapMode::ClampToEdge;
            FilterMode mMinFilter = FilterMode::LinearMipmapLinear;
            FilterMode mMagFilter = FilterMode::Linear;
            Compression mCompression = Compression::ImageData;
            // OpenMW's Technique::parseSampler (commit 442eec6e2f) parsed source_format but then passed internal_format
            // to Texture::setSourceFormat. Both are recorded as written; the texture stage must choose.
            std::optional<SourceFormat> mSourceFormat;
            std::optional<SourceType> mSourceType;
            std::optional<InternalFormat> mInternalFormat;
        };

        // A pass: the shader blocks and block header sharing one name. The sources are the blocks as written, each
        // with its leading #line directive. The default vertex shader, the generated header and the 'shared' block
        // are not included here (Technique::getShared has the latter); a later stage assembles them as Fx::Pass did.
        struct PassDesc
        {
            enum class Type
            {
                None,
                Pixel,
                Compute
            };

            std::string getName() const { return mName; }

            std::string getTarget() const { return mTarget; }

            std::string mName;
            Type mType = Type::Pixel;
            bool mUBO = false;

            std::optional<std::string> mVertex;
            std::optional<std::string> mFragment;
            std::optional<std::string> mCompute;

            // The render targets this pass reads (rt1, rt2, rt3) and the one it writes (empty: none named).
            std::array<std::string, 3> mRenderTargets;
            std::string mTarget;

            std::optional<BlendFunc> mBlendSource;
            std::optional<BlendFunc> mBlendDest;
            std::optional<BlendEquation> mBlendEq;
        };

        template <class T>
        struct Choice
        {
            std::string mLabel;
            T mValue;
        };

        template <class T>
        struct Uniform
        {
            std::optional<T> mValue;
            std::optional<std::vector<T>> mArray;

            T mDefault = {};
            T mMin = std::numeric_limits<T>::lowest();
            T mMax = std::numeric_limits<T>::max();

            std::vector<Choice<T>> mChoices;

            using value_type = T;

            bool isArray() const { return mArray.has_value(); }

            const std::vector<T>& getArray() const { return *mArray; }

            T getValue() const { return mValue.value_or(mDefault); }
        };

        using Uniform_t = std::variant<Uniform<vsg::vec2>, Uniform<vsg::vec3>, Uniform<vsg::vec4>, Uniform<bool>,
            Uniform<float>, Uniform<int>>;

        struct UniformBase
        {
            std::string mName;
            std::string mDisplayName;
            std::string mHeader;
            std::string mTechniqueName;
            std::string mDescription;

            bool mStatic = true;
            std::optional<SamplerType> mSamplerType = std::nullopt;
            double mStep = 1.0;

            Uniform_t mData;

            template <class T>
            T getValue() const
            {
                return std::get<Uniform<T>>(mData).getValue();
            }

            size_t getNumElements() const
            {
                return std::visit([](auto&& arg) { return arg.isArray() ? arg.getArray().size() : 1; }, mData);
            }

            template <class T>
            T getMin() const
            {
                return std::get<Uniform<T>>(mData).mMin;
            }

            template <class T>
            T getMax() const
            {
                return std::get<Uniform<T>>(mData).mMax;
            }

            template <class T>
            T getDefault() const
            {
                return std::get<Uniform<T>>(mData).mDefault;
            }

            template <class T>
            void setValue(const T& value)
            {
                std::visit(
                    [&, value](auto&& arg) {
                        using U = typename std::decay_t<decltype(arg)>::value_type;

                        if constexpr (std::is_same_v<T, U>)
                        {
                            arg.mValue = value;
                        }
                        else
                        {
                            Log(Debug::Warning) << "Attempting to set uniform '" << mName << "' with wrong type";
                        }
                    },
                    mData);
            }

            template <class T, class A>
            void setValue(const std::vector<T, A>& value)
            {
                std::visit(
                    [&, value](auto&& arg) {
                        using U = typename std::decay_t<decltype(arg)>::value_type;

                        if (!arg.isArray() || arg.getArray().size() != value.size())
                        {
                            Log(Debug::Error)
                                << "Attempting to set uniform array '" << mName << "' with mismatching array sizes";
                            return;
                        }

                        if constexpr (std::is_same_v<T, U>)
                        {
                            arg.mArray = value;
                        }
                        else
                            Log(Debug::Warning) << "Attempting to set uniform array '" << mName << "' with wrong type";
                    },
                    mData);
            }

            // forceUniform: OpenMW's shader manager debug mode, where values stay editable instead of becoming
            // constants.
            std::optional<std::string> getGLSL(bool forceUniform) const
            {
                if (mSamplerType)
                {
                    switch (mSamplerType.value())
                    {
                        case Texture_1D:
                            return std::format("uniform sampler1D {};", mName);
                        case Texture_2D:
                            return std::format("uniform sampler2D {};", mName);
                        case Texture_3D:
                            return std::format("uniform sampler3D {};", mName);
                    }
                }

                return std::visit(
                    [&](auto&& arg) -> std::optional<std::string> {
                        using T = typename std::decay_t<decltype(arg)>::value_type;

                        auto value = arg.getValue();

                        const bool useUniform = arg.isArray() || (forceUniform || mStatic == false);
                        const std::string uname
                            = arg.isArray() ? std::format("{}[{}]", mName, arg.getArray().size()) : mName;

                        if constexpr (std::is_same_v<T, vsg::vec2>)
                        {
                            if (useUniform)
                                return std::format("uniform vec2 {};", uname);

                            return std::format("const vec2 {}=vec2({:f},{:f});", mName, value[0], value[1]);
                        }
                        else if constexpr (std::is_same_v<T, vsg::vec3>)
                        {
                            if (useUniform)
                                return std::format("uniform vec3 {};", uname);

                            return std::format(
                                "const vec3 {}=vec3({:f},{:f},{:f});", mName, value[0], value[1], value[2]);
                        }
                        else if constexpr (std::is_same_v<T, vsg::vec4>)
                        {
                            if (useUniform)
                                return std::format("uniform vec4 {};", uname);

                            return std::format("const vec4 {}=vec4({:f},{:f},{:f},{:f});", mName, value[0], value[1],
                                value[2], value[3]);
                        }
                        else if constexpr (std::is_same_v<T, float>)
                        {
                            if (useUniform)
                                return std::format("uniform float {};", uname);

                            return std::format("const float {}={:f};", mName, value);
                        }
                        else if constexpr (std::is_same_v<T, int>)
                        {
                            if (useUniform)
                                return std::format("uniform int {};", uname);

                            return std::format("const int {}={};", mName, value);
                        }
                        else
                        {
                            static_assert(std::is_same_v<T, bool>, "Non-exhaustive visitor");
                            if (useUniform)
                                return std::format("uniform bool {};", uname);

                            return std::format("const bool {}={};", mName, value);
                        }
                    },
                    mData);
            }
        };
    }
}

#endif
