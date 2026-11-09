#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_MODEL_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_MODEL_H

// The mod library and the profiles of the launcher's mod organizer (task 0057, stage a). Two JSON files in canonical
// text, "openeden.mods" v1 and "openeden.profile" v1: sorted keys, two-space indent, one trailing newline, so that
// write(read(text)) is byte-identical to canonical text. No Qt, no game types.
//
// Threading: every function here is pure and may be called from any thread.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ModOrganizer
{
    namespace Limits
    {
        inline constexpr std::size_t sMaxFileBytes = 8u * 1024 * 1024;
        inline constexpr std::size_t sMaxMods = 10000;
        inline constexpr std::size_t sMaxPlugins = 10000;
        inline constexpr std::size_t sMaxListEntries = 10000;
        inline constexpr std::size_t sMaxPathBytes = 4096;
        inline constexpr std::size_t sMaxTextBytes = 4096;
        inline constexpr std::size_t sMaxNameBytes = 256;
    }

    enum class ModKind
    {
        Folder,
        Pack,
    };

    /// A place on disk that adds files to the game. The files stay where the user put them.
    struct Mod
    {
        std::string mId; // slug, see isValidModId
        std::string mName;
        ModKind mKind = ModKind::Folder;
        std::string mRoot; // absolute path as the user gave it, UTF-8, '/' separators
        std::string mDataPath; // relative sub-path of the data root, or empty
        std::string mNotes;

        bool operator==(const Mod&) const = default;
    };

    struct ModLibrary
    {
        std::vector<Mod> mMods;

        const Mod* find(std::string_view id) const;
        bool operator==(const ModLibrary&) const = default;
    };

    struct ProfileMod
    {
        std::string mId;
        bool mEnabled = true;
        bool mUseRepack = false;

        bool operator==(const ProfileMod&) const = default;
    };

    struct PluginEntry
    {
        std::string mName;
        bool mActive = true;

        bool operator==(const PluginEntry&) const = default;
    };

    /// Order in every list is priority order, later wins (the openmw.cfg convention).
    struct Profile
    {
        std::string mName;
        std::vector<ProfileMod> mMods;
        std::vector<PluginEntry> mPlugins;
        std::vector<std::string> mArchives;
        std::vector<std::string> mGroundcover;
        std::vector<std::string> mExtensions;

        bool operator==(const Profile&) const = default;
    };

    /// "file: pointer: message", the pointer a JSON pointer ("/mods/3/root"), empty for the whole file. When the file
    /// has a higher major version than this reader knows, mNewerVersion is set: show it read-only, never overwrite.
    struct LoadError
    {
        std::string mFile;
        std::size_t mLine = 0; // JSON syntax errors only
        std::string mPointer;
        std::string mMessage;
        bool mNewerVersion = false;

        std::string format() const;
    };

    /// Lower case letters, digits, '.', '_' and '-', first character a letter or digit, 1 to 64 bytes.
    bool isValidModId(std::string_view id);
    /// Turns a display name into a valid id that is not in `taken` (adds -2, -3, ... when needed).
    std::string makeModId(std::string_view name, const std::vector<std::string>& taken);
    /// A profile name is a file name: 1 to 64 bytes, plain text, nothing that fails VFS::Path::checkExtractionSafe,
    /// no '/'.
    bool isValidProfileName(std::string_view name);

    std::optional<ModLibrary> readModLibrary(std::string_view text, std::string_view fileName, LoadError& error);
    std::string writeModLibrary(const ModLibrary& library);

    std::optional<Profile> readProfile(std::string_view text, std::string_view fileName, LoadError& error);
    std::string writeProfile(const Profile& profile);
}

#endif
