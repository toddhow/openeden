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

        /// True when the neutral default for `hdr` applies: a neutral world, no user value for `hdr`, and the defaults
        /// (which include the other config folders' settings.cfg) still hold the original value of the shipped file.
        inline bool hdrDefaultApplies(const CategorySettingValueMap& defaults, const CategorySettingValueMap& user,
            const CategorySettingValueMap& originalDefaults)
        {
            const auto key = std::make_pair(std::string_view("Rendering"), std::string_view("hdr"));
            if (!neutralWorldOn(defaults, user) || user.contains(key))
                return false;
            const auto now = defaults.find(key);
            const auto original = originalDefaults.find(key);
            if (now == defaults.end() || original == originalDefaults.end())
                return false;
            return now->second == original->second;
        }

        /// Writes the neutral defaults into `defaults` (never into the user's settings: nothing is saved by it).
        inline void apply(CategorySettingValueMap& defaults, const CategorySettingValueMap& user,
            const CategorySettingValueMap& originalDefaults)
        {
            if (hdrDefaultApplies(defaults, user, originalDefaults))
                defaults.find(std::make_pair(std::string_view("Rendering"), std::string_view("hdr")))->second = "true";
        }
    }
}

#endif
