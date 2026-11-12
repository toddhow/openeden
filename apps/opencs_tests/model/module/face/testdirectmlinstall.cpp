// Task 0147 slice 3: the DirectML download on request. Nothing here touches the network: sources are file:/// URLs of
// generated packages (and, gated, a loopback http server of the checker's own). The pinned package's numbers are
// pinned too.
#include "editor_modules/face_editor/directml_install.hpp"
#include "editor_modules/face_editor/sha512.hpp"

#include "ziphelper.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    using namespace face_editor;
    namespace fs = std::filesystem;

    constexpr const char* kEntry = "bin/x64-win/DirectML.dll";

    struct Fixture
    {
        explicit Fixture(const char* name)
            : mRoot(fs::temp_directory_path() / ("oe_face_dml_" + std::string(name)))
        {
            fs::remove_all(mRoot);
            fs::create_directories(mRoot);
            mDest = mRoot / "dest";
        }

        /// A package file with the entry "bin/x64-win/DirectML.dll" holding \a dll (and a licence next to it).
        DirectMlPackage makePackage(const std::string& dll, bool deflate = true, const char* entry = kEntry)
        {
            const std::string zip = ziphelper::makeZip({ { "LICENSE.txt", "licence text", false }, { entry, dll, deflate } });
            return packageFor(zip);
        }

        DirectMlPackage packageFor(const std::string& zipBytes)
        {
            const fs::path file = mRoot / "package.nupkg";
            ziphelper::writeFile(file, zipBytes);
            DirectMlPackage p;
            p.mVersion = "9.9.9";
            p.mUrl = "file:///" + file.generic_string();
            p.mSha512 = sha512Hex(zipBytes);
            p.mSize = zipBytes.size();
            p.mEntry = kEntry;
            p.mMaxEntryBytes = 64u << 20;
            return p;
        }

        std::set<std::string> destFiles() const
        {
            std::set<std::string> names;
            if (fs::exists(mDest))
                for (const auto& e : fs::recursive_directory_iterator(mDest))
                    names.insert(e.path().filename().generic_string());
            return names;
        }

        fs::path mRoot, mDest;
    };

    std::string readAll(const fs::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    TEST(DirectMlInstall, installsTheDllAndNothingElse)
    {
        Fixture f("ok");
        const std::string dll = "MZ" + ziphelper::noise(3 << 20, 5);
        const DirectMlPackage p = f.makePackage(dll);
        std::vector<double> fractions;
        std::string error;
        ASSERT_TRUE(installDirectMl(p, f.mDest, [&](double fr, std::string_view) {
            fractions.push_back(fr);
            return true;
        }, error)) << error;
        EXPECT_EQ(f.destFiles(), (std::set<std::string>{ "DirectML.dll" }));
        EXPECT_EQ(readAll(f.mDest / "DirectML.dll"), dll);
        ASSERT_FALSE(fractions.empty());
        for (std::size_t i = 1; i < fractions.size(); ++i)
            EXPECT_GE(fractions[i], fractions[i - 1]);
        EXPECT_GE(fractions.back(), 0.99);
        for (double fr : fractions)
            EXPECT_LE(fr, 1.0);
    }

    TEST(DirectMlInstall, aStoredEntryAlsoWorks)
    {
        Fixture f("stored");
        const DirectMlPackage p = f.makePackage("MZ-small", false);
        std::string error;
        ASSERT_TRUE(installDirectMl(p, f.mDest, {}, error)) << error;
        EXPECT_EQ(readAll(f.mDest / "DirectML.dll"), "MZ-small");
    }

    TEST(DirectMlInstall, aWrongHashIsRefusedAndLeavesNothing)
    {
        Fixture f("hash");
        DirectMlPackage p = f.makePackage("MZ-dll");
        p.mSha512[0] = p.mSha512[0] == '0' ? '1' : '0';
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_NE(error.find("SHA-512"), std::string::npos) << error;
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, aHashOfTheWrongCaseOrLengthIsRefused)
    {
        Fixture f("hashform");
        DirectMlPackage p = f.makePackage("MZ-dll");
        p.mSha512 = p.mSha512.substr(0, 100);
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, aTruncatedDownloadIsRefused)
    {
        Fixture f("trunc");
        DirectMlPackage p = f.makePackage("MZ" + ziphelper::noise(200000, 6));
        const std::string whole = readAll(f.mRoot / "package.nupkg");
        ziphelper::writeFile(f.mRoot / "package.nupkg", whole.substr(0, whole.size() / 2)); // hash and size stay the full ones
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_NE(error.find("ended"), std::string::npos) << error;
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, aSourceLargerThanThePinnedSizeIsRefused)
    {
        Fixture f("big");
        DirectMlPackage p = f.makePackage("MZ" + ziphelper::noise(100000, 7));
        p.mSize -= 10;
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_NE(error.find("larger"), std::string::npos) << error;
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, anArchiveWithoutTheDllIsRefusedEvenWithTheRightHash)
    {
        Fixture f("nodll");
        const DirectMlPackage p = f.packageFor(ziphelper::makeZip({ { "LICENSE.txt", "x", false }, { "bin/x86-win/DirectML.dll", "MZ", false } }));
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_NE(error.find("DirectML.dll"), std::string::npos) << error;
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, notAZipWithTheRightHashIsRefused)
    {
        Fixture f("notzip");
        const DirectMlPackage p = f.packageFor("this is not a zip file at all");
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, aDllThatDoesNotStartWithMzIsRefused)
    {
        Fixture f("nomz");
        const DirectMlPackage p = f.makePackage("<html>not a dll</html>");
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_NE(error.find("DLL"), std::string::npos) << error;
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, cancellingLeavesNothing)
    {
        Fixture f("cancel");
        const DirectMlPackage p = f.makePackage("MZ" + ziphelper::noise(6 << 20, 8), false);
        int calls = 0;
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, [&](double, std::string_view) { return ++calls < 3; }, error));
        EXPECT_EQ(error, "cancelled");
        EXPECT_GE(calls, 3);
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, aFailedReinstallLeavesAnExistingDllAlone)
    {
        Fixture f("keep");
        fs::create_directories(f.mDest);
        ziphelper::writeFile(f.mDest / "DirectML.dll", "MZ-old");
        DirectMlPackage p = f.makePackage("MZ-new");
        p.mSha512.back() = p.mSha512.back() == '0' ? '1' : '0';
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_EQ(readAll(f.mDest / "DirectML.dll"), "MZ-old");
        EXPECT_EQ(f.destFiles(), (std::set<std::string>{ "DirectML.dll" }));
        // A good reinstall replaces it.
        p = f.makePackage("MZ-new");
        ASSERT_TRUE(installDirectMl(p, f.mDest, {}, error)) << error;
        EXPECT_EQ(readAll(f.mDest / "DirectML.dll"), "MZ-new");
    }

    TEST(DirectMlInstall, aMissingSourceIsAnError)
    {
        Fixture f("nosource");
        DirectMlPackage p = f.makePackage("MZ");
        fs::remove(f.mRoot / "package.nupkg");
        std::string error;
        EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error));
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(f.destFiles().empty());
    }

    TEST(DirectMlInstall, onlyHttpsFileAndLoopbackHttpAreAccepted)
    {
        Fixture f("scheme");
        DirectMlPackage p = f.makePackage("MZ");
        for (const char* url : { "ftp://example.com/p.nupkg", "http://example.com/p.nupkg", "http://127.0.0.1.evil.example/p.nupkg",
                 "\\\\server\\share\\p.nupkg", "C:\\p.nupkg", "", "javascript:alert(1)" })
        {
            p.mUrl = url;
            std::string error;
            EXPECT_FALSE(installDirectMl(p, f.mDest, {}, error)) << url;
            EXPECT_FALSE(error.empty()) << url;
            EXPECT_TRUE(f.destFiles().empty());
        }
    }

    TEST(DirectMlInstall, theDllLivesUnderTheUserDataFolderByVersion)
    {
        const fs::path user = fs::path("U") / "data";
        EXPECT_EQ(directMlDllPath(user), user / "openeden" / "directml" / "1.15.4" / "DirectML.dll");
        DirectMlPackage p;
        p.mVersion = "2.0.0";
        EXPECT_EQ(directMlDllPath(user, p), user / "openeden" / "directml" / "2.0.0" / "DirectML.dll");
        EXPECT_TRUE(directMlDllPath({}).empty());
    }

    TEST(DirectMlInstall, installedMeansTheFileIsThere)
    {
        Fixture f("installed");
        EXPECT_FALSE(directMlInstalled(f.mRoot));
        ziphelper::writeFile(directMlDllPath(f.mRoot), "MZ");
        EXPECT_TRUE(directMlInstalled(f.mRoot));
        EXPECT_FALSE(directMlInstalled({}));
    }

    TEST(DirectMlInstall, theNumbersOfThePinnedPackage)
    {
        const DirectMlPackage& p = pinnedDirectMlPackage();
        EXPECT_EQ(p.mVersion, "1.15.4");
        EXPECT_EQ(p.mUrl, "https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/1.15.4");
        EXPECT_EQ(p.mSha512,
            "fde767f56904abc90fd53f65d8729c918ab7f6e3c5e1ecdd479908fc02b4535cf2b0860f7ab2acb9b731d6cb809b72c3d5d4d02853fb8f"
            "5ea022a47bc44ef285");
        EXPECT_EQ(p.mSize, 202292617u);
        EXPECT_EQ(p.mEntry, kEntry);
        EXPECT_GE(p.mMaxEntryBytes, 20u << 20);
    }

    TEST(DirectMlInstall, theConfirmationNamesTheSizeTheLicenceAndTheFolder)
    {
        const fs::path user = fs::temp_directory_path() / "oe_face_dml_confirm";
        const std::string text = directMlConfirmationText(user);
        EXPECT_NE(text.find("202,292,617"), std::string::npos) << text;
        EXPECT_NE(text.find("Microsoft Software License Terms"), std::string::npos) << text;
        EXPECT_NE(text.find("DirectML"), std::string::npos);
        EXPECT_NE(text.find("send"), std::string::npos) << "the telemetry clause is mentioned: " << text;
        EXPECT_NE(text.find(directMlDllPath(user).parent_path().generic_string()), std::string::npos) << text;
        EXPECT_NE(text.find("CPU"), std::string::npos) << "says the CPU path works without it";
        EXPECT_NE(text.find("nuget.org"), std::string::npos);
    }

    // ---- gated, with the real pinned package from the download cache ----------------------------------------------

    TEST(DirectMlInstall, theRealPinnedPackageInstallsFromAFileUrl)
    {
        const char* path = std::getenv("OPENEDEN_DML_PACKAGE");
        if (path == nullptr)
            GTEST_SKIP() << "OPENEDEN_DML_PACKAGE (Microsoft.AI.DirectML.1.15.4.nupkg) is not set";
        Fixture f("real");
        DirectMlPackage p = pinnedDirectMlPackage();
        p.mUrl = "file:///" + fs::path(path).generic_string();
        std::vector<double> fractions;
        std::string error;
        ASSERT_TRUE(installDirectMl(p, f.mDest, [&](double fr, std::string_view) {
            fractions.push_back(fr);
            return true;
        }, error)) << error;
        EXPECT_EQ(f.destFiles(), (std::set<std::string>{ "DirectML.dll" }));
        EXPECT_GT(fs::file_size(f.mDest / "DirectML.dll"), 10u << 20);
        EXPECT_GT(fractions.size(), 20u);
#ifdef _WIN32
        // The extracted file is a real DLL: it loads by its full path.
        HMODULE h = LoadLibraryExW((f.mDest / "DirectML.dll").c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        EXPECT_NE(h, nullptr) << "error " << GetLastError();
        if (h != nullptr)
            FreeLibrary(h);
#endif
        // With a corrupted copy of the real package: the real hash refuses it.
        const fs::path bad = f.mRoot / "bad.nupkg";
        fs::copy_file(path, bad);
        {
            std::fstream io(bad, std::ios::in | std::ios::out | std::ios::binary);
            io.seekp(1 << 20);
            io.put('!');
        }
        Fixture g("real2");
        p.mUrl = "file:///" + bad.generic_string();
        EXPECT_FALSE(installDirectMl(p, g.mDest, {}, error));
        EXPECT_NE(error.find("SHA-512"), std::string::npos) << error;
        EXPECT_TRUE(g.destFiles().empty());
    }

    // A loopback http server run by the checker (python -m http.server, a fake package): the WinHTTP path. The env
    // vars give the URL, the SHA-512 and the size of the fake package it serves.
    TEST(DirectMlInstall, aLoopbackHttpSourceInstalls)
    {
        const char* url = std::getenv("OPENEDEN_DML_HTTP_URL");
        const char* sha = std::getenv("OPENEDEN_DML_HTTP_SHA512");
        const char* size = std::getenv("OPENEDEN_DML_HTTP_SIZE");
        if (url == nullptr || sha == nullptr || size == nullptr)
            GTEST_SKIP() << "OPENEDEN_DML_HTTP_URL / _SHA512 / _SIZE are not set";
        Fixture f("http");
        DirectMlPackage p;
        p.mVersion = "9.9.9";
        p.mUrl = url;
        p.mSha512 = sha;
        p.mSize = std::strtoull(size, nullptr, 10);
        p.mEntry = kEntry;
        p.mMaxEntryBytes = 64u << 20;
        std::string error;
        ASSERT_TRUE(installDirectMl(p, f.mDest, {}, error)) << error;
        EXPECT_EQ(f.destFiles(), (std::set<std::string>{ "DirectML.dll" }));
        p.mSha512[3] = p.mSha512[3] == '0' ? '1' : '0';
        Fixture g("http2");
        EXPECT_FALSE(installDirectMl(p, g.mDest, {}, error));
        EXPECT_TRUE(g.destFiles().empty());
    }
}
