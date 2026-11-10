// Task 0098, move 8, stage 8c step 1: tests of MWWorld::CustomData::as<T>(), the template that replaces the twelve
// typed accessors (asNpcCustomData and so on) so that the core no longer names the data types of the classes in the
// game pack. The accessors' rule was: the owning class's own data returns itself, any other data throws
// std::logic_error whose text starts with "bad cast". as<T>() keeps both, and also accepts a derived type
// (dynamic_cast), which the old accessors had no need for.
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "apps/openmw/mwworld/customdata.hpp"

namespace
{
    struct DataA : MWWorld::CustomData
    {
        int mValue = 1;
        std::unique_ptr<MWWorld::CustomData> clone() const override { return std::make_unique<DataA>(*this); }
    };

    struct DataB : MWWorld::TypedCustomData<DataB>
    {
        int mOther = 2;
    };

    // not final: a derived type is also an A
    struct DataDerived : DataA
    {
        int mExtra = 3;
        std::unique_ptr<MWWorld::CustomData> clone() const override { return std::make_unique<DataDerived>(*this); }
    };

    TEST(CustomDataAs, returns_the_object_itself_for_the_exact_type)
    {
        DataA a;
        MWWorld::CustomData& base = a;
        EXPECT_EQ(&base.as<DataA>(), &a);
        base.as<DataA>().mValue = 7;
        EXPECT_EQ(a.mValue, 7);
    }

    TEST(CustomDataAs, const_version_returns_the_same_object)
    {
        DataB b;
        const MWWorld::CustomData& base = b;
        EXPECT_EQ(&base.as<DataB>(), &b);
        EXPECT_EQ(base.as<DataB>().mOther, 2);
    }

    TEST(CustomDataAs, another_type_throws_a_logic_error_that_says_bad_cast)
    {
        DataA a;
        MWWorld::CustomData& base = a;
        EXPECT_THROW(base.as<DataB>(), std::logic_error);
        try
        {
            base.as<DataB>();
            FAIL() << "no exception";
        }
        catch (const std::logic_error& e)
        {
            EXPECT_EQ(std::string(e.what()).rfind("bad cast", 0), 0u) << e.what();
        }
    }

    TEST(CustomDataAs, const_version_of_another_type_throws_too)
    {
        const DataB b;
        const MWWorld::CustomData& base = b;
        EXPECT_THROW(base.as<DataA>(), std::logic_error);
    }

    TEST(CustomDataAs, a_derived_object_is_also_its_base_type)
    {
        DataDerived d;
        MWWorld::CustomData& base = d;
        EXPECT_EQ(&base.as<DataDerived>(), &d);
        EXPECT_EQ(&base.as<DataA>(), static_cast<DataA*>(&d));
        EXPECT_EQ(base.as<DataA>().mValue, 1);
        EXPECT_THROW(base.as<DataB>(), std::logic_error);
    }

    TEST(CustomDataAs, a_base_object_is_not_its_derived_type)
    {
        DataA a;
        MWWorld::CustomData& base = a;
        EXPECT_THROW(base.as<DataDerived>(), std::logic_error);
    }

    TEST(CustomDataAs, clone_keeps_the_dynamic_type)
    {
        DataB b;
        b.mOther = 9;
        std::unique_ptr<MWWorld::CustomData> copy = b.clone();
        EXPECT_EQ(copy->as<DataB>().mOther, 9);
        EXPECT_NE(&copy->as<DataB>(), &b);
    }
}
