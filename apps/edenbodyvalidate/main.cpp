// The body package validator as a program (task 0063). All the work is in components/body/bodytool.cpp so that the
// tests can call it without starting a process.

#include <components/body/bodytool.hpp>

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
    return Body::runBodyValidateTool(args, std::cout, std::cerr);
}
