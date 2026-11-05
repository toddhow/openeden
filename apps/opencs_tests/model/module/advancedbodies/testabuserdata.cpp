// Editor Module API 1.1, the user data folder query (task 0031 follow-up), and its use by the body package panel to
// open the merged body cache without OPENEDEN_CS_ASSETDIR. Written from the request alone, before the code.
//
// Assumptions where the request left room:
//  - EditorContext::userDataPath() is a const virtual returning std::filesystem::path; the default (a host that does
//    not know one, and ModuleRegistry's own do-nothing context) is an empty path.
//  - mergedCachePath(userData) is <userData>/openeden/bodies/current (what `openmw --bake-bodies` writes, see
//    components/body/bodycache.hpp).
//  - The command advancedbodies.openMerged is enabled when the host has a user data folder, and opens that file with
//    EditorContext::openAsset; with no such file it opens nothing and says why through log(). It never pushes a
//    command or marks anything dirty.
#include "apps/opencs/model/module/modulehost.hpp"
#include "apps/opencs/model/module/moduleregistry.hpp"

#include "editor_modules/advancedbodies_editor/documents.hpp"

#include "abtestsupport.hpp"

#include <openeden/editor_module/module.hpp>
#include <openeden/editor_module/registrar.hpp>
#include <openeden/editor_module/types.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ABTest
{
    namespace
    {
        using edit::AssetDocument;

        class UserDataContext final : public edit::EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<edit::Command>) override { ++mPushed; }
            void markDirty(std::string_view) override { ++mDirtyMarks; }
            void log(edit::LogLevel, std::string_view message) override { mLog.emplace_back(message); }
            void requestRedraw() override { ++mRedraws; }
            AssetDocument* openAsset(const std::filesystem::path& path) override
            {
                mOpened.push_back(path);
                return nullptr;
            }
            AssetDocument* activeAsset() override { return nullptr; }
            std::filesystem::path userDataPath() const override { return mUserData; }

            std::filesystem::path mUserData;
            std::vector<std::filesystem::path> mOpened;
            std::vector<std::string> mLog;
            int mPushed = 0;
            int mDirtyMarks = 0;
            int mRedraws = 0;
        };

        // A host as an API 1.0 test writes it: no userDataPath override.
        class OldContext final : public edit::EditorContext
        {
        public:
            void pushCommand(std::unique_ptr<edit::Command>) override {}
            void markDirty(std::string_view) override {}
            void log(edit::LogLevel, std::string_view) override {}
            void requestRedraw() override {}
            AssetDocument* openAsset(const std::filesystem::path&) override { return nullptr; }
            AssetDocument* activeAsset() override { return nullptr; }
        };

        TEST(UserDataPathApi, aHostThatDoesNotKnowOneGivesAnEmptyPath)
        {
            OldContext context;
            EXPECT_TRUE(context.userDataPath().empty());
            CSMModule::ModuleRegistry registry;
            EXPECT_TRUE(registry.context().userDataPath().empty()) << "the registry's own do-nothing context";
        }

        TEST(UserDataPathApi, aModuleSeesWhatTheHostSays)
        {
            // Through the registrar a module gets in registerUi: the registry hands out the context set on it.
            UserDataContext context;
            context.mUserData = "some/user/data";
            CSMModule::ModuleRegistry registry;
            registry.setContext(&context);
            EXPECT_EQ(registry.context().userDataPath(), std::filesystem::path("some/user/data"));
        }

        TEST(UserDataPathApi, theMergedCacheIsUnderOpenedenBodies)
        {
            EXPECT_EQ(ab::mergedCachePath("U"), std::filesystem::path("U") / "openeden" / "bodies" / "current");
            EXPECT_TRUE(ab::mergedCachePath({}).empty()) << "no user data folder, no path";
        }

        class AbOpenMergedTest : public AbTempTest
        {
        protected:
            void SetUp() override
            {
                AbTempTest::SetUp();
                mContext.mUserData = mDir;
                mRegistry.setContext(&mContext);
                mHost = std::make_unique<CSMModule::ModuleHost>(mRegistry, mContext);
                mHost->loadStatic(ab::kModuleId, *ab::moduleDescriptor(), CSMModule::ModuleSource::Origin::Core);
            }

            void TearDown() override
            {
                mHost.reset();
                AbTempTest::TearDown();
            }

            CSMModule::ModuleRegistry mRegistry;
            UserDataContext mContext;
            std::unique_ptr<CSMModule::ModuleHost> mHost;
        };

        TEST_F(AbOpenMergedTest, theCommandOpensTheCacheInTheUserDataFolder)
        {
            const std::filesystem::path current = ab::mergedCachePath(mDir);
            writeText(current, "x");
            EXPECT_TRUE(mRegistry.isCommandEnabled("advancedbodies.openMerged"));
            EXPECT_TRUE(mRegistry.runCommand("advancedbodies.openMerged"));
            ASSERT_EQ(mContext.mOpened.size(), 1u);
            EXPECT_EQ(mContext.mOpened[0], current);
            EXPECT_EQ(mContext.mPushed, 0);
            EXPECT_EQ(mContext.mDirtyMarks, 0);
        }

        TEST_F(AbOpenMergedTest, withoutACacheNothingIsOpenedAndTheLogSaysWhere)
        {
            EXPECT_TRUE(mRegistry.runCommand("advancedbodies.openMerged"));
            EXPECT_TRUE(mContext.mOpened.empty());
            ASSERT_FALSE(mContext.mLog.empty());
            EXPECT_TRUE(contains(mContext.mLog.back(), "bodies")) << mContext.mLog.back();
        }

        TEST_F(AbOpenMergedTest, withoutAUserDataFolderTheCommandIsDisabled)
        {
            mContext.mUserData.clear();
            EXPECT_FALSE(mRegistry.isCommandEnabled("advancedbodies.openMerged"));
            EXPECT_FALSE(mRegistry.runCommand("advancedbodies.openMerged"));
            EXPECT_TRUE(mContext.mOpened.empty());
        }
    }
}
