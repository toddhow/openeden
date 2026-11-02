// edenvalidate: reads every byte of an Eden Archive under the specification's rules and says where it is not valid.
#include <cstdio>
#include <string>
#include <vector>

#include <edenarchive/validator.hpp>

#include "toolcommon.hpp"

namespace
{
    int usage()
    {
        std::fprintf(stderr,
            "usage: edenvalidate [--non-canonical] [--no-content] [--budget BYTES] <pack.edenpak>\n"
            "Prints OK for a valid pack, otherwise one finding per line:\n"
            "  <file>: E_CLASS at offset 0xHEX, field NAME: message\n"
            "--non-canonical skips the canonical-form checks; --no-content skips decoding and hashing the data.\n"
            "Exit: 0 valid, 1 invalid, 2 usage or I/O error.\n");
        return EdenTools::exitUsage;
    }
}

static int run(int argc, char** argv)
{
    using namespace EdenTools;
    EdenArchive::ValidateOptions options;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--non-canonical")
            options.mCanonical = false;
        else if (arg == "--no-content")
            options.mVerifyContent = false;
        else if (arg == "--budget")
        {
            std::uint64_t value = 0;
            if (i + 1 >= argc || !parseUnsigned(argv[++i], value))
                return usage();
            options.mDecodeBudget = value;
        }
        else if (arg.size() > 1 && arg[0] == '-' && arg[1] == '-')
            return usage();
        else
            positional.push_back(arg);
    }
    if (positional.size() != 1)
        return usage();

    EdenArchive::Error error;
    const std::shared_ptr<const EdenArchive::Source> source
        = EdenArchive::openFileSource(pathFromUtf8(positional[0]), error);
    if (!source)
    {
        std::fprintf(stderr, "%s\n", error.describe(positional[0]).c_str());
        return exitUsage;
    }
    const EdenArchive::ValidationReport report = EdenArchive::validate(source, options);
    if (report.valid())
    {
        std::printf("OK\n");
        return exitOk;
    }
    for (const EdenArchive::Error& finding : report.mFindings)
        std::printf("%s\n", finding.describe(positional[0]).c_str());
    const bool ioProblem
        = !report.mOpened && report.mFindings.size() == 1 && report.mFindings[0].mClass == EdenArchive::ErrorClass::Io;
    return ioProblem ? exitUsage : exitInvalid;
}

int main(int argc, char** argv)
{
    return EdenTools::runGuarded(&run, argc, argv);
}
