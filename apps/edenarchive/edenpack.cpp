// edenpack: packs a folder into an Eden Archive (.edenpak). Same folder, same options, same bytes.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <edenarchive/writer.hpp>

#include "toolcommon.hpp"

namespace
{
    int usage()
    {
        std::fprintf(stderr,
            "usage: edenpack [--store|--optimise] [--level N] [--chunk-size N] [--threads N]\n"
            "                [--derived --generation N --source-digest HEX64] [--force] [--normalize-names]\n"
            "                <input-folder> <output.edenpak>\n"
            "Packs every regular file below the folder. Names must already be normalized (lower case, no backslash)\n"
            "unless --normalize-names is given. The default mode is --store (chunked and hashed, never recompressed).\n"
            "The input files are never changed. Exit: 0 done, 1 refused or invalid input, 2 usage or I/O error.\n");
        return EdenTools::exitUsage;
    }
}

static int run(int argc, char** argv)
{
    using namespace EdenTools;
    EdenArchive::WriteOptions options;
    options.mStoreOnly = true;
    bool force = false;
    bool normalizeNames = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto number = [&](std::uint64_t& value) {
            return i + 1 < argc && parseUnsigned(argv[++i], value);
        };
        std::uint64_t value = 0;
        if (arg == "--store")
            options.mStoreOnly = true;
        else if (arg == "--optimise" || arg == "--optimize")
            options.mStoreOnly = false;
        else if (arg == "--level")
        {
            if (!number(value) || value > 22)
                return usage();
            options.mZstdLevel = static_cast<int>(value);
        }
        else if (arg == "--chunk-size")
        {
            if (!number(value) || value > 0xFFFFFFFFull)
                return usage();
            options.mChunkSize = static_cast<std::uint32_t>(value);
        }
        else if (arg == "--threads")
        {
            if (!number(value) || value > 1024)
                return usage();
            options.mThreads = static_cast<unsigned>(value);
        }
        else if (arg == "--derived")
            options.mDerived = true;
        else if (arg == "--generation")
        {
            if (!number(value))
                return usage();
            options.mGeneration = value;
        }
        else if (arg == "--source-digest")
        {
            if (i + 1 >= argc || !parseHash(argv[++i], options.mSourceDigest))
                return usage();
        }
        else if (arg == "--force")
            force = true;
        else if (arg == "--normalize-names")
            normalizeNames = true;
        else if (arg.size() > 1 && arg[0] == '-' && arg[1] == '-')
            return usage();
        else
            positional.push_back(arg);
    }
    if (positional.size() != 2)
        return usage();
    if (!options.mDerived && (options.mGeneration != 0 || options.mSourceDigest != EdenArchive::Hash{}))
    {
        std::fprintf(stderr, "edenpack: --generation and --source-digest need --derived\n");
        return exitUsage;
    }

    const std::filesystem::path input = pathFromUtf8(positional[0]);
    const std::filesystem::path output = pathFromUtf8(positional[1]);
    std::error_code ec;
    if (!std::filesystem::is_directory(input, ec))
    {
        std::fprintf(stderr, "edenpack: '%s' is not a folder\n", positional[0].c_str());
        return exitUsage;
    }
    if (std::filesystem::exists(output, ec) && !force)
    {
        std::fprintf(stderr, "edenpack: '%s' exists; use --force to replace it\n", positional[1].c_str());
        return exitInvalid;
    }

    // Collect the files. The order does not matter: the writer sorts the entries.
    std::vector<EdenArchive::InputEntry> entries;
    std::map<std::string, std::string> seen; // stored name -> file on disk, to find two files with one name
    std::filesystem::recursive_directory_iterator iterator(input, std::filesystem::directory_options::none, ec);
    if (ec)
    {
        std::fprintf(stderr, "edenpack: cannot read '%s': %s\n", positional[0].c_str(), ec.message().c_str());
        return exitUsage;
    }
    for (; iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(ec))
    {
        if (ec)
        {
            std::fprintf(stderr, "edenpack: cannot read the folder: %s\n", ec.message().c_str());
            return exitUsage;
        }
        const std::filesystem::directory_entry& item = *iterator;
        // The output (and the temporary name the writer uses) may lie inside the folder: never pack it.
        std::filesystem::path temporaryOutput = output;
        temporaryOutput += ".tmp";
        std::error_code sameError;
        if (std::filesystem::equivalent(item.path(), output, sameError) || item.path() == temporaryOutput)
            continue;
        std::error_code statusError;
        const std::filesystem::file_status status = item.symlink_status(statusError);
        if (statusError || status.type() != std::filesystem::file_type::regular)
            continue; // folders are implied by the names; links and special files are not packed
        std::string name = pathToUtf8(item.path().lexically_relative(input));
        if (normalizeNames)
        {
            for (char& c : name)
            {
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            }
        }
        const auto inserted = seen.emplace(name, pathToUtf8(item.path()));
        if (!inserted.second)
        {
            std::fprintf(stderr, "edenpack: '%s' and '%s' would both be stored as '%s'\n", inserted.first->second.c_str(),
                pathToUtf8(item.path()).c_str(), name.c_str());
            return exitInvalid;
        }
        EdenArchive::InputEntry entry;
        entry.mPath = name;
        entry.mFile = item.path();
        entries.push_back(std::move(entry));
    }

    const EdenArchive::Error error = EdenArchive::writePack(entries, output, options);
    if (error)
    {
        // Name the input file when the error points at an entry (field "entry[N]...").
        std::string file;
        std::size_t index = 0;
        if (std::sscanf(error.mField.c_str(), "entry[%zu]", &index) == 1 && index < entries.size())
            file = pathToUtf8(entries[index].mFile);
        std::string text = error.describe();
        if (!file.empty())
            text += " (file " + file + ")";
        std::fprintf(stderr, "edenpack: %s\n", text.c_str());
        return error.mClass == EdenArchive::ErrorClass::Io ? exitUsage : exitInvalid;
    }
    std::printf("packed %zu files into %s\n", entries.size(), positional[1].c_str());
    return exitOk;
}

int main(int argc, char** argv)
{
    return EdenTools::runGuarded(&run, argc, argv);
}
