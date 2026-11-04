#include <components/misc/quietwindow.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <string>

#include <stdlib.h>

namespace
{
    // OPENEDEN_QUIET_WINDOW (2026-11-04): one reading of the variable for the game's window, Eden Studio's windows and
    // the crash catcher, whose message boxes are not shown when it is set. The rule is the one the game had in
    // apps/openmw/vsgengine.cpp: unset, empty or exactly "0" means off, anything else on.

    // Sets or removes OPENEDEN_QUIET_WINDOW for one test, and puts the old value back.
    class ScopedQuietVariable
    {
    public:
        explicit ScopedQuietVariable(const char* value)
        {
            if (const char* old = std::getenv(Misc::quietWindowVariable))
                mOld = old;
            set(value);
        }

        ~ScopedQuietVariable() { set(mOld ? mOld->c_str() : nullptr); }

        ScopedQuietVariable(const ScopedQuietVariable&) = delete;
        ScopedQuietVariable& operator=(const ScopedQuietVariable&) = delete;

    private:
        static void set(const char* value)
        {
#ifdef _WIN32
            // An empty value removes the variable on Windows.
            _putenv_s(Misc::quietWindowVariable, value != nullptr ? value : "");
#else
            if (value != nullptr)
                setenv(Misc::quietWindowVariable, value, 1);
            else
                unsetenv(Misc::quietWindowVariable);
#endif
        }

        std::optional<std::string> mOld;
    };

    TEST(MiscQuietWindowTest, the_variable_is_the_one_the_check_scripts_set)
    {
        EXPECT_STREQ(Misc::quietWindowVariable, "OPENEDEN_QUIET_WINDOW");
    }

    TEST(MiscQuietWindowTest, unset_empty_and_zero_mean_off)
    {
        EXPECT_FALSE(Misc::isQuietWindowValue(nullptr));
        EXPECT_FALSE(Misc::isQuietWindowValue(""));
        EXPECT_FALSE(Misc::isQuietWindowValue("0"));
    }

    TEST(MiscQuietWindowTest, any_other_value_means_on)
    {
        EXPECT_TRUE(Misc::isQuietWindowValue("1"));
        EXPECT_TRUE(Misc::isQuietWindowValue("yes"));
        EXPECT_TRUE(Misc::isQuietWindowValue("true"));
        // Not a boolean: only an exact "0" is off.
        EXPECT_TRUE(Misc::isQuietWindowValue("false"));
        EXPECT_TRUE(Misc::isQuietWindowValue("00"));
        EXPECT_TRUE(Misc::isQuietWindowValue(" 0"));
        EXPECT_TRUE(Misc::isQuietWindowValue("0 "));
    }

    TEST(MiscQuietWindowTest, the_environment_is_read_with_the_same_rule)
    {
        {
            const ScopedQuietVariable variable(nullptr);
            EXPECT_FALSE(Misc::isQuietWindowRequested());
        }
        {
            const ScopedQuietVariable variable("0");
            EXPECT_FALSE(Misc::isQuietWindowRequested());
        }
        {
            const ScopedQuietVariable variable("1");
            EXPECT_TRUE(Misc::isQuietWindowRequested());
        }
        {
            const ScopedQuietVariable variable("yes");
            EXPECT_TRUE(Misc::isQuietWindowRequested());
        }
    }

    TEST(MiscQuietWindowTest, the_environment_is_read_on_every_call)
    {
        const ScopedQuietVariable outer("1");
        EXPECT_TRUE(Misc::isQuietWindowRequested());
        {
            const ScopedQuietVariable inner("0");
            EXPECT_FALSE(Misc::isQuietWindowRequested());
        }
        EXPECT_TRUE(Misc::isQuietWindowRequested());
    }
}
