// Task 0093 stage a: the shape of the Papyrus runtime contract header (sdk/include/openeden/script/papyrus.hpp).
//
// In plain words: a contract header is what other code builds against, so it must not drag in anything of the engine.
// These tests read the header file itself and check the rules of the root guide (standard types only, a named
// namespace) and the values the task spec states (contract id and version, the value makers, the attach key order).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <openeden/script/papyrus.hpp>

namespace
{
    using namespace openeden::script;

    std::string readHeader()
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "sdk"
            / "include" / "openeden" / "script" / "papyrus.hpp";
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    TEST(PapyrusContractHeader, exists)
    {
        EXPECT_FALSE(readHeader().empty());
    }

    TEST(PapyrusContractHeader, includes_only_the_sdk_and_the_standard_library)
    {
        const std::string text = readHeader();
        ASSERT_FALSE(text.empty());
        std::size_t position = 0;
        int includes = 0;
        while ((position = text.find("#include", position)) != std::string::npos)
        {
            const std::size_t end = text.find('\n', position);
            const std::string line = text.substr(position, end - position);
            const bool sdk = line.find("<openeden/") != std::string::npos;
            const bool standard = line.find('.') == std::string::npos;
            EXPECT_TRUE(sdk || standard) << line;
            ++includes;
            position = end;
        }
        EXPECT_GT(includes, 0);
    }

    TEST(PapyrusContractHeader, names_nothing_of_the_engine_or_a_third_party_in_code)
    {
        const std::string text = readHeader();
        ASSERT_FALSE(text.empty());
        const std::vector<std::string> forbidden = { "components/", "apps/", "MyGUI", "mygui", "Qt", "QString", "Jolt",
            "JPH", "osg", "vsg::", "sol::", "lua_State", "Compiler::", "Interpreter::" };
        for (const std::string& word : forbidden)
        {
            // A comment may say what the contract does not use; code may not.
            std::size_t position = 0;
            while ((position = text.find(word, position)) != std::string::npos)
            {
                const std::size_t newline = text.rfind('\n', position);
                const std::size_t lineStart = newline == std::string::npos ? 0 : newline + 1;
                const std::string linePrefix = text.substr(lineStart, position - lineStart);
                EXPECT_NE(linePrefix.find("//"), std::string::npos) << word << " outside a comment";
                position += word.size();
            }
        }
    }

    TEST(PapyrusContractHeader, lives_in_a_named_namespace_only)
    {
        const std::string text = readHeader();
        EXPECT_NE(text.find("namespace openeden::script"), std::string::npos);
        // An anonymous namespace gives each library its own copy of a type and aborts on GCC.
        EXPECT_EQ(text.find("namespace\n"), std::string::npos);
        EXPECT_EQ(text.find("namespace {"), std::string::npos);
    }

    TEST(PapyrusContract, has_its_own_id_and_version_one_zero)
    {
        EXPECT_EQ(PapyrusContract::kId, "openeden.script.papyrus");
        EXPECT_EQ(PapyrusContract::kVersion.major, 1);
        EXPECT_EQ(PapyrusContract::kVersion.minor, 1);
    }

    TEST(PapyrusContract, every_operation_that_can_fail_is_noexcept)
    {
        static_assert(noexcept(std::declval<PapyrusContract&>().loadScript({}, {})));
        static_assert(noexcept(std::declval<const PapyrusContract&>().link()));
        static_assert(noexcept(std::declval<PapyrusContract&>().attach({}, {})));
        static_assert(noexcept(std::declval<PapyrusContract&>().send(0, {}, {})));
        static_assert(noexcept(std::declval<PapyrusContract&>().call(0, {}, {})));
        static_assert(noexcept(std::declval<PapyrusContract&>().tick(0.0)));
        static_assert(noexcept(std::declval<const PapyrusContract&>().serialise()));
        static_assert(noexcept(std::declval<PapyrusContract&>().restore({})));
        static_assert(noexcept(std::declval<PapyrusContract&>().setPolicy(UnknownFunctionPolicy::Tolerant)));
        SUCCEED();
    }

    TEST(PapyrusContract, shares_the_host_types_unchanged)
    {
        static_assert(std::is_same_v<decltype(CallResult::error), ErrorReport>);
        static_assert(std::is_same_v<decltype(AttachKey::formId), FormId>);
        static_assert(std::is_same_v<decltype(std::declval<const PapyrusContract&>().serialise()), SaveBlob>);
        static_assert(std::is_same_v<decltype(std::declval<PapyrusContract&>().restore({})), RestoreResult>);
        SUCCEED();
    }

