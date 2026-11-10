// Stand-in for `openmw --bake-lod-terrain` and `--bake-lod-objects` in the launcher page check
// (tools/launcher_check/pagegrab.cpp): prints a few lines, takes a moment and exits with 0, or with 3 for the object
// clusters, so that the page's running, finished and failed states can be seen without the game. It bakes nothing.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

int main(int argc, char** argv)
{
    const char* what = argc > 1 ? argv[1] : "(no argument)";
    std::printf("fake bake: %s\n", what);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    if (argc > 1 && std::strcmp(argv[1], "--bake-lod-objects") == 0)
    {
        std::printf("fake bake: failing on purpose\n");
        return 3;
    }
    std::printf("fake bake: done\n");
    return 0;
}
