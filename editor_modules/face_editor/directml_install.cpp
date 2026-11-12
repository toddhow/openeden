#include "directml_install.hpp"

#include "sha512.hpp"
#include "zip_entry.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace face_editor
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::size_t kChunk = 1u << 20;

        std::string withThousands(std::uint64_t n)
        {
            std::string digits = std::to_string(n), out;
            for (std::size_t i = 0; i < digits.size(); ++i)
            {
                if (i != 0 && (digits.size() - i) % 3 == 0)
                    out.push_back(',');
                out.push_back(digits[i]);
            }
            return out;
        }

        bool isLowerHex(const std::string& s, std::size_t length)
        {
            return s.size() == length
                && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
        }

        /// Where the package bytes come from; read() returns the count (0 = end) or -1 with \a error set.
        class Source
        {
        public:
            virtual ~Source() = default;
            virtual long long read(char* dst, std::size_t count, std::string& error) = 0;
        };

        class FileSource final : public Source
        {
        public:
            explicit FileSource(const fs::path& path)
                : mIn(path, std::ios::binary)
            {
            }
            bool ok() const { return static_cast<bool>(mIn); }
            long long read(char* dst, std::size_t count, std::string& error) override
            {
                mIn.read(dst, static_cast<std::streamsize>(count));
                if (mIn.bad())
                {
                    error = "the package file could not be read";
                    return -1;
                }
                return static_cast<long long>(mIn.gcount());
            }

        private:
            std::ifstream mIn;
        };

