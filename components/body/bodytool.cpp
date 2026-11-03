#include "bodytool.hpp"

#include "bodyjson.hpp"
#include "bodypack.hpp"
#include "bodyvalidate.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Body
{
    namespace
    {
        constexpr int sExitOk = 0;
        constexpr int sExitErrors = 1;
        constexpr int sExitCannotRead = 2;
        constexpr std::string_view sPackMagic = "EDENPAK"; // Eden Archive format, section 2
        constexpr std::string_view sBodyFileName = "body.json";

        void printUsage(std::ostream& err)
        {
            err << "usage: edenbodyvalidate [--json] <package>\n"
                   "  <package> is a body.json file, a package folder or a .edenpak.\n"
                   "  exit status: 0 no errors, 1 errors, 2 cannot read the input or bad arguments\n";
        }

        std::optional<std::string> readWholeFile(const std::filesystem::path& path)
        {
            std::error_code code;
            const std::uintmax_t size = std::filesystem::file_size(path, code);
            if (code || size > Limits::sMaxFileBytes)
            {
                return std::nullopt;
            }
            std::ifstream stream(path, std::ios::binary);
            std::string text(static_cast<std::size_t>(size), '\0');
            stream.read(text.data(), static_cast<std::streamsize>(text.size()));
            if (!stream || stream.gcount() != static_cast<std::streamsize>(text.size()))
            {
                return std::nullopt;
            }
            return text;
        }

        // Reads a file the package names. Refuses symbolic links and anything that resolves outside `base`, so a
        // package cannot make the tool read other files.
        std::optional<std::string> readPackageRelativeFile(
            const std::filesystem::path& base, const std::filesystem::path& relative)
        {
            std::error_code code;
            const std::filesystem::path path = base / relative;
            const std::filesystem::file_status status = std::filesystem::symlink_status(path, code);
            if (code || std::filesystem::is_symlink(status))
            {
                return std::nullopt;
            }
            const std::filesystem::path canonicalBase = std::filesystem::weakly_canonical(base, code);
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, code);
            if (code)
            {
                return std::nullopt;
            }
            const std::filesystem::path inside = canonicalPath.lexically_relative(canonicalBase);
            if (inside.empty() || *inside.begin() == "..")
            {
                return std::nullopt;
            }
            return readWholeFile(path);
        }

        bool looksLikePack(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            std::string head(sPackMagic.size(), '\0');
            stream.read(head.data(), static_cast<std::streamsize>(head.size()));
            return stream.gcount() == static_cast<std::streamsize>(head.size()) && head == sPackMagic;
        }

        std::string utf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        int report(const BodyPackage& package, const ValidateOptions& options, bool json, std::ostream& out)
        {
            const Report result = validateBodyPackage(package, options);
            out << (json ? formatReportJson(result) : formatReportText(result));
            return result.ok() ? sExitOk : sExitErrors;
        }

        int loadFailed(const BodyLoadResult& loaded, std::ostream& err)
        {
            err << loaded.mError.toString() << "\n";
            // A file that could not be read at all has line 0; one that does not parse or fails the format is an error
            // of the package.
            return loaded.mError.line == 0 ? sExitCannotRead : sExitErrors;
        }

        // body.json on disk; the files it names are read relative to `base`.
        int validateFile(const std::filesystem::path& bodyFile, const std::filesystem::path& base, bool json,
            std::ostream& out, std::ostream& err)
        {
            const BodyLoadResult loaded = readBodyPackageFile(bodyFile);
            if (!loaded.mValue)
            {
                return loadFailed(loaded, err);
            }
            ValidateOptions options;
            options.mReadFile = [base](std::string_view path) -> std::optional<std::string> {
                if (!isSafePackagePath(path))
                {
                    return std::nullopt;
                }
                const std::u8string relative(path.begin(), path.end());
                return readPackageRelativeFile(base, std::filesystem::path(relative));
            };
            return report(*loaded.mValue, options, json, out);
        }
    }

    int runBodyValidateTool(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)
    {
        try
        {
            bool json = false;
            std::optional<std::string> input;
            for (const std::string& arg : args)
            {
                if (arg == "--json")
                {
                    json = true;
                }
                else if (arg.size() >= 2 && arg.compare(0, 2, "--") == 0)
                {
                    err << "unknown option " << arg << "\n";
                    printUsage(err);
                    return sExitCannotRead;
                }
                else if (input)
                {
                    err << "only one package may be given\n";
                    printUsage(err);
                    return sExitCannotRead;
                }
                else
                {
                    input = arg;
                }
            }
            if (!input)
            {
                err << "no package given\n";
                printUsage(err);
                return sExitCannotRead;
            }

            const std::filesystem::path inputPath(*input);
            std::error_code code;
            if (std::filesystem::is_directory(inputPath, code))
            {
                const std::filesystem::path bodyFile = inputPath / std::string(sBodyFileName);
                if (!std::filesystem::is_regular_file(bodyFile, code))
                {
                    err << "'" << utf8(inputPath) << "' is not a body package: it has no body.json\n";
                    return sExitCannotRead;
                }
                return validateFile(bodyFile, inputPath, json, out, err);
            }
            if (!std::filesystem::is_regular_file(inputPath, code))
            {
                err << "cannot find '" << *input << "'\n";
                return sExitCannotRead;
            }
            if (!looksLikePack(inputPath))
            {
                return validateFile(inputPath, inputPath.parent_path(), json, out, err);
            }

            std::string error;
            const std::optional<PackageFiles> files = readPackageFile(inputPath, error);
            if (!files)
            {
                err << error << "\n";
                return sExitCannotRead;
            }
            const auto body = files->mFiles.find(std::string(sBodyFileName));
            if (body == files->mFiles.end())
            {
                err << "'" << utf8(inputPath) << "' is not a body package: the pack has no body.json\n";
                return sExitCannotRead;
            }
            const std::string text(body->second.begin(), body->second.end());
            const BodyLoadResult loaded = readBodyPackage(text, utf8(inputPath) + ":body.json");
            if (!loaded.mValue)
            {
                return loadFailed(loaded, err);
            }
            ValidateOptions options;
            options.mReadFile = [&files](std::string_view path) -> std::optional<std::string> {
                if (!isSafePackagePath(path))
                {
                    return std::nullopt;
                }
                const auto found = files->mFiles.find(std::string(path));
                if (found == files->mFiles.end())
                {
                    return std::nullopt;
                }
                return std::string(found->second.begin(), found->second.end());
            };
            return report(*loaded.mValue, options, json, out);
        }
        catch (const std::exception& e)
        {
            err << "failed: " << e.what() << "\n";
            return sExitCannotRead;
        }
    }
}
