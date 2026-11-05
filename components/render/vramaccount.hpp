#ifndef OPENEDEN_RENDER_VRAMACCOUNT_H
#define OPENEDEN_RENDER_VRAMACCOUNT_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Render::VideoMemory
{
    /*
     * The ledger of the video memory the renderer holds (task 0080, stage a): named entries, each set by the system
     * that owns the memory from the dimensions and formats it created. Nothing is read back from Vulkan per object.
     * Entries are kept sorted by name so that logs and the controller see the same order every run.
     *
     * Not thread-safe: owners call it on the main thread.
     */
    class Ledger
    {
    public:
        /// Creates the entry or replaces its bytes; a value of 0 or less removes it.
        void set(std::string_view name, std::int64_t bytes)
        {
            const auto it = find(name);
            if (bytes <= 0)
            {
                if (it != mEntries.end() && it->first == name)
                    mEntries.erase(it);
                return;
            }
            if (it != mEntries.end() && it->first == name)
                it->second = bytes;
            else
                mEntries.insert(it, { std::string(name), bytes });
        }

        /// Adds to the entry (creating it); the entry is removed when it reaches 0 or less.
        void add(std::string_view name, std::int64_t deltaBytes)
        {
            const auto it = find(name);
            const std::int64_t before = (it != mEntries.end() && it->first == name) ? it->second : 0;
            set(name, before + deltaBytes);
        }

        std::int64_t total() const
        {
            std::int64_t sum = 0;
            for (const auto& entry : mEntries)
                sum += entry.second;
            return sum;
        }

        /// The usage the ledger cannot explain: max(0, usage - total). An unknown usage (negative) has none.
        std::int64_t untracked(std::int64_t usageBytes) const
        {
            if (usageBytes < 0)
                return 0;
            return std::max<std::int64_t>(0, usageBytes - total());
        }

        /// 0 when there is no such entry.
        std::int64_t get(std::string_view name) const
        {
            const auto it = find(name);
            return (it != mEntries.end() && it->first == name) ? it->second : 0;
        }

        const std::vector<std::pair<std::string, std::int64_t>>& entries() const { return mEntries; }

    private:
        using Entries = std::vector<std::pair<std::string, std::int64_t>>;

        Entries::const_iterator find(std::string_view name) const
        {
            return std::lower_bound(mEntries.begin(), mEntries.end(), name,
                [](const std::pair<std::string, std::int64_t>& entry, std::string_view key) {
                    return std::string_view(entry.first) < key;
                });
        }

        Entries::iterator find(std::string_view name)
        {
            return std::lower_bound(mEntries.begin(), mEntries.end(), name,
                [](const std::pair<std::string, std::int64_t>& entry, std::string_view key) {
                    return std::string_view(entry.first) < key;
                });
        }

        Entries mEntries; // sorted by name
    };

    /// width * height * bytes per texel * layers, for a render target (no mips): what an owner passes to Ledger::set.
    inline std::int64_t bytesOfTarget(
        std::uint32_t width, std::uint32_t height, std::uint32_t bytesPerTexel, std::uint32_t layers = 1)
    {
        return static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height)
            * static_cast<std::int64_t>(bytesPerTexel) * static_cast<std::int64_t>(layers);
    }
}

#endif
