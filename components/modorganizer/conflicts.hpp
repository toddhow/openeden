#ifndef OPENEDEN_COMPONENTS_MODORGANIZER_CONFLICTS_H
#define OPENEDEN_COMPONENTS_MODORGANIZER_CONFLICTS_H

// The conflict view data (task 0057, stage c): a throw-away VFS::Manager built from a layout in the same order as
// VFS::registerArchives, asked who overrides whom, with every provider mapped back to a mod. Built without starting
// the game. Names are compared as the engine does (VFS::Path::Normalized). Threading: run on a worker thread.

#include "layout.hpp"

#include <string>
#include <vector>

namespace ModOrganizer
{
    struct ConflictFile
    {
        std::string mPath; // normalized
        std::string mWinner; // source label, see sourceModId
        std::vector<std::string> mLosers; // highest priority loser first
        bool mIdentical = false; // all providers have a stored content hash and they are equal
    };

    struct ModCounts
    {
        std::string mSource; // "mod:<id>", "base", "archive:<name>", "shim:<id>"
        std::size_t mOverridden = 0; // files of this source hidden by a later one
        std::size_t mOverriding = 0; // files of this source that hide earlier ones
        std::size_t mIdenticalDuplicates = 0;
    };

    struct ConflictReport
    {
        std::vector<ConflictFile> mFiles; // ordered by path
        std::vector<ModCounts> mCounts; // registration order
        std::vector<std::string> mErrors; // a source that could not be opened
    };

    /// Archives named in layout.mArchives are looked for in the layout data folders, last folder first, like
    /// Files::Collections::getPath. `includeIdentical` keeps sets whose stored hashes are all equal.
    ConflictReport computeConflicts(const Layout& layout, bool includeIdentical = false);

    /// The mod id behind a source label ("" for the base layer and for archives).
    std::string sourceModId(const std::string& source);

    /// Files that two layouts of the same mods give different winning MODS for (the repack swap proof, decision
    /// D5). A pack and a folder of one mod count as the same winner.
    std::vector<std::string> differentWinners(const Layout& a, const Layout& b);
}

#endif
