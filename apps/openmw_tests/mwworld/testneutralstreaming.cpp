// Task 0357 (Skyrim streaming, step 1 of task 0323's plan): the cell preloader lists a neutral object's model, and a
// neutral session gets its own navmesh updater thread count. Written before the code; the first run was red.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <components/esm3/readerscache.hpp>
#include <components/settings/values.hpp>

#include "apps/openmw/mwclass/neutral.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutralcell.hpp"
#include "apps/openmw/mwworld/neutralnavthreads.hpp"
#include "apps/openmw/mwworld/neutralstore.hpp"
#include "apps/openmw/mwworld/ptr.hpp"

namespace
{
    using namespace openeden::sdk::game;

    struct PreloadGuard
    {
        bool mWorld = Settings::game().mNeutralWorld.get();
        bool mPreload = Settings::game().mNeutralPreloadModels.get();
        ~PreloadGuard()
        {
            Settings::game().mNeutralWorld.set(mWorld);
            Settings::game().mNeutralPreloadModels.set(mPreload);
        }
    };

    struct NeutralPreloadFixture : testing::Test
    {
        void SetUp() override { MWClass::Neutral::registerSelf(); }

        static WorldObject object(WorldId id, ObjectKind kind, const std::string& model)
        {
            WorldObject o;
            o.id = id;
            o.base = 100 + id;
            o.kind = kind;
            o.baseEditorId = "Base" + std::to_string(o.base);
            o.modelPath = model;
            o.transform.position = { double(id), 2, 3 };
            return o;
        }

        // id 1 static, 2 tree, 3 actor (live thing), 4 static with an editor marker model, 5 static with no model.
        std::vector<MWWorld::Ptr> load()
        {
            WorldCell c;
            c.id = 9;
            c.editorId = "PreloadInterior";
            c.interior = true;
            c.objects.push_back(object(1, ObjectKind::Static, "architecture\\wall01.nif"));
            c.objects.push_back(object(2, ObjectKind::Tree, "landscape\\trees\\pine01.nif"));
            c.objects.push_back(object(3, ObjectKind::Actor, "actors\\character\\character assets\\skeleton.nif"));
            c.objects.push_back(object(4, ObjectKind::Static, "marker_error.nif"));
            c.objects.push_back(object(5, ObjectKind::Static, ""));
            for (const WorldObject& o : c.objects)
                EXPECT_NE(mStore.getWritable<MWWorld::NeutralBase>().addFromObject("skyrimse", o).mBase, nullptr);
            const ESM::Cell record = MWWorld::makeNeutralCellRecord("skyrimse", c);
            mCell = std::make_unique<MWWorld::CellStore>(MWWorld::Cell(record), mStore, mReaders);
            EXPECT_TRUE(mCell->setNeutralCell("skyrimse", c));
            mCell->load();
            std::vector<MWWorld::Ptr> result;
            mCell->forEach(
                [&](const MWWorld::Ptr& ptr) {
                    result.push_back(ptr);
                    return true;
                },
                true);
            return result;
        }

        static std::vector<std::string> modelsOf(const MWWorld::Ptr& ptr)
        {
            std::vector<std::string_view> views;
            ptr.getClass().getModelsToPreload(ptr, views);
            return std::vector<std::string>(views.begin(), views.end());
        }

        MWWorld::ESMStore mStore;
        ESM::ReadersCache mReaders;
        std::unique_ptr<MWWorld::CellStore> mCell;
    };

    TEST_F(NeutralPreloadFixture, a_neutral_session_lists_the_model_of_a_static_and_a_tree)
    {
        PreloadGuard guard;
        Settings::game().mNeutralWorld.set(true);
        Settings::game().mNeutralPreloadModels.set(true);
        const auto ptrs = load();
        ASSERT_EQ(ptrs.size(), 5u);
        std::vector<std::string> all;
        for (const auto& ptr : ptrs)
            for (const auto& m : modelsOf(ptr))
                all.push_back(m);
        ASSERT_EQ(all.size(), 2u);
        EXPECT_EQ(all[0], "architecture\\wall01.nif");
        EXPECT_EQ(all[1], "landscape\\trees\\pine01.nif");
    }

    TEST_F(NeutralPreloadFixture, an_actor_a_marker_and_an_empty_model_list_nothing)
    {
        PreloadGuard guard;
        Settings::game().mNeutralWorld.set(true);
        Settings::game().mNeutralPreloadModels.set(true);
        const auto ptrs = load();
        EXPECT_TRUE(modelsOf(ptrs[2]).empty());
        EXPECT_TRUE(modelsOf(ptrs[3]).empty());
        EXPECT_TRUE(modelsOf(ptrs[4]).empty());
    }

    TEST_F(NeutralPreloadFixture, the_list_is_what_getModel_returns_and_stays_valid_after_the_call)
    {
        PreloadGuard guard;
        Settings::game().mNeutralWorld.set(true);
        Settings::game().mNeutralPreloadModels.set(true);
        const auto ptrs = load();
        std::vector<std::string_view> views;
        ptrs[0].getClass().getModelsToPreload(ptrs[0], views);
        ASSERT_EQ(views.size(), 1u);
        EXPECT_EQ(views[0].data(), ptrs[0].getClass().getModel(ptrs[0]).data());
    }

    TEST_F(NeutralPreloadFixture, the_kill_switch_and_a_session_without_a_neutral_world_list_nothing)
    {
        PreloadGuard guard;
        const auto ptrs = load();
        Settings::game().mNeutralWorld.set(true);
        Settings::game().mNeutralPreloadModels.set(false);
        EXPECT_TRUE(modelsOf(ptrs[0]).empty());
        Settings::game().mNeutralWorld.set(false);
        Settings::game().mNeutralPreloadModels.set(true);
        EXPECT_TRUE(modelsOf(ptrs[0]).empty());
    }

    TEST(NeutralNavThreads, a_neutral_session_takes_its_own_count)
    {
        EXPECT_EQ(MWWorld::chooseNavMeshUpdaterThreads(true, 1, 4), 4u);
        EXPECT_EQ(MWWorld::chooseNavMeshUpdaterThreads(true, 2, 6), 6u);
    }

    TEST(NeutralNavThreads, a_session_without_a_neutral_world_keeps_the_normal_count)
    {
        EXPECT_EQ(MWWorld::chooseNavMeshUpdaterThreads(false, 1, 4), 1u);
        EXPECT_EQ(MWWorld::chooseNavMeshUpdaterThreads(false, 3, 4), 3u);
    }

    TEST(NeutralNavThreads, a_count_below_one_is_one)
    {
        EXPECT_EQ(MWWorld::chooseNavMeshUpdaterThreads(true, 1, 0), 1u);
        EXPECT_EQ(MWWorld::chooseNavMeshUpdaterThreads(false, 0, 4), 1u);
    }
}
