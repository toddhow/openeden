#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_CONFIGFILE_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_CONFIGFILE_H

// Writes a profile's lines into the user openmw.cfg (task 0057, stage b). Owned keys only (data, content,
// groundcover, fallback-archive, extension); every other line and every comment is kept where it was. The lines are
// put where the first old owned line was (or at the end). Nothing changed means nothing written. A backup copy is
// made first and the last few are kept. The global openmw.cfg is never written.
//
// Deviation from the spec text, which suggested reusing Config::GameSettings::writeFileWithComments: that function
// is Qt based and rewrites comments with its own policy for "ordered" lines; this pure version is testable without
// Qt and the launcher page calls it. Its policy is simpler: comments are never moved or removed.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ModOrganizer
{
    /// True for "key = value" lines of an owned key (leading blanks allowed, key case-sensitive like the engine).
    bool isOwnedLine(std::string_view line);
    /// The owned lines of a file's text, trimmed, in file order.
    std::vector<std::string> ownedLines(std::string_view text);

    struct RewriteResult
    {
        std::string mText;
        bool mChanged = false;
    };
    /// Pure text rewrite. The file's line ending style (LF or CRLF) and a missing final newline are kept.
    RewriteResult rewriteOwnedLines(std::string_view existing, const std::vector<std::string>& wanted);

    /// True when the file's owned lines differ from `expected`: someone edited it by hand (or it was never applied).
    bool differsFromProfile(std::string_view existing, const std::vector<std::string>& expected);

    struct ApplyOptions
    {
        int mKeepBackups = 5;
        /// The install's openmw.cfg: refused as a target when it is the same file.
        std::filesystem::path mGlobalConfig;
        /// Asked once before anything is written; true means the game is running and the call refuses.
        std::function<bool()> mGameRunning;
    };

    struct ApplyResult
    {
        bool mOk = false;
        bool mWritten = false;
        std::filesystem::path mBackup;
        std::string mError;
    };

    /// Reads `path` (a missing file counts as empty), rewrites it, and writes through a temporary name and a rename.
    ApplyResult applyToConfigFile(
        const std::filesystem::path& path, const std::vector<std::string>& wanted, const ApplyOptions& options);

    /// openmw.cfg.bak-<n> files next to `path`, oldest first.
    std::vector<std::filesystem::path> listBackups(const std::filesystem::path& path);
}

#endif
