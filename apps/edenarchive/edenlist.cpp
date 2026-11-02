// edenlist: lists the entries of an Eden Archive: size, content hash and path (or key) of each.
#include <cstdio>
#include <string>

#include "toolcommon.hpp"

static int run(int argc, char** argv)
{
    using namespace EdenTools;
    if (argc != 2 || std::string(argv[1]).starts_with("--"))
    {
        std::fprintf(stderr,
            "usage: edenlist <pack.edenpak>\n"
            "Prints '<size> <64 hex content hash> <path>' (or 'key:<16 hex digits>') per entry, in entry order.\n"
            "Exit: 0 done, 1 invalid pack, 2 usage or I/O error.\n");
        return exitUsage;
    }
    int exitCode = exitOk;
    const std::unique_ptr<EdenArchive::Archive> pack = openPack(pathFromUtf8(argv[1]), exitCode);
    if (!pack)
        return exitCode;
    listPack(*pack);
    return exitOk;
}

int main(int argc, char** argv)
{
    return EdenTools::runGuarded(&run, argc, argv);
}
