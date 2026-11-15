#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openeden/game/save_chunks.hpp>

// Task 0177 (neutral world saves, stage N6, stage 3): the Skyrim SE pack's first save chunk, "loadorder".
//
// A neutral save names its objects by the pack's own ids (plugin-and-local-form-id identities resolved by the load
// order), so it is only meaningful over the load order it was made with. The engine already asks the player about
// content files the save needs and the session lacks; it does not notice the same files in another ORDER, which gives
// the same form ids different meanings. The chunk records the plugin file names in order and refuses a load whose
// session lists them differently (the host then refuses the load cleanly, never half applied).
//
// This is also the smallest user of the chunk API that the Papyrus wiring (task 0176) uses for its script state:
// register a SaveChunkHandler from start() through Host::getSaveChunkService(), write in write(), check in read().
// Header only; no engine type; main thread (the host calls write and read there).

namespace game_skyrimse
{
    class LoadOrderStamp final : public openeden::sdk::SaveChunkHandler
    {
    public:
        static constexpr std::uint32_t kVersion = 1;
        static constexpr std::uint32_t kMaxPlugins = 4096; // the engine's own limit is far lower; a hostile cap
        static constexpr std::string_view kName = "loadorder";

        /// The session's plugin file names (not paths), in load order.
        void setLoadOrder(std::vector<std::string> names) { mNames = std::move(names); }
        [[nodiscard]] const std::vector<std::string>& loadOrder() const { return mNames; }

        /// Called with the reason when a read refuses (the pack logs it: the host's own message only names the chunk).
        void setRefusalSink(std::function<void(const std::string&)> sink) { mSink = std::move(sink); }

        /// Why the last read refused, empty when it did not.
        [[nodiscard]] const std::string& refusal() const { return mRefusal; }

        std::string_view chunkName() const override { return kName; }
        std::uint32_t chunkVersion() const override { return kVersion; }

        void write(openeden::sdk::ChunkWriter& writer) override
        {
            writer.writeValue(static_cast<std::uint32_t>(mNames.size()));
            for (const std::string& name : mNames)
                writer.writeString(name);
        }

        bool read(openeden::sdk::ChunkReader& reader, std::uint32_t version) override
        {
            mRefusal.clear();
            if (version != kVersion)
                return refuse("the load order stamp has version " + std::to_string(version));
            std::uint32_t count = 0;
            if (!reader.readValue(count) || count > kMaxPlugins)
                return refuse("the load order stamp is damaged");
            std::vector<std::string> saved;
            saved.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::string name;
                if (!reader.readString(name))
                    return refuse("the load order stamp is damaged");
                saved.push_back(std::move(name));
            }
            if (reader.remaining() != 0)
                return refuse("the load order stamp has bytes after its end");
            const std::string difference = describeDifference(saved, mNames);
            if (!difference.empty())
                return refuse("this save was made with another load order: " + difference);
            return true;
        }

        /// "" when the two lists are the same names in the same order (compared case-insensitively, as the engine
        /// compares content files), else the first difference.
        [[nodiscard]] static std::string describeDifference(
            const std::vector<std::string>& saved, const std::vector<std::string>& now)
        {
            const std::size_t common = saved.size() < now.size() ? saved.size() : now.size();
            for (std::size_t i = 0; i < common; ++i)
                if (!equalNoCase(saved[i], now[i]))
                    return "position " + std::to_string(i + 1) + " was '" + saved[i] + "', it is '" + now[i] + "'";
            if (saved.size() != now.size())
                return std::to_string(saved.size()) + " plugins were loaded, there are " + std::to_string(now.size());
            return {};
        }

    private:
        static bool equalNoCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                if (lower(a[i]) != lower(b[i]))
                    return false;
            }
            return true;
        }

        bool refuse(std::string reason)
        {
            mRefusal = std::move(reason);
            if (mSink)
                mSink(mRefusal);
            return false;
        }

        std::vector<std::string> mNames;
        std::string mRefusal;
        std::function<void(const std::string&)> mSink;
    };
}
