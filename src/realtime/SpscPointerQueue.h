#pragma once

#include <array>
#include <atomic>
#include <cstddef>

// Fixed-capacity single-producer/single-consumer pointer queue.
template <typename T, std::size_t Capacity>
class SpscPointerQueue final
{
    static_assert(Capacity > 1);
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "Realtime retirement queues require lock-free index atomics.");

public:
    bool push(T* value) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto read = readIndex.load(std::memory_order_acquire);
        if (write - read >= Capacity)
            return false;
        entries[write % Capacity] = value;
        writeIndex.store(write + 1, std::memory_order_release);
        return true;
    }

    T* pop() noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        if (read == writeIndex.load(std::memory_order_acquire))
            return nullptr;
        auto* value = entries[read % Capacity];
        readIndex.store(read + 1, std::memory_order_release);
        return value;
    }

private:
    std::array<T*, Capacity> entries {};
    alignas(64) std::atomic<std::size_t> writeIndex { 0 };
    alignas(64) std::atomic<std::size_t> readIndex { 0 };
};
