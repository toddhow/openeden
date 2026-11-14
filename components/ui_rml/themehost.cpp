#include "themehost.hpp"

#include <algorithm>
#include <cctype>

namespace UiRml
{
    ThemeHost::ThemeHost(ReadFile read)
        : mRead(std::move(read))
    {
    }

    void ThemeHost::setRequest(const ThemeRequest& request)
    {
        std::lock_guard lock(mMutex);
        mRequest = request;
    }

    ThemeRequest ThemeHost::request() const
    {
        std::lock_guard lock(mMutex);
        return mRequest;
    }

    bool ThemeHost::refresh()
    {
        ThemeRequest request;
        {
            std::lock_guard lock(mMutex);
            request = mRequest;
        }
        // Reading and validating happens outside the lock: the reader may take a while.
        ThemeAssembly assembly = assembleTheme(mRead, request);
        std::lock_guard lock(mMutex);
        const bool changed = !mAssembled || assembly.mText != mAssembly.mText;
        mAssembly = std::move(assembly);
        mAssembled = true;
        return changed;
    }

    bool ThemeHost::generate(const std::string& path, std::vector<char>& out)
    {
        std::string normalised = path;
        for (char& c : normalised)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (normalised != kBaseSheetPath)
            return false;
        bool assembled;
        {
            std::lock_guard lock(mMutex);
            assembled = mAssembled;
        }
        if (!assembled)
            refresh();
        std::lock_guard lock(mMutex);
        out.assign(mAssembly.mText.begin(), mAssembly.mText.end());
        return true;
    }

    ThemeAssembly ThemeHost::assembly() const
    {
        std::lock_guard lock(mMutex);
        return mAssembly;
    }
}
