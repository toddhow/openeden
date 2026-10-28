#pragma once

// Fixed-capacity ring buffer queue with zero dynamic allocation
// (Milestone C7 Stage C7b, docs/tasks/0026-c7-reconciliation-and-ownership.md).

#include <array>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace opas::reconcile {

template <typename T, std::size_t Capacity>
class FixedEventQueue {
    static_assert(Capacity > 0, "FixedEventQueue Capacity must be greater than zero");

public:
    FixedEventQueue() noexcept = default;

    ~FixedEventQueue() noexcept
    {
        clear();
    }

    FixedEventQueue(const FixedEventQueue&) = delete;
    FixedEventQueue& operator=(const FixedEventQueue&) = delete;

    FixedEventQueue(FixedEventQueue&& other) noexcept
    {
        moveFrom(std::move(other));
    }

    FixedEventQueue& operator=(FixedEventQueue&& other) noexcept
    {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size_ == 0;
    }

    [[nodiscard]] bool full() const noexcept
    {
        return size_ == Capacity;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept
    {
        return Capacity;
    }

    [[nodiscard]] std::size_t droppedCount() const noexcept
    {
        return droppedCount_;
    }

    [[nodiscard]] std::size_t totalPushedCount() const noexcept
    {
        return totalPushedCount_;
    }

    [[nodiscard]] const T* front() const noexcept
    {
        return empty() ? nullptr : ptrAt(head_);
    }

    [[nodiscard]] T* front() noexcept
    {
        return empty() ? nullptr : ptrAt(head_);
    }

    [[nodiscard]] const T* back() const noexcept
    {
        if (empty()) {
            return nullptr;
        }
        const std::size_t idx = (tail_ == 0) ? (Capacity - 1) : (tail_ - 1);
        return ptrAt(idx);
    }

    [[nodiscard]] T* back() noexcept
    {
        if (empty()) {
            return nullptr;
        }
        const std::size_t idx = (tail_ == 0) ? (Capacity - 1) : (tail_ - 1);
        return ptrAt(idx);
    }

    template <typename... Args>
    bool emplace(Args&&... args)
    {
        if (full()) {
            ++droppedCount_;
            return false;
        }
        ::new (static_cast<void*>(ptrAt(tail_))) T(std::forward<Args>(args)...);
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        ++totalPushedCount_;
        return true;
    }

    bool push(const T& item)
    {
        if (full()) {
            ++droppedCount_;
            return false;
        }
        ::new (static_cast<void*>(ptrAt(tail_))) T(item);
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        ++totalPushedCount_;
        return true;
    }

    bool push(T&& item)
    {
        if (full()) {
            ++droppedCount_;
            return false;
        }
        ::new (static_cast<void*>(ptrAt(tail_))) T(std::move(item));
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        ++totalPushedCount_;
        return true;
    }

    std::optional<T> pop()
    {
        if (empty()) {
            return std::nullopt;
        }
        T* p = ptrAt(head_);
        std::optional<T> result(std::move(*p));
        p->~T();
        head_ = (head_ + 1) % Capacity;
        --size_;
        return result;
    }

    void clear() noexcept
    {
        while (!empty()) {
            ptrAt(head_)->~T();
            head_ = (head_ + 1) % Capacity;
            --size_;
        }
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

private:
    struct alignas(alignof(T)) StorageSlot {
        std::byte data[sizeof(T)];
    };

    std::array<StorageSlot, Capacity> storage_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
    std::size_t droppedCount_ = 0;
    std::size_t totalPushedCount_ = 0;

    T* ptrAt(std::size_t idx) noexcept
    {
        return std::launder(reinterpret_cast<T*>(&storage_[idx].data[0]));
    }

    const T* ptrAt(std::size_t idx) const noexcept
    {
        return std::launder(reinterpret_cast<const T*>(&storage_[idx].data[0]));
    }

    void moveFrom(FixedEventQueue&& other) noexcept
    {
        while (!other.empty()) {
            T* src = other.ptrAt(other.head_);
            ::new (static_cast<void*>(ptrAt(tail_))) T(std::move(*src));
            src->~T();
            other.head_ = (other.head_ + 1) % Capacity;
            --other.size_;
            tail_ = (tail_ + 1) % Capacity;
            ++size_;
        }
        droppedCount_ = other.droppedCount_;
        totalPushedCount_ = other.totalPushedCount_;
        other.head_ = 0;
        other.tail_ = 0;
        other.size_ = 0;
        other.droppedCount_ = 0;
        other.totalPushedCount_ = 0;
    }
};

} // namespace opas::reconcile
