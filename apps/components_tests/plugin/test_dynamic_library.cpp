#include <components/plugin/dynamic_library.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace
{
    using openeden::plugin::DynamicLibrary;

#if defined(_WIN32)
    const std::filesystem::path kSystemLibraryName = "kernel32.dll";
    const std::string kValidSymbolName = "GetCurrentProcessId";
    using GetPidFn = unsigned long(__stdcall*)();
#else
    const std::filesystem::path kSystemLibraryName = "libc.so.6";
    const std::string kValidSymbolName = "getpid";
    using GetPidFn = int (*)();
#endif
} // namespace

TEST(DynamicLibraryTest, LoadNonExistentFile_ReturnsFalseWithDescriptiveError)
{
    DynamicLibrary lib;
    std::string error;
    const std::filesystem::path nonExistent = "non_existent_library_987654321.dll";

    const bool success = lib.load(nonExistent, error);

    EXPECT_FALSE(success);
    EXPECT_FALSE(lib.isLoaded());
    EXPECT_FALSE(error.empty());
}

TEST(DynamicLibraryTest, ConstructorWithNonExistentPath_FailsToLoad)
{
    std::string error;
    const DynamicLibrary lib("non_existent_library_987654321.dll", error);

    EXPECT_FALSE(lib.isLoaded());
    EXPECT_FALSE(error.empty());
}

TEST(DynamicLibraryTest, LoadValidLibrary_ResolvesSymbolSuccessfully)
{
    DynamicLibrary lib;
    std::string error;

    const bool loaded = lib.load(kSystemLibraryName, error);
    ASSERT_TRUE(loaded) << "Failed to load system library: " << error;
    EXPECT_TRUE(lib.isLoaded());

    std::string symError;
    void* sym = lib.getSymbol(kValidSymbolName, symError);
    ASSERT_NE(sym, nullptr) << "Failed to resolve valid symbol: " << symError;
    EXPECT_TRUE(symError.empty());

    auto fn = reinterpret_cast<GetPidFn>(sym);
    EXPECT_GT(fn(), 0u);
}

TEST(DynamicLibraryTest, QueryNonExistentSymbol_ReturnsNullptrWithError)
{
    DynamicLibrary lib;
    std::string error;

    const bool loaded = lib.load(kSystemLibraryName, error);
    ASSERT_TRUE(loaded) << "Failed to load system library: " << error;
    ASSERT_TRUE(lib.isLoaded());

    std::string symError;
    void* sym = lib.getSymbol("symbol_that_does_not_exist_xyz123", symError);

    EXPECT_EQ(sym, nullptr);
    EXPECT_FALSE(symError.empty());
}

TEST(DynamicLibraryTest, QuerySymbolOnUnloadedLibrary_ReturnsNullptrWithError)
{
    DynamicLibrary lib;
    EXPECT_FALSE(lib.isLoaded());

    std::string symError;
    void* sym = lib.getSymbol(kValidSymbolName, symError);

    EXPECT_EQ(sym, nullptr);
    EXPECT_FALSE(symError.empty());
}

TEST(DynamicLibraryTest, RaiiUnloadSemantics_ExplicitUnloadResetsState)
{
    DynamicLibrary lib;
    std::string error;

    ASSERT_TRUE(lib.load(kSystemLibraryName, error)) << error;
    EXPECT_TRUE(lib.isLoaded());

    lib.unload();
    EXPECT_FALSE(lib.isLoaded());

    std::string symError;
    void* sym = lib.getSymbol(kValidSymbolName, symError);
    EXPECT_EQ(sym, nullptr);
    EXPECT_FALSE(symError.empty());
}

TEST(DynamicLibraryTest, RaiiUnloadSemantics_MoveConstructorTransfersOwnership)
{
    DynamicLibrary lib1;
    std::string error;

    ASSERT_TRUE(lib1.load(kSystemLibraryName, error)) << error;
    EXPECT_TRUE(lib1.isLoaded());

    DynamicLibrary lib2(std::move(lib1));
    EXPECT_FALSE(lib1.isLoaded());
    EXPECT_TRUE(lib2.isLoaded());

    std::string symError;
    void* sym = lib2.getSymbol(kValidSymbolName, symError);
    EXPECT_NE(sym, nullptr);
    EXPECT_TRUE(symError.empty());
}

TEST(DynamicLibraryTest, RaiiUnloadSemantics_MoveAssignmentTransfersOwnership)
{
    DynamicLibrary lib1;
    std::string error;

    ASSERT_TRUE(lib1.load(kSystemLibraryName, error)) << error;

    DynamicLibrary lib2;
    EXPECT_FALSE(lib2.isLoaded());

    lib2 = std::move(lib1);
    EXPECT_FALSE(lib1.isLoaded());
    EXPECT_TRUE(lib2.isLoaded());

    std::string symError;
    void* sym = lib2.getSymbol(kValidSymbolName, symError);
    EXPECT_NE(sym, nullptr);
}

TEST(DynamicLibraryTest, RaiiUnloadSemantics_DestructorReleasesLibrary)
{
    // Scope block to verify destructor runs safely without crashes
    {
        DynamicLibrary scopedLib;
        std::string error;
        ASSERT_TRUE(scopedLib.load(kSystemLibraryName, error)) << error;
        EXPECT_TRUE(scopedLib.isLoaded());
    }
    // Library has been safely closed out of scope
    SUCCEED();
}
