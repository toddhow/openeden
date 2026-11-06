#include "modulesettings.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace CSMModule
{
    using namespace openeden::editor;

    std::optional<std::string> MemorySettingsStore::get(const std::string& section, const std::string& key) const
    {
        const auto found = mValues.find({ section, key });
        if (found == mValues.end())
            return std::nullopt;
        return found->second;
    }

    void MemorySettingsStore::set(const std::string& section, const std::string& key, const std::string& value)
    {
        mValues[{ section, key }] = value;
    }

    namespace
    {
        std::string toText(const PropertyValue& value)
        {
            if (const bool* flag = std::get_if<bool>(&value))
                return *flag ? "true" : "false";
            if (const std::int64_t* integer = std::get_if<std::int64_t>(&value))
                return std::to_string(*integer);
            if (const double* number = std::get_if<double>(&value))
            {
                char buffer[64];
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *number);
                return std::string(buffer, result.ptr);
            }
            if (const std::string* text = std::get_if<std::string>(&value))
                return *text;
            return {};
        }

        std::optional<PropertyValue> fromText(PropertyType type, const std::string& text)
        {
            switch (type)
            {
                case PropertyType::Bool:
                    if (text == "true")
                        return PropertyValue(true);
                    if (text == "false")
                        return PropertyValue(false);
                    return std::nullopt;
                case PropertyType::Int:
                {
                    std::int64_t parsed = 0;
                    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
                    if (result.ec != std::errc() || result.ptr != text.data() + text.size())
                        return std::nullopt;
                    return PropertyValue(parsed);
                }
                case PropertyType::Float:
                {
                    double parsed = 0;
                    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
                    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !std::isfinite(parsed))
                        return std::nullopt;
                    return PropertyValue(parsed);
                }
                case PropertyType::String:
                case PropertyType::Choice:
                    return PropertyValue(text);
                case PropertyType::Vec3:
                    break;
            }
            return std::nullopt;
        }

        bool typeMatches(PropertyType type, const PropertyValue& value)
        {
            switch (type)
            {
                case PropertyType::Bool:
                    return std::holds_alternative<bool>(value);
                case PropertyType::Int:
                    return std::holds_alternative<std::int64_t>(value);
                case PropertyType::Float:
                    return std::holds_alternative<double>(value);
                case PropertyType::String:
                case PropertyType::Choice:
                    return std::holds_alternative<std::string>(value);
                case PropertyType::Vec3:
                    break;
            }
            return false;
        }

        /// The number clamped into [min, max]; \a changed says whether it moved.
        PropertyValue clamped(const PropertyDesc& property, const PropertyValue& value, bool& changed)
        {
            changed = false;
            if (const std::int64_t* integer = std::get_if<std::int64_t>(&value))
            {
                // The bounds are doubles: an Int range is exact up to 2^53.
                const double low
                    = std::max(property.min, static_cast<double>(std::numeric_limits<std::int64_t>::lowest()));
                const double high
                    = std::min(property.max, static_cast<double>(std::numeric_limits<std::int64_t>::max()));
                std::int64_t result = *integer;
                if (static_cast<double>(result) < low)
                    result = static_cast<std::int64_t>(std::ceil(low));
                else if (static_cast<double>(result) > high)
                    result = static_cast<std::int64_t>(std::floor(high));
                changed = result != *integer;
                return PropertyValue(result);
            }
            if (const double* number = std::get_if<double>(&value))
            {
                const double result = std::min(std::max(*number, property.min), property.max);
                changed = result != *number;
                return PropertyValue(result);
            }
            return value;
        }

        bool inRange(const PropertyDesc& property, const PropertyValue& value)
        {
            bool changed = false;
            clamped(property, value, changed);
            return !changed;
        }
    }

    std::string ModuleSettings::sectionOf(const std::string& moduleId)
    {
        return "module." + moduleId;
    }

    ModuleSettings::ModuleSettings(Warn warn)
        : mWarn(std::move(warn))
        , mStore(&mMemory)
    {
    }

    void ModuleSettings::setStore(SettingsStore* store)
    {
        mStore = store != nullptr ? store : &mMemory;
    }

    SettingEntry* ModuleSettings::find(std::string_view id)
    {
        for (SettingEntry& entry : mEntries)
            if (entry.mDesc.property.id == id)
                return &entry;
        return nullptr;
    }

    const SettingEntry* ModuleSettings::find(std::string_view id) const
    {
        for (const SettingEntry& entry : mEntries)
            if (entry.mDesc.property.id == id)
                return &entry;
        return nullptr;
    }

    void ModuleSettings::warnOnce(const std::string& id, const std::string& message)
    {
        if (!mWarned.insert(id).second)
            return;
        if (mWarn)
            mWarn(message);
    }

    void ModuleSettings::write(const SettingEntry& entry)
    {
        mStore->set(sectionOf(entry.mModule), entry.mDesc.property.id, toText(entry.mValue));
        mWritten = true;
    }

    Registration ModuleSettings::add(const std::string& moduleId, const SettingDesc& desc)
    {
        const PropertyDesc& property = desc.property;
        const auto refuse = [&](const std::string& why) {
            return Registration{ false, "setting '" + property.id + "': " + why };
        };

        if (property.id.empty())
            return { false, "a setting needs an id" };
        if (find(property.id) != nullptr)
            return refuse("this id is registered already");
        if (property.type == PropertyType::Vec3)
            return refuse("a Vec3 cannot be a setting");
        if (!typeMatches(property.type, desc.defaultValue))
            return refuse("the default value is not of the setting's type");
        if (property.type == PropertyType::Int || property.type == PropertyType::Float)
        {
            if (!(property.min <= property.max))
                return refuse("min is above max");
            if (const double* number = std::get_if<double>(&desc.defaultValue))
                if (!std::isfinite(*number))
                    return refuse("the default value is not finite");
            if (!inRange(property, desc.defaultValue))
                return refuse("the default value is outside the range");
        }
        if (property.type == PropertyType::Choice)
        {
            if (property.choices.empty())
                return refuse("a choice needs choices");
            const std::string& value = std::get<std::string>(desc.defaultValue);
            if (std::find(property.choices.begin(), property.choices.end(), value) == property.choices.end())
                return refuse("the default value is not one of the choices");
        }

        SettingEntry entry{ moduleId, desc, desc.defaultValue };
        const std::string section = sectionOf(moduleId);
        const std::optional<std::string> stored = mStore->get(section, property.id);
        bool repair = false;
        if (!stored)
        {
            repair = true; // the default is written
        }
        else if (const std::optional<PropertyValue> parsed = fromText(property.type, *stored))
        {
            bool changed = false;
            PropertyValue value = clamped(property, *parsed, changed);
            const bool listed = property.type != PropertyType::Choice
                || std::find(property.choices.begin(), property.choices.end(), std::get<std::string>(value))
                    != property.choices.end();
            if (!listed)
            {
                warnOnce(property.id,
                    "[" + section + "] " + property.id + " = " + *stored
                        + " is not one of the choices; using the default");
                repair = true;
            }
            else
            {
                entry.mValue = std::move(value);
                if (changed)
                {
                    warnOnce(property.id,
                        "[" + section + "] " + property.id + " = " + *stored + " is outside its range; using "
                            + toText(entry.mValue));
                    repair = true;
                }
            }
        }
        else
        {
            warnOnce(property.id,
                "[" + section + "] " + property.id + " = " + *stored + " cannot be read; using the default");
            repair = true;
        }
        if (repair)
            write(entry);
        mEntries.push_back(std::move(entry));
        return {};
    }

    std::optional<PropertyValue> ModuleSettings::get(std::string_view id) const
    {
        if (const SettingEntry* entry = find(id))
            return entry->mValue;
        return std::nullopt;
    }

    std::optional<PropertyValue> ModuleSettings::set(std::string_view id, const PropertyValue& value)
    {
        SettingEntry* entry = find(id);
        if (entry == nullptr)
            return std::nullopt;
        const PropertyDesc& property = entry->mDesc.property;
        if (!typeMatches(property.type, value))
            return std::nullopt;
        if (const double* number = std::get_if<double>(&value))
            if (!std::isfinite(*number))
                return std::nullopt;
        if (property.type == PropertyType::Choice
            && std::find(property.choices.begin(), property.choices.end(), std::get<std::string>(value))
                == property.choices.end())
            return std::nullopt;

        bool changed = false;
        entry->mValue = clamped(property, value, changed);
        if (changed)
            warnOnce(property.id,
                "setting " + property.id + " was set to " + toText(value) + ", outside its range; using "
                    + toText(entry->mValue));
        write(*entry);
        return entry->mValue;
    }

    void ModuleSettings::removeModule(const std::string& moduleId)
    {
        mEntries.erase(std::remove_if(mEntries.begin(), mEntries.end(),
                           [&](const SettingEntry& entry) { return entry.mModule == moduleId; }),
            mEntries.end());
    }

    bool ModuleSettings::takeWritten()
    {
        const bool written = mWritten;
        mWritten = false;
        return written;
    }
}
