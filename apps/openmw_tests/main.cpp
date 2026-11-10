#include <components/debug/debugging.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/values.hpp>
#include <components/testing/util.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

namespace
{
    // Gives every test the same starting state of the process-wide random generators the game code draws from:
    // Misc::Rng::getGenerator (a flickering light's phase, a dice roll) and std::rand (Anim::AutoPlay's random phase
    // groups). Without it a test that depends on a draw, for example "a flickering light shows more than 10 different
    // colours in 30 frames", passes or fails with whatever the tests before it drew, so a shuffled order or
    // --gtest_repeat could fail it (seen with the actor parts tests). A test that wants its own sequence seeds its own
    // generator or calls Misc::Rng::init itself.
    constexpr unsigned sTestRngSeed = 1;

    class SeedRngListener : public testing::EmptyTestEventListener
    {
    public:
        void OnTestStart(const testing::TestInfo&) override
        {
            Misc::Rng::init(sTestRngSeed);
            std::srand(sTestRngSeed);
        }
    };
}

int main(int argc, char* argv[])
{
    Log::sMinDebugLevel = Debug::getDebugLevel();

    const std::filesystem::path settingsDefaultPath = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files"
        / Misc::StringUtils::stringToU8String("settings-default.cfg");

    Settings::SettingsFileParser parser;
    parser.loadSettingsFile(settingsDefaultPath, Settings::Manager::mDefaultSettings);

    Settings::StaticValues::initDefaults();

    Settings::Manager::mUserSettings = Settings::Manager::mDefaultSettings;

    Settings::StaticValues::init();

    testing::InitGoogleTest(&argc, argv);
    // gtest takes ownership of the listener.
    testing::UnitTest::GetInstance()->listeners().Append(new SeedRngListener);

    const int result = RUN_ALL_TESTS();
    if (result == 0)
        std::filesystem::remove_all(TestingOpenMW::outputDir());
    return result;
}
