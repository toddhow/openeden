#ifndef OPENEDEN_COMPONENTS_SETTINGS_NEUTRALDEFAULTS_H
#define OPENEDEN_COMPONENTS_SETTINGS_NEUTRALDEFAULTS_H

#include "categories.hpp"

#include <string>
#include <string_view>

namespace Settings
{
    // Task 0355 (follow-up): defaults that differ in a game pack's (neutral) session. Skyrim's lighting parity (the
    // cell's image space, task 0355 stage c) only shows with the HDR path, so a neutral session starts with
    // `[Rendering] hdr` on. Morrowind's default (`hdr = false`) is untouched: the rule needs `[Game] neutral world`, and
    // a value the user (or another config folder's settings.cfg, which counts as a changed default) wrote for `hdr`
    // always wins, true or false. The one place that reads this is Settings::Manager::load, after the user's file is
    // read and before the setting values are made, so nothing sees the old default.
    namespace NeutralDefaults
    {
        inline bool isTrue(std::string_view text)
        {
            return text == "true" || text == "1";
        }

        inline bool neutralWorldOn(
            const CategorySettingValueMap& defaults, const CategorySettingValueMap& user)
        {
            const auto key = std::make_pair(std::string_view("Game"), std::string_view("neutral world"));
            if (const auto it = user.find(key); it != user.end())
                return isTrue(it->second);
            if (const auto it = defaults.find(key); it != defaults.end())
                return isTrue(it->second);
            return false;
        }

        /// True when the neutral default of `category`/`key` applies: a neutral world, no user value for it, and the defaults
        /// (which include the other config folders' settings.cfg) still hold the original value of the shipped file.
        inline bool defaultApplies(std::string_view category, std::string_view key,
            const CategorySettingValueMap& defaults, const CategorySettingValueMap& user,
            const CategorySettingValueMap& originalDefaults)
        {
            const auto k = std::make_pair(category, key);
            if (!neutralWorldOn(defaults, user) || user.contains(k))
                return false;
            const auto now = defaults.find(k);
            const auto original = originalDefaults.find(k);
            if (now == defaults.end() || original == originalDefaults.end())
                return false;
            return now->second == original->second;
        }

        inline bool hdrDefaultApplies(const CategorySettingValueMap& defaults, const CategorySettingValueMap& user,
            const CategorySettingValueMap& originalDefaults)
        {
            return defaultApplies("Rendering", "hdr", defaults, user, originalDefaults);
        }

        /// Writes the neutral defaults into `defaults` (never into the user's settings: nothing is saved by it).
        /// Task 0361d: `[GUI] rmlui` on and its C++ debug panel off, so the pack's own HUD shows in a Skyrim session.
        inline void apply(CategorySettingValueMap& defaults, const CategorySettingValueMap& user,
            const CategorySettingValueMap& originalDefaults)
        {
            struct Rule
            {
                std::string_view mCategory, mKey, mValue;
            };
            static constexpr Rule rules[] = { { "Rendering", "hdr", "true" }, { "GUI", "rmlui", "true" },
                { "GUI", "rmlui debug panel", "false" } };
            for (const Rule& r : rules)
                if (defaultApplies(r.mCategory, r.mKey, defaults, user, originalDefaults))
                    defaults.find(std::make_pair(r.mCategory, r.mKey))->second = std::string(r.mValue);
        }
    }
}

#endif
