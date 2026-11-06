// Task 0092 stage a: the shape of the script runtime contract headers (sdk/include/openeden/script/).
//
// In plain words: a contract header is what other code builds against, so it must not drag in anything of the engine.
// These tests read the header files themselves and check the rules of the root guide: no record, engine, renderer, Qt
// or Jolt type; standard types only; and the contract types live in a named namespace (an anonymous one gives every
// library its own copy of the type and aborts on GCC).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#include <openeden/script/host.hpp>
#include <openeden/script/obscript.hpp>

namespace
{
    std::string readHeader(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "sdk"
            / "include" / "openeden" / "script" / name;
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    const std::vector<std::string> sHeaders = { "host.hpp", "obscript.hpp" };

    TEST(ScriptContractHeaders, the_headers_exist)
    {
        for (const std::string& header : sHeaders)
        {
            EXPECT_FALSE(readHeader(header).empty()) << header;
        }
    }

    TEST(ScriptContractHeaders, include_nothing_of_the_engine_or_a_third_party)
    {
        const std::vector<std::string> forbidden = { "components/", "apps/", "esm", "ESM", "MyGUI", "mygui", "Qt",
            "QString", "Jolt", "JPH", "osg", "vsg::", "sol::", "lua_State", "Compiler::", "Interpreter::" };
        for (const std::string& header : sHeaders)
        {
            const std::string text = readHeader(header);
            ASSERT_FALSE(text.empty()) << header;
            std::size_t position = 0;
            while ((position = text.find("#include", position)) != std::string::npos)
            {
                const std::size_t end = text.find('\n', position);
                const std::string line = text.substr(position, end - position);
                // Only the SDK's own headers and the standard library may be included.
                const bool sdk = line.find("<openeden/") != std::string::npos;
                const bool standard = line.find('.') == std::string::npos;
                EXPECT_TRUE(sdk || standard) << header << ": " << line;
                position = end;
            }
            for (const std::string& word : forbidden)
            {
                EXPECT_EQ(text.find(word), std::string::npos) << header << " mentions " << word;
            }
        }
    }

    TEST(ScriptContractHeaders, the_contract_types_are_in_a_named_namespace)
    {
        for (const std::string& header : sHeaders)
        {
            const std::string text = readHeader(header);
            EXPECT_NE(text.find("namespace openeden::script"), std::string::npos) << header;
            EXPECT_EQ(text.find("namespace\n"), std::string::npos) << header << " has an anonymous namespace";
            EXPECT_EQ(text.find("namespace {"), std::string::npos) << header << " has an anonymous namespace";
        }
    }

    TEST(ScriptContractHeaders, the_world_and_the_runtime_are_abstract_and_have_virtual_destructors)
    {
        EXPECT_TRUE(std::is_abstract_v<openeden::script::ObScriptWorld>);
        EXPECT_TRUE(std::is_abstract_v<openeden::script::ObScriptContract>);
        EXPECT_TRUE(std::has_virtual_destructor_v<openeden::script::ObScriptWorld>);
        EXPECT_TRUE(std::has_virtual_destructor_v<openeden::script::ObScriptContract>);
    }

    TEST(ScriptContractHeaders, a_form_is_a_32_bit_integer)
    {
        static_assert(std::is_same_v<openeden::script::FormId, std::uint32_t>);
        EXPECT_EQ(sizeof(openeden::script::FormId), 4u);
    }

    TEST(ScriptContractHeaders, the_contract_has_an_id_and_a_version)
    {
        EXPECT_EQ(openeden::script::ObScriptContract::kId, "openeden.script.obscript");
        EXPECT_EQ(openeden::script::ObScriptContract::kVersion.major, 1);
    }

    TEST(ScriptContractHeaders, an_error_report_formats_as_script_line_column_message)
    {
        openeden::script::ErrorReport report;
        report.script = "MyScript";
        report.line = 4;
        report.column = 9;
        report.message = "unknown function 'X'";
        EXPECT_EQ(report.format(), "MyScript:4:9: unknown function 'X'");

        report.line = 0;
        EXPECT_EQ(report.format(), "MyScript: unknown function 'X'");
    }

    TEST(ScriptContractHeaders, a_value_carries_one_kind_and_defaults_to_integer_zero)
    {
        const openeden::script::Value zero;
        EXPECT_EQ(zero.kind, openeden::script::ValueKind::Integer);
        EXPECT_EQ(zero.integer, 0);
        EXPECT_EQ(openeden::script::Value::makeFloat(2.5f).number, 2.5f);
        EXPECT_EQ(openeden::script::Value::makeForm(0xFFFFFFFFu).integer, -1);
        EXPECT_EQ(openeden::script::Value::makeString("abc").text, "abc");
    }
}
