// edenlist: lists the entries of an Eden Archive: size, content hash and path (or key) of each.
#include <cstdio>
#include <string>

#include "toolcommon.hpp"

static int run(int argc, char** argv)
{
    using namespace EdenTools;
    const bool provenance = argc == 3 && std::string(argv[1]) == "--provenance";
    const char* const packPath = provenance ? argv[2] : (argc == 2 ? argv[1] : nullptr);
    if (packPath == nullptr || std::string(packPath).starts_with("--"))
    {
        std::fprintf(stderr,
            "usage: edenlist [--provenance] <pack.edenpak>\n"
            "Prints '<size> <64 hex content hash> <path>' (or 'key:<16 hex digits>') per entry, in entry order.\n"
            "--provenance adds ' prov <tool id> <64 hex source digest> <parameter bytes in hex, or ->' to each line\n"
            "(' prov none' for an entry without a record).\n"
            "Exit: 0 done, 1 invalid pack, 2 usage or I/O error.\n");
        return exitUsage;
    }
    int exitCode = exitOk;
    const std::unique_ptr<EdenArchive::Archive> pack = openPack(pathFromUtf8(packPath), exitCode);
    if (!pack)
        return exitCode;
    listPack(*pack, provenance);
    return exitOk;
}

int main(int argc, char** argv)
{
    return EdenTools::runGuarded(&run, argc, argv);
}
