#ifndef CSM_MODULE_MODULESETTINGS_H
#define CSM_MODULE_MODULESETTINGS_H

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

namespace CSMModule
{
    /// Where module settings are kept, as text, by section and key (task 0091 stage e). The editor's implementation
    /// is Settings::Manager's user settings (eden-studio.cfg); tests and capture runs use MemorySettingsStore.
    class SettingsStore
    {
    public:
        virtual ~SettingsStore() = default;
        virtual std::optional<std::string> get(const std::string& section, const std::string& key) const = 0;
        virtual void set(const std::string& section, const std::string& key, const std::string& value) = 0;
    };

    class MemorySettingsStore final : public SettingsStore
    {
    public:
        std::optional<std::string> get(const std::string& section, const std::string& key) const override;
        void set(const std::string& section, const std::string& key, const std::string& value) override;

    private:
        std::map<std::pair<std::string, std::string>, std::string> mValues;
    };

    struct SettingEntry
    {
        std::string mModule;
        openeden::editor::SettingDesc mDesc;
        openeden::editor::PropertyValue mValue; ///< The current value, within the declared range.
    };

    /// The settings the modules registered (API 1.2, toddhow's answer 3: eden-studio.cfg under `[module.<id>]`,
    /// matching `[extension.<id>]`). Pure: no Qt.
    ///
    /// - add() validates a description (a non-empty id nobody else has, a type of Bool, Int, Float, String or Choice, a
    ///   default inside the declared range or among the choices) and reads the stored value: absent means the default
    ///   is WRITTEN to the store; a value that cannot be read, is not one of the choices or is outside the range falls
    ///   back to the default or is clamped, with ONE warning for that setting, and the repaired value is written back.
    /// - set() takes a value of exactly the setting's type (an Int setting takes a std::int64_t, a Float a double),
    /// clamps
    ///   a number into the range (warning once, as above), refuses a Choice that is not listed, and writes the store.
    /// - The values live in the store as text: bool as "true" or "false", numbers in the C locale with the fewest
    /// digits
    ///   that read back the same, text as it is.
    ///
    /// Ownership: owns the entries; borrows the store (which must outlive it, or setStore(nullptr) first). The default
    /// store is a MemorySettingsStore of its own. Main thread only.
    class ModuleSettings
    {
    public:
        using Warn = std::function<void(const std::string&)>;

        explicit ModuleSettings(Warn warn = {});
        // The built-in store's address is held: no copies.
        ModuleSettings(const ModuleSettings&) = delete;
        ModuleSettings& operator=(const ModuleSettings&) = delete;

        /// null: back to the built-in memory store. Entries added before keep their values.
        void setStore(SettingsStore* store);

        openeden::editor::Registration add(const std::string& moduleId, const openeden::editor::SettingDesc& desc);
        std::optional<openeden::editor::PropertyValue> get(std::string_view id) const;
        /// \return the value now held (clamped), or nothing if the id is unknown, the type differs or the choice is not
        /// listed (nothing changes).
        std::optional<openeden::editor::PropertyValue> set(
            std::string_view id, const openeden::editor::PropertyValue& value);

        const std::vector<SettingEntry>& entries() const { return mEntries; }
        /// Forgets a module's entries (the stored values stay in the file).
        void removeModule(const std::string& moduleId);

        /// Whether the store was written to (a default, a repair or a set) since the last call: the editor then saves
        /// the configuration file.
        bool takeWritten();

        static std::string sectionOf(const std::string& moduleId);

    private:
        SettingEntry* find(std::string_view id);
        const SettingEntry* find(std::string_view id) const;
        void warnOnce(const std::string& id, const std::string& message);
        void write(const SettingEntry& entry);

        Warn mWarn;
        MemorySettingsStore mMemory;
        SettingsStore* mStore;
        std::vector<SettingEntry> mEntries;
        std::set<std::string> mWarned;
        bool mWritten = false;
    };
}

#endif
