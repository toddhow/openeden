#pragma once

#include <cstddef>
#include <new>
#include <utility>

namespace openeden::sdk
{
    /**
     * @brief Frame-scoped linear scratch bump memory allocator.
     * Guarantees zero hot-path heap allocations during frame and fixed-step simulation callbacks.
     */
    class ScratchMemoryService
    {
    public:
        virtual ~ScratchMemoryService() = default;

        /**
         * @brief Linearly allocates memory from the pre-allocated frame scratch buffer with specified alignment.
         * Returns nullptr if requested allocation exceeds remaining capacity.
         */
        virtual void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) = 0;

        /**
         * @brief Resets the bump pointer to zero, reusing the pre-allocated buffer for the next frame.
         */
        virtual void reset() = 0;

        [[nodiscard]] virtual std::size_t getCapacity() const noexcept = 0;
        [[nodiscard]] virtual std::size_t getUsedBytes() const noexcept = 0;
        [[nodiscard]] virtual std::size_t getRemainingBytes() const noexcept = 0;

        /**
         * @brief Constructs an object of type T in frame-scoped scratch memory with proper alignment.
         */
        template <typename T, typename... Args>
        T* create(Args&&... args)
        {
            void* mem = allocate(sizeof(T), alignof(T));
            if (!mem)
            {
                return nullptr;
            }
            return ::new (mem) T(std::forward<Args>(args)...);
        }
    };
} // namespace openeden::sdk