#ifdef _WIN32
        std::wstring widen(const std::string& s)
        {
            if (s.empty())
                return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<std::size_t>(std::max(n, 0)), L'\0');
            if (n > 0)
                MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        class HttpSource final : public Source
        {
        public:
            ~HttpSource() override
            {
                if (mRequest != nullptr)
                    WinHttpCloseHandle(mRequest);
                if (mConnect != nullptr)
                    WinHttpCloseHandle(mConnect);
                if (mSession != nullptr)
                    WinHttpCloseHandle(mSession);
            }

            bool open(const std::string& url, std::string& error)
            {
                const std::wstring wide = widen(url);
                URL_COMPONENTS parts{};
                parts.dwStructSize = sizeof(parts);
                parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength
                    = static_cast<DWORD>(-1);
                if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts))
                {
                    error = "the package URL is not valid";
                    return false;
                }
                const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
                std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
                path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
                const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

                mSession = WinHttpOpen(L"OpenEden Eden Studio", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
                if (mSession == nullptr)
                    mSession = WinHttpOpen(L"OpenEden Eden Studio", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
                if (mSession == nullptr)
                {
                    error = "WinHTTP could not start (error " + std::to_string(GetLastError()) + ")";
                    return false;
                }
                WinHttpSetTimeouts(mSession, 15000, 15000, 30000, 60000);
                mConnect = WinHttpConnect(mSession, host.c_str(), parts.nPort, 0);
                if (mConnect == nullptr)
                {
                    error = "cannot connect (error " + std::to_string(GetLastError()) + ")";
                    return false;
                }
                mRequest = WinHttpOpenRequest(mConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                    WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
                if (mRequest == nullptr)
                {
                    error = "cannot start the request (error " + std::to_string(GetLastError()) + ")";
                    return false;
                }
                // Certificate checks stay on (the default); redirects are followed, never from https to http.
                if (!WinHttpSendRequest(mRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                    || !WinHttpReceiveResponse(mRequest, nullptr))
                {
                    error = "the download failed (error " + std::to_string(GetLastError()) + ")";
                    return false;
                }
                DWORD status = 0, size = sizeof(status);
                if (!WinHttpQueryHeaders(mRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)
                    || status != 200)
                {
                    error = "the server answered " + std::to_string(status) + ", not 200";
                    return false;
                }
                return true;
            }

            long long read(char* dst, std::size_t count, std::string& error) override
            {
                DWORD got = 0;
                if (!WinHttpReadData(mRequest, dst, static_cast<DWORD>(std::min<std::size_t>(count, 1u << 20)), &got))
                {
                    error = "the download was interrupted (error " + std::to_string(GetLastError()) + ")";
                    return -1;
                }
                return static_cast<long long>(got);
            }

        private:
            HINTERNET mSession = nullptr, mConnect = nullptr, mRequest = nullptr;
        };
#endif

        std::string lowerCase(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        /// The host part of "http://host[:port]/...".
        std::string httpHost(const std::string& url)
        {
            const std::size_t start = url.find("://") + 3;
            std::size_t end = url.find_first_of("/?#", start);
            std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (authority.find('@') != std::string::npos)
                return "@"; // credentials in the URL: never loopback
            if (!authority.empty() && authority.front() == '[')
            {
                const std::size_t close = authority.find(']');
                return close == std::string::npos ? "@" : authority.substr(0, close + 1);
            }
            const std::size_t colon = authority.find(':');
            return lowerCase(colon == std::string::npos ? authority : authority.substr(0, colon));
        }

        std::unique_ptr<Source> openSource(const std::string& url, std::string& error)
        {
            const std::string lower = lowerCase(url.substr(0, 8));
            if (lower.rfind("file:///", 0) == 0)
            {
                std::string rest = url.substr(8);
                // file:///C:/dir -> C:/dir ; file:///dir -> /dir
                if (!(rest.size() >= 2 && std::isalpha(static_cast<unsigned char>(rest[0])) && rest[1] == ':'))
                    rest = "/" + rest;
                auto file = std::make_unique<FileSource>(fs::path(rest));
                if (!file->ok())
                {
                    error = "the package file cannot be opened: " + rest;
                    return nullptr;
                }
                return file;
            }
            const bool https = lower.rfind("https://", 0) == 0;
            const bool http = lowerCase(url.substr(0, 7)) == "http://";
            if (!https && !http)
            {
                error = "the package URL must be https (or file:///): " + url;
                return nullptr;
            }
            if (http)
            {
                const std::string host = httpHost(url);
                if (host != "127.0.0.1" && host != "localhost" && host != "[::1]")
                {
                    error = "plain http is only accepted for this machine, not for " + host;
                    return nullptr;
                }
            }
#ifdef _WIN32
            auto source = std::make_unique<HttpSource>();
            if (!source->open(url, error))
                return nullptr;
            return source;
#else
            error = "downloading is only built for Windows";
            return nullptr;
#endif
        }

        /// Removes what a failed install made: the part files, and the folder when this call created it.
        struct Cleanup
        {
            fs::path mDir, mPackage, mDllPart;
            bool mCreatedDir = false;
            bool mDone = false;
            ~Cleanup()
            {
                std::error_code ec;
                fs::remove(mPackage, ec);
                fs::remove(mDllPart, ec);
                if (!mDone && mCreatedDir)
                    fs::remove(mDir, ec); // only succeeds when empty
            }
        };
    }

    const DirectMlPackage& pinnedDirectMlPackage()
    {
        // Microsoft.AI.DirectML 1.15.4: the version Microsoft.ML.OnnxRuntime.DirectML 1.24.4 declares. The size and
        // SHA-512 are those of the NuGet package (docs/dependencies.md, "Task 0147 slice 2").
        static const DirectMlPackage sPackage = [] {
            DirectMlPackage p;
            p.mVersion = "1.15.4";
            p.mUrl = "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4";
            p.mSha512 = "fde767f56904abc90fd53f65d8729c918ab7f6e3c5e1ecdd479908fc02b4535cf2b0860f7ab2acb9b731d6cb809b72c3d5d4d"
                        "02853fb8f5ea022a47bc44ef285";
            p.mSize = 202292617;
            p.mEntry = "bin/x64-win/DirectML.dll";
            p.mMaxEntryBytes = 64u << 20; // the real DLL is 18.5 MB
            return p;
        }();
        return sPackage;
    }

    fs::path directMlDllPath(const fs::path& userData, const DirectMlPackage& package)
    {
        if (userData.empty() || package.mVersion.empty())
            return {};
        return userData / "openeden" / "directml" / package.mVersion / "DirectML.dll";
    }

    fs::path directMlDllPath(const fs::path& userData)
    {
        return directMlDllPath(userData, pinnedDirectMlPackage());
    }

    bool directMlInstalled(const fs::path& userData, const DirectMlPackage& package)
    {
        const fs::path dll = directMlDllPath(userData, package);
        std::error_code ec;
        return !dll.empty() && fs::is_regular_file(dll, ec);
    }

    bool directMlInstalled(const fs::path& userData)
    {
        return directMlInstalled(userData, pinnedDirectMlPackage());
    }

    std::string directMlConfirmationText(const fs::path& userData)
    {
        return directMlConfirmationText(userData, pinnedDirectMlPackage());
    }

    std::string directMlConfirmationText(const fs::path& userData, const DirectMlPackage& p)
    {
        const fs::path dll = directMlDllPath(userData, p);
        return "DirectML lets the Audio2Face networks run on the graphics card. It is optional: without it Generate runs "
               "on the CPU (slower, the same curves within rounding).\n\n"
               "Studio will download the Microsoft.AI.DirectML " + p.mVersion + " package from nuget.org ("
            + withThousands(p.mSize) + " bytes, about " + std::to_string((p.mSize + (1u << 20) - 1) / (1u << 20))
            + " MiB), check its SHA-512 against the value recorded in Studio, and keep only DirectML.dll (about 18.5 MB) in "
              "\n" + dll.parent_path().generic_string()
            + "\nThe rest of the package is deleted.\n\n"
              "Licence: Microsoft Software License Terms, Microsoft DirectX Machine Learning (DirectML). It is Microsoft's "
              "proprietary licence, not an open-source one, and it says the software may collect information and send it "
              "to Microsoft (you can opt out of many of these scenarios, but not all). The full text is in the package "
              "(LICENSE.txt) and on https://www.nuget.org/packages/Microsoft.AI.DirectML/" + p.mVersion
            + ". By clicking Download you accept those terms.";
    }

    bool installDirectMl(const DirectMlPackage& package, const fs::path& destDir, const InstallProgress& progress,
        std::string& error)
    {
        error.clear();
        auto step = [&](double fraction, std::string_view text) {
            if (progress && !progress(std::clamp(fraction, 0.0, 1.0), text))
            {
                error = "cancelled";
                return false;
            }
            return true;
        };
        if (!isLowerHex(package.mSha512, 128) || package.mSize == 0 || package.mEntry.empty()
            || package.mMaxEntryBytes == 0 || destDir.empty())
        {
            error = "the package description is not valid";
            return false;
        }

        std::unique_ptr<Source> source = openSource(package.mUrl, error);
        if (!source)
            return false;

        Cleanup cleanup;
        cleanup.mDir = destDir;
        std::error_code ec;
        cleanup.mCreatedDir = !fs::exists(destDir, ec);
        fs::create_directories(destDir, ec);
        if (ec)
        {
            error = "cannot create " + destDir.generic_string() + ": " + ec.message();
            return false;
        }
        cleanup.mPackage = destDir / "package.part";
        const std::string dllName = fs::path(package.mEntry).filename().generic_string();
        cleanup.mDllPart = destDir / (dllName + ".part");

        if (!step(0.0, "downloading"))
            return false;
        Sha512 hash;
        std::uint64_t total = 0;
        {
            std::ofstream out(cleanup.mPackage, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                error = "cannot write " + cleanup.mPackage.generic_string();
                return false;
            }
            std::vector<char> buffer(kChunk);
            for (;;)
            {
                const long long got = source->read(buffer.data(), buffer.size(), error);
                if (got < 0)
                    return false;
                if (got == 0)
                    break;
                total += static_cast<std::uint64_t>(got);
                if (total > package.mSize)
                {
                    error = "the download is larger than the " + std::to_string(package.mSize)
                        + " bytes the pinned package has; refused";
                    return false;
                }
                hash.update(buffer.data(), static_cast<std::size_t>(got));
                out.write(buffer.data(), got);
                if (!out)
                {
                    error = "cannot write " + cleanup.mPackage.generic_string() + " (disk full?)";
                    return false;
                }
                if (!step(0.85 * static_cast<double>(total) / static_cast<double>(package.mSize), "downloading"))
                    return false;
            }
            out.flush();
            if (!out)
            {
                error = "cannot write " + cleanup.mPackage.generic_string();
                return false;
            }
        }
        if (total != package.mSize)
        {
            error = "the download ended after " + std::to_string(total) + " of " + std::to_string(package.mSize)
                + " bytes; refused";
            return false;
        }
        if (!step(0.87, "checking the package"))
            return false;
        const std::string digest = hash.finishHex();
        if (digest != package.mSha512)
        {
            error = "the package's SHA-512 (" + digest.substr(0, 16) + "...) is not the pinned one ("
                + package.mSha512.substr(0, 16) + "...); refused";
            return false;
        }

        if (!step(0.9, "extracting " + dllName))
            return false;
        std::vector<char> dll;
        if (!readZipEntry(cleanup.mPackage, package.mEntry, package.mMaxEntryBytes, dll, error))
            return false;
        if (dll.size() < 2 || dll[0] != 'M' || dll[1] != 'Z')
        {
            error = package.mEntry + " is not a Windows DLL; refused";
            return false;
        }
        {
            std::ofstream out(cleanup.mDllPart, std::ios::binary | std::ios::trunc);
            out.write(dll.data(), static_cast<std::streamsize>(dll.size()));
            out.flush();
            if (!out)
            {
                error = "cannot write " + cleanup.mDllPart.generic_string();
                return false;
            }
        }
        fs::rename(cleanup.mDllPart, destDir / dllName, ec);
        if (ec)
        {
            error = "cannot put " + dllName + " in place: " + ec.message();
            return false;
        }
        cleanup.mDone = true;
        step(1.0, "done");
        return true;
    }
}
