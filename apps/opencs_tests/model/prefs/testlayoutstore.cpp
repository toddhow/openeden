#include "apps/opencs/model/prefs/layoutstore.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace CSMPrefs
{
    namespace
    {
        class CSMPrefsLayoutStoreTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
                mDir = std::filesystem::temp_directory_path() / (std::string("eden-studio-layout-") + info->name());
                std::filesystem::remove_all(mDir);
                std::filesystem::create_directories(mDir);
                mFile = mDir / layoutStateFileName;
            }

            void TearDown() override { std::filesystem::remove_all(mDir); }

            std::filesystem::path mDir;
            std::filesystem::path mFile;
        };

        TEST_F(CSMPrefsLayoutStoreTest, defaultsToEdenStudioWithNoFile)
        {
            LayoutStore store(mFile);
            EXPECT_EQ(store.layout(), EditorLayout::EdenStudio);
            EXPECT_TRUE(store.dockState(EditorLayout::EdenStudio).isEmpty());
            EXPECT_TRUE(store.dockState(EditorLayout::Classic).isEmpty());
        }

        TEST_F(CSMPrefsLayoutStoreTest, remembersTheChosenLayoutAcrossInstances)
        {
            LayoutStore(mFile).setLayout(EditorLayout::Classic);
            EXPECT_EQ(LayoutStore(mFile).layout(), EditorLayout::Classic);

            LayoutStore(mFile).setLayout(EditorLayout::EdenStudio);
            EXPECT_EQ(LayoutStore(mFile).layout(), EditorLayout::EdenStudio);
        }

        TEST_F(CSMPrefsLayoutStoreTest, keepsDockStatePerLayout)
        {
            const QByteArray a("\x01\x02\x03 studio", 10);
            const QByteArray b("classic", 7);
            LayoutStore store(mFile);
            store.setDockState(EditorLayout::EdenStudio, a);
            store.setDockState(EditorLayout::Classic, b);

            LayoutStore again(mFile);
            EXPECT_EQ(again.dockState(EditorLayout::EdenStudio), a);
            EXPECT_EQ(again.dockState(EditorLayout::Classic), b);
        }

        TEST_F(CSMPrefsLayoutStoreTest, resetForgetsOnlyThatLayoutsState)
        {
            LayoutStore store(mFile);
            store.setDockState(EditorLayout::EdenStudio, QByteArray("a"));
            store.setDockState(EditorLayout::Classic, QByteArray("b"));
            store.clearDockState(EditorLayout::EdenStudio);

            EXPECT_TRUE(store.dockState(EditorLayout::EdenStudio).isEmpty());
            EXPECT_EQ(store.dockState(EditorLayout::Classic), QByteArray("b"));
        }

        TEST_F(CSMPrefsLayoutStoreTest, unknownNamesGiveTheDefault)
        {
            EXPECT_EQ(layoutFromName("classic"), EditorLayout::Classic);
            EXPECT_EQ(layoutFromName("eden-studio"), EditorLayout::EdenStudio);
            EXPECT_EQ(layoutFromName(""), EditorLayout::EdenStudio);
            EXPECT_EQ(layoutFromName("garbage"), EditorLayout::EdenStudio);
        }

        TEST_F(CSMPrefsLayoutStoreTest, unwritableLocationDoesNotThrow)
        {
            LayoutStore store(mDir / "no-such-folder" / "x.ini");
            EXPECT_NO_THROW(store.setLayout(EditorLayout::Classic));
            // Qt keeps the value for this process even when the file cannot be written, so only "no throw" is promised.
            EXPECT_NO_THROW(store.layout());
        }
    }
}
