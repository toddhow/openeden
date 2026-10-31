#ifndef OPENMW_COMPONENTS_FX_PARSECONSTANTS_HPP
#define OPENMW_COMPONENTS_FX_PARSECONSTANTS_HPP

#include <array>
#include <string_view>

#include "technique.hpp"

namespace Fx
{
    namespace Constants
    {
        constexpr std::array<std::pair<std::string_view, Fx::FlagsType>, 6> TechniqueFlag = { {
            { "disable_interiors", Technique::Flag_Disable_Interiors },
            { "disable_exteriors", Technique::Flag_Disable_Exteriors },
            { "disable_underwater", Technique::Flag_Disable_Underwater },
            { "disable_abovewater", Technique::Flag_Disable_Abovewater },
            { "disable_sunglare", Technique::Flag_Disable_SunGlare },
            { "hidden", Technique::Flag_Hidden },
        } };

        constexpr std::array<std::pair<std::string_view, Types::SourceFormat>, 6> SourceFormat = { {
            { "red", Types::SourceFormat::Red },
            { "rg", Types::SourceFormat::Rg },
            { "rgb", Types::SourceFormat::Rgb },
            { "bgr", Types::SourceFormat::Bgr },
            { "rgba", Types::SourceFormat::Rgba },
            { "bgra", Types::SourceFormat::Bgra },
        } };

        constexpr std::array<std::pair<std::string_view, Types::SourceType>, 10> SourceType = { {
            { "byte", Types::SourceType::Byte },
            { "unsigned_byte", Types::SourceType::UnsignedByte },
            { "short", Types::SourceType::Short },
            { "unsigned_short", Types::SourceType::UnsignedShort },
            { "int", Types::SourceType::Int },
            { "unsigned_int", Types::SourceType::UnsignedInt },
            { "unsigned_int_24_8", Types::SourceType::UnsignedInt24_8 },
            { "half_float", Types::SourceType::HalfFloat },
            { "float", Types::SourceType::Float },
            { "double", Types::SourceType::Double },
        } };

        constexpr std::array<std::pair<std::string_view, Types::InternalFormat>, 16> InternalFormat = { {
            { "red", Types::InternalFormat::Red },
            { "r16f", Types::InternalFormat::R16f },
            { "r32f", Types::InternalFormat::R32f },
            { "rg", Types::InternalFormat::Rg },
            { "rg16f", Types::InternalFormat::Rg16f },
            { "rg32f", Types::InternalFormat::Rg32f },
            { "rgb", Types::InternalFormat::Rgb },
            { "rgb16f", Types::InternalFormat::Rgb16f },
            { "rgb32f", Types::InternalFormat::Rgb32f },
            { "rgba", Types::InternalFormat::Rgba },
            { "rgba16f", Types::InternalFormat::Rgba16f },
            { "rgba32f", Types::InternalFormat::Rgba32f },
            { "depth_component16", Types::InternalFormat::DepthComponent16 },
            { "depth_component24", Types::InternalFormat::DepthComponent24 },
            { "depth_component32", Types::InternalFormat::DepthComponent32 },
            { "depth_component32f", Types::InternalFormat::DepthComponent32f },
        } };

        constexpr std::array<std::pair<std::string_view, Types::Compression>, 13> Compression = { {
            { "auto", Types::Compression::Auto },
            { "arb", Types::Compression::Arb },
            { "s3tc_dxt1", Types::Compression::S3tcDxt1 },
            { "s3tc_dxt3", Types::Compression::S3tcDxt3 },
            { "s3tc_dxt5", Types::Compression::S3tcDxt5 },
            { "pvrtc_2bpp", Types::Compression::Pvrtc2bpp },
            { "pvrtc_4bpp", Types::Compression::Pvrtc4bpp },
            { "etc", Types::Compression::Etc },
            { "etc2", Types::Compression::Etc2 },
            { "rgtc1", Types::Compression::Rgtc1 },
            { "rgtc2", Types::Compression::Rgtc2 },
            { "s3tc_dxt1c", Types::Compression::S3tcDxt1c },
            { "s3tc_dxt1a", Types::Compression::S3tcDxt1a },
        } };

        constexpr std::array<std::pair<std::string_view, Types::WrapMode>, 4> WrapMode = { {
            { "clamp_to_edge", Types::WrapMode::ClampToEdge },
            { "clamp_to_border", Types::WrapMode::ClampToBorder },
            { "repeat", Types::WrapMode::Repeat },
            { "mirror", Types::WrapMode::Mirror },
        } };

        constexpr std::array<std::pair<std::string_view, Types::FilterMode>, 6> FilterMode = { {
            { "linear", Types::FilterMode::Linear },
            { "linear_mipmap_linear", Types::FilterMode::LinearMipmapLinear },
            { "linear_mipmap_nearest", Types::FilterMode::LinearMipmapNearest },
            { "nearest", Types::FilterMode::Nearest },
            { "nearest_mipmap_linear", Types::FilterMode::NearestMipmapLinear },
            { "nearest_mipmap_nearest", Types::FilterMode::NearestMipmapNearest },
        } };

        constexpr std::array<std::pair<std::string_view, Types::BlendFunc>, 15> BlendFunc = { {
            { "dst_alpha", Types::BlendFunc::DstAlpha },
            { "dst_color", Types::BlendFunc::DstColor },
            { "one", Types::BlendFunc::One },
            { "one_minus_dst_alpha", Types::BlendFunc::OneMinusDstAlpha },
            { "one_minus_dst_color", Types::BlendFunc::OneMinusDstColor },
            { "one_minus_src_alpha", Types::BlendFunc::OneMinusSrcAlpha },
            { "one_minus_src_color", Types::BlendFunc::OneMinusSrcColor },
            { "src_alpha", Types::BlendFunc::SrcAlpha },
            { "src_alpha_saturate", Types::BlendFunc::SrcAlphaSaturate },
            { "src_color", Types::BlendFunc::SrcColor },
            { "constant_color", Types::BlendFunc::ConstantColor },
            { "one_minus_constant_color", Types::BlendFunc::OneMinusConstantColor },
            { "constant_alpha", Types::BlendFunc::ConstantAlpha },
            { "one_minus_constant_alpha", Types::BlendFunc::OneMinusConstantAlpha },
            { "zero", Types::BlendFunc::Zero },
        } };

        constexpr std::array<std::pair<std::string_view, Types::BlendEquation>, 8> BlendEquation = { {
            { "rgba_min", Types::BlendEquation::RgbaMin },
            { "rgba_max", Types::BlendEquation::RgbaMax },
            { "alpha_min", Types::BlendEquation::AlphaMin },
            { "alpha_max", Types::BlendEquation::AlphaMax },
            { "logic_op", Types::BlendEquation::LogicOp },
            { "add", Types::BlendEquation::Add },
            { "subtract", Types::BlendEquation::Subtract },
            { "reverse_subtract", Types::BlendEquation::ReverseSubtract },
        } };
    }
}

#endif
