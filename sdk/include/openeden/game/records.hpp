#pragma once

#include <cstdint>
#include <string_view>

namespace openeden::sdk
{
    /// Four characters as a record type id, for example makeRecordTypeId('N', 'P', 'C', '_').
    [[nodiscard]] constexpr std::uint32_t makeRecordTypeId(char a, char b, char c, char d) noexcept
    {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(a))
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16)
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
    }

    /// A record type a game pack asks the core to store (docs/design/game-packs.md section 5.1). No third-party types.
    struct RecordTypeInfo
    {
        std::uint32_t typeId = 0;
        std::string_view name; ///< Copied by the host
        std::uint32_t sizeBytes = 0;
    };

    /**
     * @brief Game packs only. Main thread only.
     */
    class RecordService
    {
    public:
        virtual ~RecordService() = default;

        /// False for a zero id, a zero size, an empty name or an id already registered.
        [[nodiscard]] virtual bool registerRecordType(const RecordTypeInfo& info) = 0;

        /// Null if not registered. The pointer stays valid until the host shuts down.
        [[nodiscard]] virtual const RecordTypeInfo* findRecordType(std::uint32_t typeId) const = 0;
    };
} // namespace openeden::sdk
