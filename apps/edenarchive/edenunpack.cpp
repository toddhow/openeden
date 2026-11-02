// edenunpack: writes the files of an Eden Archive to a folder, or lists them.
#include <cstdio>
#include <string>
#include <vector>

#include <edenarchive/extract.hpp>

#include <components/vfs/entryname.hpp>

#include "toolcommon.hpp"

namespace
{
    int usage()
    {
        std::fprintf(stderr,
            "usage: edenunpack [--overwrite] <pack.edenpak> <output-folder>\n"
            "       edenunpack --list <pack.edenpak>\n"
            "Names are checked with the engine's extraction rules before anything is written; an existing file is\n"
            "not replaced without --overwrite. Exit: 0 done, 1 invalid pack or refused, 2 usage or I/O error.\n");
        return EdenTools::exitUsage;
    }
}

static int run(int argc, char** argv)
{
    using namespace EdenTools;
    bool list = false;
    EdenArchive::UnpackOptions options;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--list")
            list = true;
        else if (arg == "--overwrite")
            options.mOverwrite = true;
        else if (arg.size() > 1 && arg[0] == '-' && arg[1] == '-')
            return usage();
        else
            positional.push_back(arg);
    }
    if (positional.size() != (list ? 1u : 2u))
        return usage();

    int exitCode = exitOk;
    const std::unique_ptr<EdenArchive::Archive> pack = openPack(pathFromUtf8(positional[0]), exitCode);
    if (!pack)
        return exitCode;
    if (list)
    {
        // Names only (edenlist prints sizes and hashes too).
        for (std::size_t i = 0; i < pack->entryCount(); ++i)
        {
            const EdenArchive::EntryInfo entry = pack->entry(i);
            if (entry.mHasPath)
                std::printf("%.*s\n", static_cast<int>(entry.mPath.size()), entry.mPath.data());
            else
                std::printf("key:%016llx\n", static_cast<unsigned long long>(entry.mKey));
        }
        return exitOk;
    }
    // The engine's own rule (components/vfs/entryname.cpp), as task 0052 asks.
    options.mNameCheck = [](std::string_view name) { return VFS::Path::checkExtractionSafe(name); };
    const EdenArchive::Error error = EdenArchive::unpack(*pack, pathFromUtf8(positional[1]), options);
    if (error)
    {
        std::fprintf(stderr, "%s\n", error.describe(positional[0]).c_str());
        return exitInvalid;
    }
    return exitOk;
}

int main(int argc, char** argv)
{
    return EdenTools::runGuarded(&run, argc, argv);
}
