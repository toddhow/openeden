#include "scratch_memory.hpp"

namespace openeden::plugin
{
    ScratchMemoryServiceImpl::ScratchMemoryServiceImpl(std::size_t capacity)
        : mCapacity(capacity)
        , mBuffer(std::make_unique<std::uint8_t[]>(capacity))
    {
    }

    ScratchMemoryServiceImpl::ScratchMemoryServiceImpl(ScratchMemoryServiceImpl&& other) noexcept
        : mCapacity(other.mCapacity)
        , mOffset(other.mOffset)
        , mBuffer(std::move(other.mBuffer))
    {
        other.mCapacity = 0;
        other.mOffset = 0;
    }

    ScratchMemoryServiceImpl& ScratchMemoryServiceImpl::operator=(ScratchMemoryServiceImpl&& other) noexcept
    {
        if (this != &other)
        {
            mCapacity = other.mCapacity;
            mOffset = other.mOffset;
            mBuffer = std::move(other.mBuffer);
            other.mCapacity = 0;
            other.mOffset = 0;
        }
        return *this;
    }

    void* ScratchMemoryServiceImpl::allocate(std::size_t size, std::size_t alignment)
    {
        if (mCapacity == 0 || !mBuffer)
        {
            return nullptr;
        }

        if (alignment == 0)
        {
            alignment = alignof(std::max_align_t);
        }

        // Alignment must be a non-zero power of two
        if ((alignment & (alignment - 1)) != 0)
        {
            return nullptr;
        }

        const auto currentAddr = reinterpret_cast<std::uintptr_t>(mBuffer.get() + mOffset);
        const auto alignedAddr = (currentAddr + (alignment - 1)) & ~(alignment - 1);
        const auto padding = static_cast<std::size_t>(alignedAddr - currentAddr);

        // Safe bounds check preventing integer overflow
        if (padding > mCapacity - mOffset || size > (mCapacity - mOffset - padding))
        {
            return nullptr;
        }

        mOffset += padding + size;
        return reinterpret_cast<void*>(alignedAddr);
    }

    void ScratchMemoryServiceImpl::reset()
    {
        mOffset = 0;
    }

    std::size_t ScratchMemoryServiceImpl::getCapacity() const noexcept
    {
        return mCapacity;
    }

    std::size_t ScratchMemoryServiceImpl::getUsedBytes() const noexcept
    {
        return mOffset;
    }

    std::size_t ScratchMemoryServiceImpl::getRemainingBytes() const noexcept
    {
        return mOffset <= mCapacity ? mCapacity - mOffset : 0;
    }
} // namespace openeden::plugin