    TEST(PapyrusValue, a_default_value_is_none)
    {
        const PapyrusValue value;
        EXPECT_EQ(value.kind, PapyrusKind::None);
        EXPECT_EQ(PapyrusValue::makeNone().kind, PapyrusKind::None);
    }

    TEST(PapyrusValue, makers_set_the_kind_and_the_payload)
    {
        EXPECT_EQ(PapyrusValue::makeInt(-5).kind, PapyrusKind::Int);
        EXPECT_EQ(PapyrusValue::makeInt(-5).integer, -5);
        EXPECT_EQ(PapyrusValue::makeFloat(2.5f).kind, PapyrusKind::Float);
        EXPECT_EQ(PapyrusValue::makeFloat(2.5f).number, 2.5f);
        EXPECT_EQ(PapyrusValue::makeBool(true).kind, PapyrusKind::Bool);
        EXPECT_EQ(PapyrusValue::makeBool(true).integer, 1);
        EXPECT_EQ(PapyrusValue::makeBool(false).integer, 0);
        EXPECT_EQ(PapyrusValue::makeString("abc").kind, PapyrusKind::String);
        EXPECT_EQ(PapyrusValue::makeString("abc").text, "abc");
        EXPECT_EQ(PapyrusValue::makeArray(7).kind, PapyrusKind::Array);
        EXPECT_EQ(PapyrusValue::makeArray(7).handle, 7u);
        EXPECT_EQ(PapyrusValue::makeStruct(9).kind, PapyrusKind::Struct);
        EXPECT_EQ(PapyrusValue::makeStruct(9).handle, 9u);
    }

    TEST(PapyrusValue, an_object_is_a_native_form_or_an_attached_instance)
    {
        const PapyrusValue form = PapyrusValue::makeForm("Actor", 0x14);
        EXPECT_EQ(form.kind, PapyrusKind::Object);
        EXPECT_FALSE(form.attached);
        EXPECT_EQ(form.handle, 0x14u);
        EXPECT_EQ(form.text, "Actor");
        const PapyrusValue instance = PapyrusValue::makeInstance(12);
        EXPECT_EQ(instance.kind, PapyrusKind::Object);
        EXPECT_TRUE(instance.attached);
        EXPECT_EQ(instance.handle, 12u);
    }

    TEST(PapyrusValue, int_is_signed_thirty_two_bit_and_a_form_id_is_unsigned)
    {
        static_assert(std::is_same_v<decltype(PapyrusValue::integer), std::int32_t>);
        static_assert(std::is_same_v<FormId, std::uint32_t>);
        const PapyrusValue form = PapyrusValue::makeForm("Form", 0xFF000014u);
        EXPECT_EQ(form.handle, 0xFF000014u);
    }

    TEST(AttachKey, orders_by_kind_then_form_then_alias_then_effect)
    {
        AttachKey a;
        a.kind = AttachKind::Form;
        a.formId = 5;
        AttachKey b = a;
        b.formId = 6;
        AttachKey c = a;
        c.kind = AttachKind::Alias;
        c.formId = 1;
        AttachKey d = c;
        d.aliasIndex = 2;
        AttachKey e = d;
        e.effectKey = 3;
        EXPECT_LT(a, b);
        EXPECT_LT(b, c);
        EXPECT_LT(c, d);
        EXPECT_LT(d, e);
        EXPECT_EQ(a, a);
        std::map<AttachKey, int> map;
        map[a] = 1;
        map[b] = 2;
        EXPECT_EQ(map.size(), 2u);
    }

    TEST(NativeDescriptor, defaults_are_instance_not_latent_main_thread)
    {
        const NativeDescriptor descriptor;
        EXPECT_FALSE(descriptor.global);
        EXPECT_FALSE(descriptor.latent);
        EXPECT_EQ(descriptor.threadRule, NativeThreadRule::MainOnly);
    }

    TEST(PexSizeConvention, has_the_three_observed_conventions)
    {
        EXPECT_NE(PexSizeConvention::IncludesSelf, PexSizeConvention::ExcludesSelf);
        EXPECT_NE(PexSizeConvention::ExcludesSelf, PexSizeConvention::ExcludesSelfOtherOrder);
    }
}
