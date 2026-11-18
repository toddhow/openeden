// Task 0311 (Hearthfire): PapyrusRuntime::setArrayProperty, the way a game pack gives a script the array an editor stored
// in its property (the house scripts keep their enable lists and house flags that way). Generated scripts.

#include <gtest/gtest.h>

#include <span>
#include <vector>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;

    ScriptBuilder holder()
    {
        ScriptBuilder s("Holder");
        s.var("Items", "Int[]");
        s.var("Flags", "Bool[]");
        s.var("Things", "Form[]");
        s.var("Plain", "Int");
        s.function("Sum", "Int")
            .local("n", "Int")
            .local("a", "Int")
            .local("b", "Int")
            .local("c", "Int")
            .op(Op::ArrayLength, { Id("n"), Id("Items") })
            .op(Op::ArrayGetElement, { Id("a"), Id("Items"), Int(0) })
            .op(Op::ArrayGetElement, { Id("b"), Id("Items"), Int(1) })
            .op(Op::ArrayGetElement, { Id("c"), Id("Items"), Int(2) })
            .op(Op::IMul, { Id("n"), Id("n"), Int(1000) })
            .iadd(Id("n"), Id("n"), Id("a"))
            .iadd(Id("n"), Id("n"), Id("b"))
            .iadd(Id("n"), Id("n"), Id("c"))
            .ret(Id("n"));
        s.function("FlagLength", "Int").local("n", "Int").op(Op::ArrayLength, { Id("n"), Id("Flags") }).ret(Id("n"));
        s.function("ThingLength", "Int").local("n", "Int").op(Op::ArrayLength, { Id("n"), Id("Things") }).ret(Id("n"));
        return s;
    }

    TEST(PapyrusArrayProperty, anArrayOfIntegersIsSetAndTheScriptReadsIt)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ASSERT_TRUE(vm.add(holder()));
            const InstanceId id = vm.make("Holder");
            const std::vector<PapyrusValue> items{ PapyrusValue::makeInt(10), PapyrusValue::makeInt(20),
                PapyrusValue::makeInt(30) };
            ASSERT_TRUE(vm.rt().setArrayProperty(id, "Items", items));
            EXPECT_EQ(vm.runInt(id, "Sum"), 3060) << "length 3, then the elements 10, 20 and 30";
        }
    }

    TEST(PapyrusArrayProperty, boolAndFormArraysTakeTheirKindOfElement)
    {
        Vm vm(Target::SkyrimSE);
        ASSERT_TRUE(vm.add(holder()));
        const InstanceId id = vm.make("Holder");
        const std::vector<PapyrusValue> flags{ PapyrusValue::makeBool(true), PapyrusValue::makeBool(false) };
        const std::vector<PapyrusValue> things{ PapyrusValue::makeForm("ObjectReference", 0x40), PapyrusValue::makeNone(),
            PapyrusValue::makeForm("", 0x41), PapyrusValue::makeNone() };
        ASSERT_TRUE(vm.rt().setArrayProperty(id, "Flags", flags));
        ASSERT_TRUE(vm.rt().setArrayProperty(id, "Things", things));
        EXPECT_EQ(vm.runInt(id, "FlagLength"), 2);
        EXPECT_EQ(vm.runInt(id, "ThingLength"), 4);
    }

    TEST(PapyrusArrayProperty, anEmptyArrayIsAnArrayOfLengthZero)
    {
        Vm vm(Target::SkyrimSE);
        ASSERT_TRUE(vm.add(holder()));
        const InstanceId id = vm.make("Holder");
        ASSERT_TRUE(vm.rt().setArrayProperty(id, "Flags", std::span<const PapyrusValue>{}));
        EXPECT_EQ(vm.runInt(id, "FlagLength"), 0);
    }

    TEST(PapyrusArrayProperty, anElementOfTheWrongKindAnUnknownNameAndANonArrayAreRefusedAndChangeNothing)
    {
        Vm vm(Target::SkyrimSE);
        ASSERT_TRUE(vm.add(holder()));
        const InstanceId id = vm.make("Holder");
        const std::vector<PapyrusValue> good{ PapyrusValue::makeInt(1), PapyrusValue::makeInt(2), PapyrusValue::makeInt(3) };
        ASSERT_TRUE(vm.rt().setArrayProperty(id, "Items", good));
        const std::vector<PapyrusValue> text{ PapyrusValue::makeString("no") };
        EXPECT_FALSE(vm.rt().setArrayProperty(id, "Items", text));
        EXPECT_FALSE(vm.rt().setArrayProperty(id, "Items", std::vector<PapyrusValue>{ PapyrusValue::makeForm("Form", 5) }));
        EXPECT_FALSE(vm.rt().setArrayProperty(id, "NoSuchProperty", good));
        EXPECT_FALSE(vm.rt().setArrayProperty(id, "Plain", good)) << "Plain is an Int";
        EXPECT_FALSE(vm.rt().setArrayProperty(0, "Items", good));
        EXPECT_FALSE(vm.rt().setArrayProperty(77, "Items", good));
        EXPECT_EQ(vm.runInt(id, "Sum"), 3006) << "the array set before is still there";
    }

    TEST(PapyrusArrayProperty, anArrayOverTheCapIsRefused)
    {
        Vm vm(Target::SkyrimSE);
        ASSERT_TRUE(vm.add(holder()));
        const InstanceId id = vm.make("Holder");
        const std::vector<PapyrusValue> within(128, PapyrusValue::makeInt(1));
        const std::vector<PapyrusValue> over(129, PapyrusValue::makeInt(1));
        EXPECT_TRUE(vm.rt().setArrayProperty(id, "Items", within));
        EXPECT_FALSE(vm.rt().setArrayProperty(id, "Items", over));
    }
}
