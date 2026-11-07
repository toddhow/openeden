#ifndef OPENEDEN_COMPONENTS_CONTENTSOURCE_DECODERREGISTRY_H
#define OPENEDEN_COMPONENTS_CONTENTSOURCE_DECODERREGISTRY_H

#include <cstdint>
#include <functional>
#include <map>
#include <utility>

// Layer 2 of the record source (task 0083, docs/design/game-packs.md 5.2): a map from a record type to the function
// that decodes that record, filled by the game that owns the records. The registry is a template over the context a
// decoder works on (for Morrowind, the reader and the store; for the TES4 family, its reader), so this directory needs
// no include of any record or reader. The types are ordered, so listing them and counting what was skipped is
// deterministic.
namespace Content
{
    template <class Context>
    class DecoderRegistry
    {
    public:
        using Decoder = std::function<bool(Context&)>;

        /// Registers the decoder of a record type. A decoder returns false when it consumed the record without applying
        /// it (a deleted record), true otherwise. A second registration of a type, or an empty decoder, is refused
        /// (false) and changes nothing.
        bool add(std::uint32_t type, Decoder decoder)
        {
            if (!decoder)
            {
                return false;
            }
            return mDecoders.emplace(type, std::move(decoder)).second;
        }

        /// The decoder of a type, or nullptr. The pointer stays valid until the registry is destroyed.
        const Decoder* find(std::uint32_t type) const
        {
            const auto it = mDecoders.find(type);
            return it == mDecoders.end() ? nullptr : &it->second;
        }

        std::size_t size() const { return mDecoders.size(); }
        const std::map<std::uint32_t, Decoder>& decoders() const { return mDecoders; }

        /// Counts a record that had no decoder, by type, for the log and the tools.
        void countSkipped(std::uint32_t type) { ++mSkipped[type]; }
        const std::map<std::uint32_t, std::uint64_t>& skipped() const { return mSkipped; }

    private:
        std::map<std::uint32_t, Decoder> mDecoders;
        std::map<std::uint32_t, std::uint64_t> mSkipped;
    };
}

#endif
