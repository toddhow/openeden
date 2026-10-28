#pragma once

#include <openeden/version.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::contracts
{
    /**
     * @brief Versioned contract for converting NetImmerse Keyframe (.kf) animations
     * and companion skeleton (.nif) models to glTF 2.0 (.glb) buffers and clip sidecars.
     */
    class KfConverterContract
    {
    public:
        static constexpr std::string_view kId = "openeden.compat.kf_converter";
        static constexpr sdk::Version kVersion{ 1, 0 };

        virtual ~KfConverterContract() = default;

        struct ConversionOptions
        {
            float unitsPerMeter = 69.9912f;
            int resampleRateHz = 60;
            bool embedEventsInExtras = false;
            std::string clipPrefix = "";
        };

        struct ConversionResult
        {
            bool success = false;
            std::vector<std::uint8_t> glbData;
            std::string sidecarJson;
            std::string errorMessage;
        };

        /**
         * @brief Converts a NetImmerse .kf buffer and companion skeleton .nif buffer into
         * a binary glTF 2.0 (.glb) container and an opas.clipsidecar JSON string.
         */
        virtual ConversionResult convertKfToGltf(
            std::span<const std::uint8_t> kfBytes,
            std::span<const std::uint8_t> skeletonNifBytes,
            const ConversionOptions& options = {}) = 0;
    };
} // namespace openeden::contracts
