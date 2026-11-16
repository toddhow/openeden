// Task 0215: the pack's `skyrimse.presentation` namespace (Morrowind's music script gives way when the session has its
// own menus or music).
#include <game_skyrimse/presentation.hpp>

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace
{
    using namespace game_skyrimse::presentation;
    using openeden::sdk::BindingValue;

    class FakeBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            ++mDeclared;
            mName = std::string(d.name);
            mVersion = d.version;
            mMenuSafe = d.menuSafe;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        bool answer(const std::string& name)
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            if (it == mFunctions.end())
                return false;
            return it->second.handler->call({}).value.boolean;
        }
        int mDeclared = 0;
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        bool mMenuSafe = false;
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    TEST(SkyrimPresentation, flagsOwnTheMusicWhenMenusOrMusicIsOn)
    {
        EXPECT_FALSE((Flags{ false, false }.ownsMusic()));
        EXPECT_TRUE((Flags{ true, false }.ownsMusic()));
        EXPECT_TRUE((Flags{ false, true }.ownsMusic()));
        EXPECT_TRUE((Flags{ true, true }.ownsMusic()));
        EXPECT_FALSE((Flags{ false, false }.declared()));
        EXPECT_TRUE((Flags{ true, false }.declared()));
    }

    TEST(SkyrimPresentation, nothingIsDeclaredWithBothSettingsOff)
    {
        PresentationService service(Flags{ false, false });
        FakeBindings bindings;
        EXPECT_TRUE(service.declare(bindings).ok);
        EXPECT_EQ(bindings.mDeclared, 0);
    }

    TEST(SkyrimPresentation, theNamespaceIsMenuSafeAndAnswersTheFlags)
    {
        PresentationService service(Flags{ true, false });
        FakeBindings bindings;
        ASSERT_TRUE(service.declare(bindings).ok);
        EXPECT_EQ(bindings.mName, "skyrimse.presentation");
        EXPECT_EQ(bindings.mVersion.major, 1);
        EXPECT_TRUE(bindings.mMenuSafe);
        EXPECT_TRUE(bindings.answer("menus"));
        EXPECT_FALSE(bindings.answer("music"));
        EXPECT_TRUE(bindings.answer("owns_music"));
    }

    TEST(SkyrimPresentation, musicAloneAlsoOwnsTheMusic)
    {
        PresentationService service(Flags{ false, true });
        FakeBindings bindings;
        ASSERT_TRUE(service.declare(bindings).ok);
        EXPECT_FALSE(bindings.answer("menus"));
        EXPECT_TRUE(bindings.answer("music"));
        EXPECT_TRUE(bindings.answer("owns_music"));
    }
}
