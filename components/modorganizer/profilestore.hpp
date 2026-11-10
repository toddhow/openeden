#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_PROFILESTORE_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_PROFILESTORE_H

// The launcher's profile adapter (task 0057, the part the Mods page needs): where the mod library and the profiles
// live on disk, loading and saving them, importing the launcher's current openmw.cfg lines as a first profile
// (decision D3), the edit operations of the page (add, remove, enable, reorder) and applying the active profile to
// the user openmw.cfg with hand-edit detection (decision D4). No Qt: the page is a thin view on these.
//
// Files below <user config>/openeden/: mods.json, profiles/<name>.json, active-profile.txt, base-data.txt (the game
// data folders every profile starts with, shown locked) and applied/<name>.txt (the lines last written to
// openmw.cfg for that profile, to see a hand edit). Writes go through a temporary name and a rename. A file with a
// newer format version is read-only: it is shown, never overwritten.
//
// Threading: pure functions of the file system; the scan-based ones belong on a worker thread.

#include "configfile.hpp"
#include "layout.hpp"
#include "model.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ModOrganizer
{
    struct StorePaths
    {
        std::filesystem::path mFolder; // <user config>/openeden

        std::filesystem::path modsFile() const;
        std::filesystem::path profilesFolder() const;
        std::filesystem::path profileFile(const std::string& name) const;
        std::filesystem::path activeFile() const;
        std::filesystem::path baseDataFile() const;
        std::filesystem::path appliedFile(const std::string& name) const;
    };

    StorePaths storePaths(const std::filesystem::path& userConfigFolder);

    struct Store
    {
        ModLibrary mLibrary;
        std::vector<Profile> mProfiles; // sorted by name
        std::string mActive; // a name in mProfiles, or empty when there is none
        std::vector<std::string> mBaseData;
        /// Repacks the player made (mod id to shim and pack); filled by the repack runner, empty otherwise.
        std::map<std::string, RepackedMod> mRepacks;

        bool mLibraryReadOnly = false; // mods.json has a newer version
        std::vector<std::string> mReadOnlyProfiles; // names of profile files with a newer version
        std::vector<std::string> mProblems; // located errors of files that could not be read (those are skipped)

        Profile* findProfile(const std::string& name);
        const Profile* findProfile(const std::string& name) const;
        Profile* active();
        const Profile* active() const;
        bool isReadOnly(const std::string& profileName) const;
    };

    /// A missing folder or file is an empty store; an unreadable file is a problem, not an exception.
    Store loadStore(const StorePaths& paths);

    /// Writes what changed. Returns the error texts (empty when everything was written or nothing needed writing).
    /// A read-only file is not written.
    std::vector<std::string> saveStore(const StorePaths& paths, const Store& store);

    // ---- the first run -------------------------------------------------------------------------------------------

    /// The owned keys of the launcher's current settings, already unquoted.
    struct ConfigSnapshot
    {
        std::vector<std::string> mData; // data= lines in file order
        std::vector<std::string> mContent;
        std::vector<std::string> mArchives;
        std::vector<std::string> mGroundcover;
        std::vector<std::string> mExtensions;
        /// False when the installation's own openmw.cfg already names the game data: then every data= line of the
        /// user file is a mod and there is no locked base layer.
        bool mFirstDataIsBase = true;
    };

    /// Reads the owned keys of an openmw.cfg text (the user file) and undoes the quoting of its values. Later lines
    /// of a key follow earlier ones. Every other line is ignored.
    ConfigSnapshot snapshotFromConfigText(std::string_view text);

    /// Undoes the openmw.cfg quoting of one value: "..." with '&' escaping '"' and '&'; unquoted text is trimmed.
    std::string parseConfigValue(std::string_view value);

    struct ImportResult
    {
        ModLibrary mLibrary;
        Profile mProfile;
        std::vector<std::string> mBaseData;
    };

    /// The first data folder becomes the locked base layer, every other data line a mod (a path that ends in
    /// ".edenpak" is a pack), plugins keep their order and are active. Equal paths count once.
    ImportResult importFromConfig(const ConfigSnapshot& snapshot, const std::string& profileName);

    /// Fills an empty store with the import (profile "Default", active). Does nothing when a profile exists.
    bool importIfEmpty(Store& store, const ConfigSnapshot& snapshot);

    // ---- edits ---------------------------------------------------------------------------------------------------

    struct EditResult
    {
        bool mOk = false;
        std::string mId; // the mod added
        std::string mError;
        std::vector<std::string> mWarnings; // scan problems, truncation
    };

    /// Registers a folder in place (never copies, never writes into it). The data root is detected; plugins found
    /// there are appended to the active profile's plugin list when not listed yet. The mod joins the active profile
    /// enabled, last (highest priority). A folder that is already in the library (compared as the file system does)
    /// is refused.
    EditResult addFolderMod(Store& store, const std::filesystem::path& folder, const std::string& displayName = {});
    /// Same for an ".edenpak" file (checked with looksLikeEdenPack).
    EditResult addPackMod(Store& store, const std::filesystem::path& file, const std::string& displayName = {});

    /// Removes the mod from the library and every profile. Never touches files. False when it does not exist.
    bool removeMod(Store& store, const std::string& id);

    /// Moves entry `from` to position `to` (priority order, later wins). Out of range is refused (false).
    bool moveMod(Profile& profile, std::size_t from, std::size_t to);
    bool movePlugin(Profile& profile, std::size_t from, std::size_t to);
    bool setModEnabled(Profile& profile, const std::string& id, bool enabled);
    bool setModUseRepack(Profile& profile, const std::string& id, bool useRepack);
    bool setPluginActive(Profile& profile, const std::string& name, bool active);

    /// The plugin files of the enabled mods of a profile, later mods win a name; lookups ignore ASCII case.
    struct PluginInventory
    {
        std::map<std::string, std::string> mProvider; // key: lower-case name, value: id of the providing mod
        std::map<std::string, std::string> mSpelling; // key: lower-case name, value: the name as found
        bool mTruncated = false;
    };
    PluginInventory inventoryPlugins(const ModLibrary& library, const Profile& profile);

    /// Removes the profile's plugin entries that no enabled mod provides and `keep` does not list (the game's own
    /// plugins, which live in the base data). Returns the names removed.
    std::vector<std::string> dropMissingPlugins(
        Profile& profile, const PluginInventory& inventory, const std::vector<std::string>& keep);

    /// Appends the inventory's plugins that the profile does not list yet (inactive), in name order.
    std::size_t appendNewPlugins(Profile& profile, const PluginInventory& inventory);

    /// Reads the master names of a plugin file (ModOrganizer::readMasters in the launcher, a fake in tests).
    using MasterReader = std::function<std::vector<std::string>(const std::filesystem::path& file, std::string& error)>;

    struct PluginRow
    {
        std::string mName;
        bool mActive = true;
        std::string mProviderModId; // empty: the game data (base layer) or nothing
        bool mFromBase = false; // found in a base data folder
        std::filesystem::path mFile; // empty when the file was not found
        std::vector<std::string> mMasters;
        std::string mError; // reading the masters failed, or the file is missing
    };

    struct PluginReport
    {
        std::vector<PluginRow> mRows; // the profile's order
        std::vector<std::string> mBasePlugins; // plugin names found in the base data folders
        bool mTruncated = false;
    };

    /// Finds each listed plugin (the last enabled mod that has it wins, else a base data folder) and reads its
    /// masters. Walks folders and opens files: a worker thread.
    PluginReport buildPluginReport(const Store& store, const MasterReader& readMasters);

    // ---- profiles ------------------------------------------------------------------------------------------------

    /// A new profile, copied from `copyFrom` when that exists. False for an invalid or a used name.
    bool createProfile(Store& store, const std::string& name, const std::string& copyFrom = {});
    /// Not the last one, and not a read-only one. Another profile becomes active when it was the active one.
    bool deleteProfile(Store& store, const std::string& name);
    bool renameProfile(Store& store, const std::string& from, const std::string& to);

    // ---- the openmw.cfg ------------------------------------------------------------------------------------------

    LayoutOptions layoutOptions(const Store& store);
    Layout activeLayout(const Store& store);

    struct ApplyOutcome
    {
        bool mOk = false;
        bool mWritten = false;
        bool mHandEdited = false; // refused because the file was edited by hand since the last apply
        std::filesystem::path mBackup;
        std::string mError;
        std::vector<std::string> mProblems; // from the layout
        std::vector<std::string> mLines; // what was (or would be) written
    };

    /// Renders the active profile into `configFile` with backup, refuses while the game runs (options) and when the
    /// owned lines differ from the ones this profile's last apply wrote, unless `acceptHandEdits`.
    ApplyOutcome applyActiveProfile(const StorePaths& paths, const Store& store,
        const std::filesystem::path& configFile, const ApplyOptions& options, bool acceptHandEdits);
}

#endif
