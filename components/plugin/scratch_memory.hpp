#pragma once

#include <openeden/memory.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of frame-scoped linear bump scratch allocator.
     */
    class ScratchMemoryServiceImpl : public openeden::sdk::ScratchMemoryService
    {
    public:
        explicit ScratchMemoryServiceImpl(std::size_t capacity = 2 * 1024 * 1024);
        ~ScratchMemoryServiceImpl() override = default;

        // Non-copyable
        ScratchMemoryServiceImpl(const ScratchMemoryServiceImpl&) = delete;
        ScratchMemoryServiceImpl& operator=(const ScratchMemoryServiceImpl&) = delete;

        // Movable
        ScratchMemoryServiceImpl(ScratchMemoryServiceImpl&&) noexcept;
        ScratchMemoryServiceImpl& operator=(ScratchMemoryServiceImpl&&) noexcept;

        void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;

        void reset() override;

        [[nodiscard]] std::size_t getCapacity() const noexcept override;
        [[nodiscard]] std::size_t getUsedBytes() const noexcept override;
        [[nodiscard]] std::size_t getRemainingBytes() const noexcept override;

    private:
        std::size_t mCapacity;
        std::size_t mOffset = 0;
        std::unique_ptr<std::uint8_t[]> mBuffer;
    };
} // namespace openeden::plugin
