// The CreativeMotion transition measurement as a program (task 0067, stage 0). All the work is in
// components/animtrace/tracetool.cpp so that the tests can call it without starting a process.

#include <components/animtrace/tracetool.hpp>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }
    return AnimTrace::runTraceTool(args, std::cout, std::cerr);
}
