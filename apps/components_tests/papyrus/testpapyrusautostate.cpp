// Task 0299: a script that extends another and names no auto state starts in its parent's auto state. The game's
// DartTrap extends TrapBase, whose `auto State Idle` holds OnActivate: without this the dart traps never heard the
// lever. Written before the fix and run red against the old runtime.

#include <gtest/gtest.h>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;

    ScriptBuilder baseWithAutoState(const std::string& autoState)
    {
        ScriptBuilder s("TrapBaseLike");
        s.autoState(autoState);
        s.function("Describe", "Int").ret(Int(1));
        s.state("Idle").function("Describe", "Int").ret(Int(2));
        s.state("Busy").function("Describe", "Int").ret(Int(3));
        return s;
    }

    TEST(PapyrusAutoState, a_child_with_no_auto_state_starts_in_its_parents)
    {
        for (const Target target : kBothTargets)
        {
            Vm vm(target);
            ASSERT_TRUE(vm.add(baseWithAutoState("Idle")));
            ScriptBuilder child("DartLike");
            child.parent("TrapBaseLike");
            child.function("Own", "Int").ret(Int(5));
            ASSERT_TRUE(vm.add(child));
            const InstanceId id = vm.make("DartLike");
            EXPECT_EQ(vm.rt().stateOf(id), "Idle");
            EXPECT_EQ(vm.runInt(id, "Describe"), 2) << "the parent's Idle version answers";
        }
    }

    TEST(PapyrusAutoState, a_child_with_an_auto_state_of_its_own_keeps_it)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(baseWithAutoState("Idle")));
        ScriptBuilder child("BusyDart");
        child.parent("TrapBaseLike");
        child.autoState("Busy");
        ASSERT_TRUE(vm.add(child));
        const InstanceId id = vm.make("BusyDart");
        EXPECT_EQ(vm.rt().stateOf(id), "Busy");
        EXPECT_EQ(vm.runInt(id, "Describe"), 3);
    }

    TEST(PapyrusAutoState, the_nearest_ancestor_with_an_auto_state_wins_over_a_farther_one)
    {
        Vm vm;
        ASSERT_TRUE(vm.add(baseWithAutoState("Idle")));
        ScriptBuilder middle("Middle");
        middle.parent("TrapBaseLike");
        middle.autoState("Busy");
        ASSERT_TRUE(vm.add(middle));
        ScriptBuilder leaf("Leaf");
        leaf.parent("Middle");
        ASSERT_TRUE(vm.add(leaf));
        const InstanceId id = vm.make("Leaf");
        EXPECT_EQ(vm.rt().stateOf(id), "Busy");
    }

    TEST(PapyrusAutoState, a_script_with_no_auto_state_anywhere_starts_in_the_empty_state)
    {
        Vm vm;
        ScriptBuilder base("Plain");
        base.function("Describe", "Int").ret(Int(1));
        ASSERT_TRUE(vm.add(base));
        ScriptBuilder child("PlainChild");
        child.parent("Plain");
        ASSERT_TRUE(vm.add(child));
        EXPECT_EQ(vm.rt().stateOf(vm.make("PlainChild")), "");
    }
}
