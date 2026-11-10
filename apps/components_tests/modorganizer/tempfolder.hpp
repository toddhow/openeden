#ifndef OPENEDEN_TESTS_MODORGANIZER_TEMPFOLDER_H
#define OPENEDEN_TESTS_MODORGANIZER_TEMPFOLDER_H

#include <atomic>
#include <components/files/conversion.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace ModOrganizerTest
{
    /// A fresh folder below the system temp folder, removed with the object. Tests generate every mod they use.
    class TempFolder
    {
    public:
        TempFolder()
        {
            static std::atomic<int> counter{ 0 };
            mPath = std::filesystem::temp_directory_path()
                / ("oe_modorg_" + std::to_string(testing_pid()) + "_" + std::to_string(counter++));
            std::filesystem::remove_all(mPath);
            std::filesystem::create_directories(mPath);
        }
        ~TempFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mPath, ec);
        }
        TempFolder(const TempFolder&) = delete;
        TempFolder& operator=(const TempFolder&) = delete;

        const std::filesystem::path& path() const { return mPath; }

        /// Writes a file (parent folders are made) and returns its path.
        std::filesystem::path write(const std::string& relative, const std::string& content = "x") const
        {
            const std::filesystem::path file = mPath / Files::pathFromUnicodeString(relative);
            std::filesystem::create_directories(file.parent_path());
            std::ofstream out(file, std::ios::binary);
            out << content;
            return file;
        }

        static std::string read(const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

    private:
        static int testing_pid();
        std::filesystem::path mPath;
    };
}

#ifdef _WIN32
#include <process.h>
inline int ModOrganizerTest::TempFolder::testing_pid()
{
    return _getpid();
}
#else
#include <unistd.h>
inline int ModOrganizerTest::TempFolder::testing_pid()
{
    return getpid();
}
#endif

#endif
