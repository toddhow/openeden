// Task 0093 stage c: arrays, Fallout 4 structs, object casts and "is", and the VM heap (garbage collection and caps).
// Written from the task's spec before the VM. The operand order of the array and struct opcodes is the one read from
// the installed scripts (stage log); opcodes 35, 41, 44 and 47 never occur in them and are tested as recalled.

#include <gtest/gtest.h>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;
    using ScriptPapyrus::ErrorKind;

    // ---- arrays ----------------------------------------------------------------------------------------------------

    TEST(PapyrusVmData, an_array_is_created_filled_read_and_measured)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ScriptBuilder s("Arr");
            s.function("Run", "Int")
                .local("arr", "Int[]")
                .local("n", "Int")
                .local("v", "Int")
                .local("z", "Int")
                .op(Op::ArrayCreate, { Id("arr"), Int(5) })
                .op(Op::ArraySetElement, { Id("arr"), Int(2), Int(42) })
                .op(Op::ArrayGetElement, { Id("v"), Id("arr"), Int(2) })
                .op(Op::ArrayGetElement, { Id("z"), Id("arr"), Int(0) })
                .op(Op::ArrayLength, { Id("n"), Id("arr") })
                .op(Op::IMul, { Id("v"), Id("v"), Int(100) })
                .iadd(Id("v"), Id("v"), Id("n"))
                .iadd(Id("v"), Id("v"), Id("z"))
                .ret(Id("v"));
            ASSERT_TRUE(vm.add(s));
            EXPECT_EQ(vm.runInt(vm.make("Arr"), "Run"), 4205) << "element 42, length 5, default element 0";
        }
    }

    TEST(PapyrusVmData, new_elements_take_the_default_of_the_arrays_element_type)
    {
        Vm vm;
        ScriptBuilder s("Defaults");
        s.function("Str", "String")
            .local("arr", "String[]")
            .local("v", "String")
            .op(Op::ArrayCreate, { Id("arr"), Int(2) })
            .op(Op::ArrayGetElement, { Id("v"), Id("arr"), Int(1) })
            .ret(Id("v"));
        s.function("Flt", "Float")
            .local("arr", "Float[]")
            .local("v", "Float")
            .op(Op::ArrayCreate, { Id("arr"), Int(2) })
            .op(Op::ArrayGetElement, { Id("v"), Id("arr"), Int(1) })
            .ret(Id("v"));
        s.function("Bool", "Bool")
            .local("arr", "Bool[]")
            .local("v", "Bool")
            .op(Op::ArrayCreate, { Id("arr"), Int(2) })
            .op(Op::ArrayGetElement, { Id("v"), Id("arr"), Int(1) })
            .ret(Id("v"));
        s.function("Obj", "Defaults")
            .local("arr", "Defaults[]")
            .local("v", "Defaults")
            .op(Op::ArrayCreate, { Id("arr"), Int(2) })
            .op(Op::ArrayGetElement, { Id("v"), Id("arr"), Int(1) })
            .ret(Id("v"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Defaults");
        EXPECT_EQ(vm.run(id, "Str").kind, PapyrusKind::String);
        EXPECT_EQ(vm.run(id, "Flt").kind, PapyrusKind::Float);
        EXPECT_EQ(vm.run(id, "Bool").kind, PapyrusKind::Bool);
        EXPECT_EQ(vm.run(id, "Obj").kind, PapyrusKind::None);
    }

    TEST(PapyrusVmData, arrays_are_references_and_live_in_variables)
    {
        Vm vm;
        ScriptBuilder s("Shared");
        s.var("keep", "Int[]");
        s.function("Fill")
            .local("arr", "Int[]")
            .op(Op::ArrayCreate, { Id("arr"), Int(3) })
            .op(Op::ArraySetElement, { Id("arr"), Int(1), Int(9) })
            .assign(Id("keep"), Id("arr"))
            .ret(Nil());
        s.function("Poke")
            .local("other", "Int[]")
            .assign(Id("other"), Id("keep"))
            .op(Op::ArraySetElement, { Id("other"), Int(2), Int(5) })
            .ret(Nil());
        s.function("Sum", "Int")
            .local("a", "Int")
            .local("b", "Int")
            .op(Op::ArrayGetElement, { Id("a"), Id("keep"), Int(1) })
            .op(Op::ArrayGetElement, { Id("b"), Id("keep"), Int(2) })
            .iadd(Id("a"), Id("a"), Id("b"))
            .ret(Id("a"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Shared");
        vm.run(id, "Fill");
        vm.run(id, "Poke");
        EXPECT_EQ(vm.runInt(id, "Sum"), 14) << "the second name for the array sees the write";
    }

    TEST(PapyrusVmData, find_looks_forward_from_a_start_index_and_gives_minus_one_for_none)
    {
        Vm vm;
        ScriptBuilder s("Finder");
        auto build = [&s](const char* name, std::int32_t wanted, std::int32_t start) {
            s.function(name, "Int")
                .local("arr", "Int[]")
                .local("r", "Int")
                .op(Op::ArrayCreate, { Id("arr"), Int(4) })
                .op(Op::ArraySetElement, { Id("arr"), Int(0), Int(3) })
                .op(Op::ArraySetElement, { Id("arr"), Int(1), Int(5) })
                .op(Op::ArraySetElement, { Id("arr"), Int(2), Int(7) })
                .op(Op::ArraySetElement, { Id("arr"), Int(3), Int(5) })
                .op(Op::ArrayFindElement, { Id("arr"), Id("r"), Int(wanted), Int(start) })
                .ret(Id("r"));
        };
        build("First", 5, 0);
        build("Second", 5, 2);
        build("Missing", 9, 0);
        build("PastEnd", 5, 4);
        s.function("Strings", "Int")
            .local("arr", "String[]")
            .local("r", "Int")
            .op(Op::ArrayCreate, { Id("arr"), Int(2) })
            .op(Op::ArraySetElement, { Id("arr"), Int(1), Str("Hello") })
            .op(Op::ArrayFindElement, { Id("arr"), Id("r"), Str("hELLO"), Int(0) })
            .ret(Id("r"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Finder");
        EXPECT_EQ(vm.runInt(id, "First"), 1);
        EXPECT_EQ(vm.runInt(id, "Second"), 3);
        EXPECT_EQ(vm.runInt(id, "Missing"), -1);
        EXPECT_EQ(vm.runInt(id, "PastEnd"), -1);
        EXPECT_EQ(vm.runInt(id, "Strings"), 1) << "string elements are compared without regard to case";
    }

    TEST(PapyrusVmData, rfind_looks_backward_and_minus_one_starts_at_the_last_element)
    {
        Vm vm(Target::Fallout4);
        ScriptBuilder s("Rfinder");
        auto build = [&s](const char* name, std::int32_t start) {
            s.function(name, "Int")
                .local("arr", "Int[]")
                .local("r", "Int")
                .op(Op::ArrayCreate, { Id("arr"), Int(4) })
                .op(Op::ArraySetElement, { Id("arr"), Int(1), Int(5) })
                .op(Op::ArraySetElement, { Id("arr"), Int(3), Int(5) })
                .op(Op::ArrayRFindElement, { Id("arr"), Id("r"), Int(5), Int(start) })
                .ret(Id("r"));
        };
        build("FromEnd", -1);
        build("FromTwo", 2);
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Rfinder");
        EXPECT_EQ(vm.runInt(id, "FromEnd"), 3);
        EXPECT_EQ(vm.runInt(id, "FromTwo"), 1);
    }

    TEST(PapyrusVmData, a_none_array_has_length_zero_and_cannot_be_indexed)
    {
        Vm vm;
        ScriptBuilder s("NoArray");
        s.var("arr", "Int[]");
        s.function("Length", "Int").local("n", "Int").op(Op::ArrayLength, { Id("n"), Id("arr") }).ret(Id("n"));
        s.function("Get", "Int").local("v", "Int").op(Op::ArrayGetElement, { Id("v"), Id("arr"), Int(0) }).ret(Id("v"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("NoArray");
        EXPECT_EQ(vm.runInt(id, "Length"), 0);
        const CallResult result = vm.call(id, "Get");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[none receiver]")) << result.error.format();
    }

    TEST(PapyrusVmData, an_index_out_of_range_ends_the_stack_with_a_located_error)
    {
        Vm vm;
        ScriptBuilder s("Range");
        auto access = [&s](const char* name, std::int32_t index, bool write) {
            auto& f = s.function(name, "Int").local("arr", "Int[]").local("v", "Int");
            f.op(Op::ArrayCreate, { Id("arr"), Int(3) });
            if (write)
            {
                f.op(Op::ArraySetElement, { Id("arr"), Int(index), Int(1) });
            }
            else
            {
                f.op(Op::ArrayGetElement, { Id("v"), Id("arr"), Int(index) });
            }
            f.ret(Id("v"));
        };
        access("ReadAtLength", 3, false);
        access("ReadNegative", -1, false);
        access("WriteAtLength", 3, true);
        access("ReadLast", 2, false);
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Range");
        for (const char* name : { "ReadAtLength", "ReadNegative", "WriteAtLength" })
        {
            const CallResult result = vm.call(id, name);
            EXPECT_FALSE(result.ok) << name;
            EXPECT_TRUE(hasText(result.error, "[index out of range]")) << result.error.format();
            EXPECT_TRUE(hasText(result.error, std::string("Range.") + name + "@1")) << result.error.format();
        }
        EXPECT_TRUE(vm.call(id, "ReadLast").ok);
    }

    TEST(PapyrusVmData, the_array_length_cap_is_a_setting_with_a_hard_ceiling)
    {
        auto create = [](std::int32_t size) {
            ScriptBuilder s("Big");
            s.function("Run", "Int")
                .local("arr", "Int[]")
                .local("n", "Int")
                .op(Op::ArrayCreate, { Id("arr"), Int(size) })
                .op(Op::ArrayLength, { Id("n"), Id("arr") })
                .ret(Id("n"));
            return s;
        };
        {
            Vm vm; // default 128
            ASSERT_TRUE(vm.add(create(128)));
            EXPECT_EQ(vm.runInt(vm.make("Big"), "Run"), 128);
        }
        {
            Vm vm;
            ASSERT_TRUE(vm.add(create(129)));
            const CallResult result = vm.call(vm.make("Big"), "Run");
            EXPECT_FALSE(result.ok);
            EXPECT_TRUE(hasText(result.error, "[limit exceeded]")) << result.error.format();
        }
        {
            ScriptPapyrus::RuntimeOptions options;
            options.maxArrayLength = 1000;
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
            ASSERT_TRUE(vm.add(create(1000)));
            EXPECT_EQ(vm.runInt(vm.make("Big"), "Run"), 1000);
        }
        {
            ScriptPapyrus::RuntimeOptions options;
            options.maxArrayLength = 1u << 30; // clamped to 65,536
            Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
            ASSERT_TRUE(vm.add(create(65537)));
            EXPECT_FALSE(vm.call(vm.make("Big"), "Run").ok);
        }
        {
            Vm vm;
            ASSERT_TRUE(vm.add(create(-1)));
            EXPECT_FALSE(vm.call(vm.make("Big"), "Run").ok) << "a negative size";
        }
    }

    TEST(PapyrusVmData, fallout_4_arrays_grow_shrink_and_clear)
    {
        Vm vm(Target::Fallout4);
        ScriptBuilder s("Grow");
        s.var("arr", "Int[]");
        s.function("Setup")
            .op(Op::ArrayCreate, { Id("arr"), Int(0) })
            .op(Op::ArrayAdd, { Id("arr"), Int(10), Int(1) })
            .op(Op::ArrayAdd, { Id("arr"), Int(20), Int(2) }) // 10 20 20
            .op(Op::ArrayInsert, { Id("arr"), Int(5), Int(0) }) // 5 10 20 20
            .op(Op::ArrayInsert, { Id("arr"), Int(99), Int(4) }) // 5 10 20 20 99
            .ret(Nil());
        s.function("At", "Int")
            .param("i", "Int")
            .local("v", "Int")
            .op(Op::ArrayGetElement, { Id("v"), Id("arr"), Id("i") })
            .ret(Id("v"));
        s.function("Len", "Int").local("n", "Int").op(Op::ArrayLength, { Id("n"), Id("arr") }).ret(Id("n"));
        s.function("RemoveOne").op(Op::ArrayRemove, { Id("arr"), Int(1), Int(1) }).ret(Nil()); // 5 20 20 99
        s.function("RemoveTwo").op(Op::ArrayRemove, { Id("arr"), Int(1), Int(2) }).ret(Nil()); // 5 99
        s.function("RemoveLast").op(Op::ArrayRemoveLast, { Id("arr") }).ret(Nil());
        s.function("Clear").op(Op::ArrayClear, { Id("arr") }).ret(Nil());
        s.function("BadInsert").op(Op::ArrayInsert, { Id("arr"), Int(1), Int(50) }).ret(Nil());
        s.function("BadRemove").op(Op::ArrayRemove, { Id("arr"), Int(50), Int(1) }).ret(Nil());
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Grow");
        vm.run(id, "Setup");
        EXPECT_EQ(vm.runInt(id, "Len"), 5);
        const std::int32_t expected[] = { 5, 10, 20, 20, 99 };
        for (std::int32_t i = 0; i < 5; ++i)
        {
            EXPECT_EQ(vm.runInt(id, "At", { PapyrusValue::makeInt(i) }), expected[i]) << i;
        }
        vm.run(id, "RemoveOne");
        EXPECT_EQ(vm.runInt(id, "Len"), 4);
        EXPECT_EQ(vm.runInt(id, "At", { PapyrusValue::makeInt(1) }), 20);
        vm.run(id, "RemoveTwo");
        EXPECT_EQ(vm.runInt(id, "Len"), 2);
        EXPECT_EQ(vm.runInt(id, "At", { PapyrusValue::makeInt(1) }), 99);
        vm.run(id, "RemoveLast");
        EXPECT_EQ(vm.runInt(id, "Len"), 1);
        EXPECT_FALSE(vm.call(id, "BadInsert").ok);
        EXPECT_FALSE(vm.call(id, "BadRemove").ok);
        vm.run(id, "Clear");
        EXPECT_EQ(vm.runInt(id, "Len"), 0);
    }

    TEST(PapyrusVmData, adding_past_the_length_cap_fails)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxArrayLength = 3;
        Vm vm(Target::Fallout4, UnknownFunctionPolicy::Tolerant, options);
        ScriptBuilder s("Cap");
        s.function("Run")
            .local("arr", "Int[]")
            .op(Op::ArrayCreate, { Id("arr"), Int(0) })
            .op(Op::ArrayAdd, { Id("arr"), Int(1), Int(3) })
            .op(Op::ArrayAdd, { Id("arr"), Int(1), Int(1) })
            .ret(Nil());
        ASSERT_TRUE(vm.add(s));
        const CallResult result = vm.call(vm.make("Cap"), "Run");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[limit exceeded]")) << result.error.format();
    }

    // ---- structs (Fallout 4) ---------------------------------------------------------------------------------------

    ScriptBuilder structScript()
    {
        ScriptBuilder s("Shapes");
        s.structDef("Point").member("x", "Float", Flt(2.0f)).member("y", "Float").member("tag", "String", Str("t"));
        s.var("keep", "Shapes#Point");
        s.function("Defaults", "Float")
            .local("p", "Shapes#Point")
            .local("v", "Float")
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructGet, { Id("v"), Id("p"), Str("x") })
            .ret(Id("v"));
        s.function("DefaultTag", "String")
            .local("p", "Shapes#Point")
            .local("v", "String")
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructGet, { Id("v"), Id("p"), Str("tag") })
            .ret(Id("v"));
        s.function("SetGet", "Float")
            .local("p", "Shapes#Point")
            .local("v", "Float")
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructSet, { Id("p"), Str("y"), Flt(7.5f) })
            .op(Op::StructGet, { Id("v"), Id("p"), Str("y") })
            .ret(Id("v"));
        s.function("Shared", "Float")
            .local("p", "Shapes#Point")
            .local("q", "Shapes#Point")
            .local("v", "Float")
            .op(Op::StructCreate, { Id("p") })
            .assign(Id("q"), Id("p"))
            .op(Op::StructSet, { Id("q"), Str("y"), Flt(1.25f) })
            .op(Op::StructGet, { Id("v"), Id("p"), Str("y") })
            .ret(Id("v"));
        s.function("NoMember", "Float")
            .local("p", "Shapes#Point")
            .local("v", "Float")
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructGet, { Id("v"), Id("p"), Str("nothing") })
            .ret(Id("v"));
        s.function("OfNone", "Float")
            .local("v", "Float")
            .op(Op::StructGet, { Id("v"), Id("keep"), Str("x") })
            .ret(Id("v"));
        s.function("Find", "Int")
            .local("arr", "Shapes#Point[]")
            .local("p", "Shapes#Point")
            .local("r", "Int")
            .op(Op::ArrayCreate, { Id("arr"), Int(3) })
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructSet, { Id("p"), Str("y"), Flt(5.0f) })
            .op(Op::ArraySetElement, { Id("arr"), Int(2), Id("p") })
            .op(Op::ArrayFindStruct, { Id("arr"), Id("r"), Str("y"), Flt(5.0f), Int(0) })
            .ret(Id("r"));
        s.function("RFind", "Int")
            .local("arr", "Shapes#Point[]")
            .local("p", "Shapes#Point")
            .local("r", "Int")
            .op(Op::ArrayCreate, { Id("arr"), Int(3) })
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructSet, { Id("p"), Str("y"), Flt(5.0f) })
            .op(Op::ArraySetElement, { Id("arr"), Int(0), Id("p") })
            .op(Op::ArraySetElement, { Id("arr"), Int(1), Id("p") })
            .op(Op::ArrayRFindStruct, { Id("arr"), Id("r"), Str("y"), Flt(5.0f), Int(-1) })
            .ret(Id("r"));
        s.function("AllMatching", "Int")
            .local("arr", "Shapes#Point[]")
            .local("out", "Shapes#Point[]")
            .local("p", "Shapes#Point")
            .local("n", "Int")
            .op(Op::ArrayCreate, { Id("arr"), Int(3) })
            .op(Op::StructCreate, { Id("p") })
            .op(Op::StructSet, { Id("p"), Str("y"), Flt(5.0f) })
            .op(Op::ArraySetElement, { Id("arr"), Int(0), Id("p") })
            .op(Op::ArraySetElement, { Id("arr"), Int(2), Id("p") })
            .op(Op::ArrayGetAllMatchingStructs, { Id("arr"), Id("out"), Str("y"), Flt(5.0f) })
            .op(Op::ArrayLength, { Id("n"), Id("out") })
            .ret(Id("n"));
        return s;
    }

    TEST(PapyrusVmData, a_struct_starts_with_the_defaults_of_its_definition_and_members_are_set_and_read)
    {
        Vm vm(Target::Fallout4);
        ASSERT_TRUE(vm.add(structScript()));
        const InstanceId id = vm.make("Shapes");
        EXPECT_FLOAT_EQ(vm.run(id, "Defaults").number, 2.0f);
        EXPECT_EQ(vm.run(id, "DefaultTag").text, "t");
        EXPECT_FLOAT_EQ(vm.run(id, "SetGet").number, 7.5f);
        EXPECT_FLOAT_EQ(vm.run(id, "Shared").number, 1.25f) << "a struct is a reference";
    }

    TEST(PapyrusVmData, struct_errors_end_the_stack)
    {
        Vm vm(Target::Fallout4);
        ASSERT_TRUE(vm.add(structScript()));
        const InstanceId id = vm.make("Shapes");
        CallResult result = vm.call(id, "NoMember");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "nothing")) << result.error.format();
        result = vm.call(id, "OfNone");
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(hasText(result.error, "[none receiver]")) << result.error.format();
    }

    TEST(PapyrusVmData, arrays_of_structs_are_searched_by_a_member)
    {
        Vm vm(Target::Fallout4);
        ASSERT_TRUE(vm.add(structScript()));
        const InstanceId id = vm.make("Shapes");
        EXPECT_EQ(vm.runInt(id, "Find"), 2);
        EXPECT_EQ(vm.runInt(id, "RFind"), 1);
        EXPECT_EQ(vm.runInt(id, "AllMatching"), 2);
    }

    // ---- is, cast, native forms ------------------------------------------------------------------------------------

    TEST(PapyrusVmData, is_tests_the_class_chain_of_an_instance_and_of_a_native_form)
    {
        Vm vm(Target::Fallout4);
        vm.mWorld.mClasses[0x20] = "Actor";
        ASSERT_TRUE(vm.add(ScriptBuilder("Form")));
        ASSERT_TRUE(vm.add(nativeClass("ObjectReference", "Form")));
        ASSERT_TRUE(vm.add(nativeClass("Actor", "ObjectReference")));
        ASSERT_TRUE(vm.add(nativeClass("Quest", "Form")));
        ScriptBuilder base("Base");
        base.parent("Quest");
        base.var("target", "Form");
        auto test = [&base](const char* name, const char* className) {
            base.function(name, "Bool")
                .local("b", "Bool")
                .op(Op::Is, { Id("b"), Id("self"), Str(className) })
                .ret(Id("b"));
        };
        test("SelfIsBase", "base");
        test("SelfIsQuest", "Quest");
        test("SelfIsForm", "FORM");
        test("SelfIsActor", "Actor");
        for (const char* name : { "TargetIsActor", "TargetIsRef", "TargetIsQuest" })
        {
            const std::string cls = std::string(name) == "TargetIsActor" ? "Actor"
                : std::string(name) == "TargetIsRef"                     ? "ObjectReference"
                                                                         : "Quest";
            base.function(name, "Bool").local("b", "Bool").op(Op::Is, { Id("b"), Id("target"), Str(cls) }).ret(Id("b"));
        }
        ScriptBuilder child("Child");
        child.parent("Base");
        ASSERT_TRUE(vm.add(base));
        ASSERT_TRUE(vm.add(child));
        const InstanceId id = vm.make("Child");
        EXPECT_EQ(vm.run(id, "SelfIsBase").integer, 1);
        EXPECT_EQ(vm.run(id, "SelfIsQuest").integer, 1);
        EXPECT_EQ(vm.run(id, "SelfIsForm").integer, 1);
        EXPECT_EQ(vm.run(id, "SelfIsActor").integer, 0);
        ASSERT_TRUE(vm.rt().setVariable(id, "target", PapyrusValue::makeForm("Actor", 0x20)));
        EXPECT_EQ(vm.run(id, "TargetIsActor").integer, 1);
        EXPECT_EQ(vm.run(id, "TargetIsRef").integer, 1);
        EXPECT_EQ(vm.run(id, "TargetIsQuest").integer, 0);
        // None is nothing.
        ASSERT_TRUE(vm.rt().setVariable(id, "target", PapyrusValue::makeNone()));
        EXPECT_EQ(vm.run(id, "TargetIsActor").integer, 0);
    }

    TEST(PapyrusVmData, a_form_of_unknown_class_is_asked_of_the_world)
    {
        Vm vm(Target::Fallout4);
        vm.mWorld.mClasses[0x30] = "Actor";
        ASSERT_TRUE(vm.add(ScriptBuilder("Form")));
        ASSERT_TRUE(vm.add(nativeClass("Actor", "Form")));
        ScriptBuilder s("Holder");
        s.var("target", "Form");
        s.function("IsActor", "Bool")
            .local("b", "Bool")
            .op(Op::Is, { Id("b"), Id("target"), Str("Actor") })
            .ret(Id("b"));
        ASSERT_TRUE(vm.add(s));
        const InstanceId id = vm.make("Holder");
        // A form value that names no class: the world knows it (0x30) or does not (0x31).
        ASSERT_TRUE(vm.rt().setVariable(id, "target", PapyrusValue::makeForm("", 0x30)));
        EXPECT_EQ(vm.run(id, "IsActor").integer, 1);
        ASSERT_TRUE(vm.rt().setVariable(id, "target", PapyrusValue::makeForm("", 0x31)));
        EXPECT_EQ(vm.run(id, "IsActor").integer, 0);
    }

    TEST(PapyrusVmData, casting_an_object_keeps_it_when_the_class_fits_and_gives_none_when_not)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ScriptBuilder base("Base");
            base.var("held", "Base");
            ScriptBuilder child("Child");
            child.parent("Base");
            ScriptBuilder other("Other");
            other.parent("Base");
            other.function("AsChild", "Bool")
                .local("c", "Child")
                .local("b", "Bool")
                .op(Op::Cast, { Id("c"), Id("held") })
                .op(Op::Cast, { Id("b"), Id("c") })
                .ret(Id("b"));
            child.function("AsChild", "Bool")
                .local("c", "Child")
                .local("b", "Bool")
                .op(Op::Cast, { Id("c"), Id("held") })
                .op(Op::Cast, { Id("b"), Id("c") })
                .ret(Id("b"));
            child.function("AsOther", "Bool")
                .local("c", "Other")
                .local("b", "Bool")
                .op(Op::Cast, { Id("c"), Id("held") })
                .op(Op::Cast, { Id("b"), Id("c") })
                .ret(Id("b"));
            ASSERT_TRUE(vm.add(base));
            ASSERT_TRUE(vm.add(child));
            ASSERT_TRUE(vm.add(other));
            const InstanceId c = vm.make("Child");
            const InstanceId o = vm.make("Other");
            ASSERT_TRUE(vm.rt().setVariable(c, "held", PapyrusValue::makeInstance(c)));
            EXPECT_EQ(vm.run(c, "AsChild").integer, 1);
            EXPECT_EQ(vm.run(c, "AsOther").integer, 0) << "a Child is not an Other";
            ASSERT_TRUE(vm.rt().setVariable(o, "held", PapyrusValue::makeInstance(c)));
            EXPECT_EQ(vm.run(o, "AsChild").integer, 1);
        }
    }

    // ---- the heap --------------------------------------------------------------------------------------------------

    ScriptBuilder churnScript()
    {
        ScriptBuilder s("Churn");
        s.var("kept", "String").var("list", "String[]");
        // Makes n strings of about 100 bytes and drops them.
        s.function("Churn")
            .param("n", "Int")
            .local("i", "Int")
            .local("c", "Bool")
            .local("t", "String")
            .assign(Id("i"), Int(0)) // 0
            .op(Op::CompareLt, { Id("c"), Id("i"), Id("n") }) // 1
            .jmpf(Id("c"), 4) // 2 -> 6
            .op(Op::StrCat, { Id("t"), Str(std::string(100, 'x')), Id("i") }) // 3
            .iadd(Id("i"), Id("i"), Int(1)) // 4
            .jmp(-4) // 5 -> 1
            .ret(Nil()); // 6
        s.function("Keep").op(Op::StrCat, { Id("kept"), Str("kept-"), Int(77) }).ret(Nil());
        s.function("Kept", "String").ret(Id("kept"));
        s.function("FillList")
            .param("n", "Int")
            .local("i", "Int")
            .local("c", "Bool")
            .local("t", "String")
            .op(Op::ArrayCreate, { Id("list"), Id("n") }) // 0
            .assign(Id("i"), Int(0)) // 1
            .op(Op::CompareLt, { Id("c"), Id("i"), Id("n") }) // 2
            .jmpf(Id("c"), 5) // 3 -> 8
            .op(Op::StrCat, { Id("t"), Str(std::string(200, 'y')), Id("i") }) // 4
            .op(Op::ArraySetElement, { Id("list"), Id("i"), Id("t") }) // 5
            .iadd(Id("i"), Id("i"), Int(1)) // 6
            .jmp(-5) // 7 -> 2
            .ret(Nil()); // 8
        s.function("ListAt", "String")
            .param("i", "Int")
            .local("t", "String")
            .op(Op::ArrayGetElement, { Id("t"), Id("list"), Id("i") })
            .ret(Id("t"));
        return s;
    }

    TEST(PapyrusVmData, garbage_strings_are_collected_so_a_long_loop_fits_a_small_heap)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxHeapBytes = 64 * 1024;
        options.defaultBudget = { 10'000'000, 64 };
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ASSERT_TRUE(vm.add(churnScript()));
        const InstanceId id = vm.make("Churn");
        const CallResult result = vm.call(id, "Churn", { PapyrusValue::makeInt(5000) });
        EXPECT_TRUE(result.ok) << result.error.format();
        EXPECT_GT(vm.rt().stats().collections, 0u) << "about 500 KB of strings went through a 64 KB heap";
    }

    TEST(PapyrusVmData, what_a_variable_or_an_array_holds_survives_a_collection)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxHeapBytes = 64 * 1024;
        options.maxArrayLength = 64;
        options.defaultBudget = { 10'000'000, 64 };
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ASSERT_TRUE(vm.add(churnScript()));
        const InstanceId id = vm.make("Churn");
        vm.run(id, "Keep");
        vm.run(id, "FillList", { PapyrusValue::makeInt(10) });
        const std::uint64_t before = vm.rt().stats().collections;
        vm.run(id, "Churn", { PapyrusValue::makeInt(5000) });
        EXPECT_GT(vm.rt().stats().collections, before);
        EXPECT_EQ(vm.run(id, "Kept").text, "kept-77");
        EXPECT_EQ(vm.run(id, "ListAt", { PapyrusValue::makeInt(3) }).text, std::string(200, 'y') + "3");
        EXPECT_EQ(vm.run(id, "ListAt", { PapyrusValue::makeInt(9) }).text, std::string(200, 'y') + "9");
    }

    TEST(PapyrusVmData, live_data_over_the_heap_cap_ends_the_stack)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxHeapBytes = 32 * 1024;
        options.maxArrayLength = 1000;
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ASSERT_TRUE(vm.add(churnScript()));
        const InstanceId id = vm.make("Churn");
        const CallResult result = vm.call(id, "FillList", { PapyrusValue::makeInt(500) });
        EXPECT_FALSE(result.ok) << "500 strings of 200 bytes do not fit 32 KB";
        EXPECT_TRUE(hasText(result.error, "[limit exceeded]")) << result.error.format();
        // The runtime goes on (the instance still holds the part of the list that was made).
        EXPECT_TRUE(vm.call(id, "ListAt", { PapyrusValue::makeInt(0) }).ok);
    }

    TEST(PapyrusVmData, the_instance_cap_is_enforced)
    {
        ScriptPapyrus::RuntimeOptions options;
        options.maxInstances = 2;
        Vm vm(Target::SkyrimSE, UnknownFunctionPolicy::Tolerant, options);
        ASSERT_TRUE(vm.add(ScriptBuilder("One")));
        AttachKey key;
        key.formId = 1;
        EXPECT_NE(vm.rt().attach(key, "One"), 0u);
        key.formId = 2;
        EXPECT_NE(vm.rt().attach(key, "One"), 0u);
        key.formId = 3;
        EXPECT_EQ(vm.rt().attach(key, "One"), 0u);
    }
}
