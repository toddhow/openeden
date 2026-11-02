// Eden Archive conformance corpus generator (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0.
//
// Writes every case of the corpus as <id>.edenpak into a folder, and expected.txt next to them. The cases come from
// the shared list in edenarchive/testsupport/corpuscases.cpp, which the unit tests use too.
//
//   edenarchive-corpusgen [output-folder]        (the default is the current folder)
//
// Exit codes: 0 done, 2 usage or input/output error.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "corpuscases.hpp"

namespace
{
    bool writeFile(const std::filesystem::path& path, const void* data, std::size_t size)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        stream.flush();
        return static_cast<bool>(stream);
    }
}

int main(int argc, char** argv)
{
    if (argc > 2)
    {
        std::fprintf(stderr, "usage: edenarchive-corpusgen [output-folder]\n");
        return 2;
    }
    const std::filesystem::path folder = argc == 2 ? argv[1] : ".";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error)
    {
        std::fprintf(stderr, "cannot create %s: %s\n", folder.string().c_str(), error.message().c_str());
        return 2;
    }

    for (const EdenTest::CorpusCase& corpusCase : EdenTest::corpusCases())
    {
        const std::filesystem::path path = folder / (corpusCase.mId + ".edenpak");
        if (!writeFile(path, corpusCase.mBytes.data(), corpusCase.mBytes.size()))
        {
            std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
            return 2;
        }
    }
    const std::string text = EdenTest::expectedText();
    if (!writeFile(folder / "expected.txt", text.data(), text.size()))
    {
        std::fprintf(stderr, "cannot write expected.txt\n");
        return 2;
    }
    std::printf("wrote %zu cases to %s\n", EdenTest::corpusCases().size(), folder.string().c_str());
    return 0;
}
