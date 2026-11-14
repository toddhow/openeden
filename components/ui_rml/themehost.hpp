#ifndef OPENEDEN_COMPONENTS_UI_RML_THEMEHOST_H
#define OPENEDEN_COMPONENTS_UI_RML_THEMEHOST_H

#include <mutex>
#include <string>
#include <vector>

#include "themesheet.hpp"

namespace UiRml
{
    /// The current theme of the overlay (task 0163): which theme and variants are wanted, the style sheet assembled
    /// from them, and whether a re-assembly changed anything. It is also the file interface's generator for
    /// `ui/base.rcss`. Owner: the overlay. Threads: every call may come from any thread (one mutex); in the game the
    /// GUI recording thread does all but `setRequest`.
    class ThemeHost
    {
    public:
        explicit ThemeHost(ReadFile read);

        /// What is wanted from now on; takes effect at the next refresh().
        void setRequest(const ThemeRequest& request);
        ThemeRequest request() const;

        /// Assembles again from the reader. True when the served text is not the one served before (always true the
        /// first time). Problems of an assembly are logged by the caller from assembly().
        bool refresh();

        /// Serves `ui/base.rcss` (case-insensitive, either slash); assembles first when nothing was assembled yet.
        /// False for every other path.
        bool generate(const std::string& path, std::vector<char>& out);

        ThemeAssembly assembly() const;

    private:
        ReadFile mRead;
        mutable std::mutex mMutex;
        ThemeRequest mRequest;
        ThemeAssembly mAssembly;
        bool mAssembled = false;
    };
}

#endif
