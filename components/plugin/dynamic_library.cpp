#include "dynamic_library.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <utility>

namespace openeden::plugin
{
    namespace
    {
#if defined(_WIN32)
        std::string getWindowsErrorMessage(DWORD errorCode)
        {
            if (errorCode == 0)
                return {};

            char buffer[512] = { 0 };
            const DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer), nullptr);

            if (len == 0)
                return "Windows error code: " + std::to_string(errorCode);

            // Strip trailing newlines
            std::string msg(buffer, len);
            while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' || msg.back() == ' '))
                msg.pop_back();

            return msg;
        }
#endif
    } // namespace

    DynamicLibrary::DynamicLibrary()
        : mHandle(nullptr)
    {
    }

    DynamicLibrary::DynamicLibrary(const std::filesystem::path& path, std::string& error)
        : mHandle(nullptr)
    {
        load(path, error);
    }

    DynamicLibrary::~DynamicLibrary()
    {
        unload();
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
        : mHandle(other.mHandle)
    {
        other.mHandle = nullptr;
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this != &other)
        {
            unload();
            mHandle = other.mHandle;
            other.mHandle = nullptr;
        }
        return *this;
    }

    bool DynamicLibrary::load(const std::filesystem::path& path, std::string& error)
    {
        unload();
        error.clear();

#if defined(_WIN32)
        const std::wstring widePath = path.wstring();
        HMODULE handle = LoadLibraryW(widePath.c_str());
        if (!handle)
        {
            error = getWindowsErrorMessage(GetLastError());
            if (error.empty())
                error = "Failed to load library: " + path.string();
            return false;
        }
        mHandle = static_cast<void*>(handle);
        return true;
#else
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            const char* dlErr = dlerror();
            error = dlErr ? dlErr : ("Failed to load library: " + path.string());
            return false;
        }
        mHandle = handle;
        return true;
#endif
    }

    void DynamicLibrary::unload()
    {
        if (mHandle)
        {
#if defined(_WIN32)
            FreeLibrary(static_cast<HMODULE>(mHandle));
#else
            dlclose(mHandle);
#endif
            mHandle = nullptr;
        }
    }

    bool DynamicLibrary::isLoaded() const noexcept
    {
        return mHandle != nullptr;
    }

    void* DynamicLibrary::getSymbol(const std::string& name, std::string& error) const
    {
        error.clear();

        if (!isLoaded())
        {
            error = "Library is not loaded";
            return nullptr;
        }

#if defined(_WIN32)
        FARPROC proc = GetProcAddress(static_cast<HMODULE>(mHandle), name.c_str());
        if (!proc)
        {
            error = getWindowsErrorMessage(GetLastError());
            if (error.empty())
                error = "Symbol not found: " + name;
            return nullptr;
        }
        return reinterpret_cast<void*>(proc);
#else
        dlerror(); // Clear existing errors
        void* sym = dlsym(mHandle, name.c_str());
        const char* dlErr = dlerror();
        if (dlErr != nullptr)
        {
            error = dlErr;
            return nullptr;
        }
        return sym;
#endif
    }
} // namespace openeden::plugin
