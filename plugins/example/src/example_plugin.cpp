// A small plugin using every service plugins have. See README.md for what it does and why it is here.
//
// It includes SDK headers only, and asks the host for everything. No exception may leave a plugin's callbacks in normal
// running; the host would catch one and disable the plugin.

#include <openeden/debug_draw.hpp>
#include <openeden/entity.hpp>
#include <openeden/event_bus.hpp>
#include <openeden/game_events.hpp>
#include <openeden/logging.hpp>
#include <openeden/physics.hpp>
#include <openeden/physics_events.hpp>
#include <openeden/plugin.hpp>
#include <openeden/profiling.hpp>
#include <openeden/save.hpp>
#include <openeden/settings.hpp>
#include <openeden/vfs.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kPluginId = "example";

    // How far above the player the downward casts start, and how long they are, in meters.
    constexpr float kCastHeightM = 10.0f;
    constexpr float kCastLengthM = 100.0f;

    // How far above the player the debug line is, in meters.
    constexpr float kDebugLineHeightM = 3.0f;

    // Version 1 wrote the count as one byte; version 2 as 32 bits. A plugin must read what older versions saved.
    constexpr std::uint32_t kSaveVersion = 2;

    /// Marked for forwarding, so global Lua scripts receive it as openeden_example_greeted. Its fields are flat:
    /// numbers, booleans and fixed char arrays only.
    struct GreetedEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 0x45584D01;

        std::int32_t count = 0;
        char text[32] = {};
    };

    class ExamplePlugin final : public openeden::sdk::Plugin,
                                public openeden::sdk::SaveChunkHandler,
                                public openeden::sdk::EntityLifecycleListener
    {
    public:
        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            // Plugins run per frame only; they get no fixed physics steps (decision 0003).
            registrar.registerPhaseCallback(openeden::sdk::Phase::PostSimulation, [this](float) { onFrame(); });
        }

        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

        void start(openeden::sdk::Host& host) override
        {
            mLogger = host.getLoggingService()->getLogger(kPluginId);
            mEvents = host.getEventBus();
            mPhysics = host.getPhysicsQueryService();
            mEntities = host.getEntityManager();
            mSettings = host.getSettingsService();
            mSaves = host.getSaveService();
            mDebugDraw = host.getDebugDrawService();
            mProfiler = host.getProfilerService();

            readGreeting(*host.getVfsService());

            mLogger->info("setting greeting_count=" + std::to_string(mSettings->getInt(kPluginId, "greeting_count", 1)));

            // The save service shares ownership, but this object outlives it: stop() below unregisters.
            mSaves->registerHandler(kPluginId, std::shared_ptr<openeden::sdk::SaveChunkHandler>(
                                                   std::shared_ptr<void>(), static_cast<SaveChunkHandler*>(this)));

            mEntities->registerLifecycleListener(kPluginId, this);

            const openeden::sdk::ForwardedField fields[] = {
                openeden::sdk::forwardedField("count", &GreetedEvent::count, offsetof(GreetedEvent, count)),
                openeden::sdk::forwardedField("text", &GreetedEvent::text, offsetof(GreetedEvent, text)),
            };
            mEvents->markForwardedToLua<GreetedEvent>(kPluginId, "greeted", fields);

            subscribe();
        }

        void stop() override
        {
            mLogger->info("entities spawned=" + std::to_string(mSpawned) + " freed=" + std::to_string(sFreed));
            mLogger->info("contacts=" + std::to_string(mContacts));

            for (const openeden::sdk::EventId id :
                { openeden::sdk::GameLoadedEvent::kEventId, openeden::sdk::GameSavedEvent::kEventId,
                    openeden::sdk::CellChangedEvent::kEventId, openeden::sdk::PhysicsContactBeginEvent::kEventId })
                mEvents->unsubscribe(id, kPluginId);
            mEntities->unregisterLifecycleListener(kPluginId);
            mSaves->unregisterHandler(kPluginId);
        }

        // SaveChunkHandler: on the main thread, between frames
        [[nodiscard]] std::uint32_t currentVersion() const noexcept override { return kSaveVersion; }

        void writeSaveChunk(openeden::sdk::SaveChunkWriter& writer) override
        {
            mLogger->info("saved count=" + std::to_string(mCount));
            writer.write(&mCount, sizeof(mCount));
        }

        void readSaveChunk(std::uint32_t chunkVersion, openeden::sdk::SaveChunkReader& reader) override
        {
            if (chunkVersion == 1)
            {
                std::uint8_t oldCount = 0;
                reader.read(&oldCount, sizeof(oldCount));
                mCount = oldCount;
            }
            else if (chunkVersion == kSaveVersion && reader.size() >= sizeof(mCount))
                reader.read(&mCount, sizeof(mCount));
            else
            {
                // From a newer version of this plugin, or damaged. Guessing could be worse than starting again.
                mLogger->warning("cannot read a save chunk of version " + std::to_string(chunkVersion));
                return;
            }
            mLogger->info("loaded count=" + std::to_string(mCount) + " version=" + std::to_string(chunkVersion));
        }

        // EntityLifecycleListener
        void onEntityLifecycle(openeden::sdk::EntityLifecycleEvent event, openeden::sdk::EntityId entity) override
        {
            if (event != openeden::sdk::EntityLifecycleEvent::Spawned)
                return;
            ++mSpawned;
            // The host frees this with the entity. The deleter's code lives in this library, which the host keeps
            // loaded until every entity's data has gone.
            mEntities->attachData(entity, "example.data", new int(0), [](void* data) {
                delete static_cast<int*>(data);
                ++sFreed;
            });
        }

    private:
        void readGreeting(openeden::sdk::VfsService& vfs)
        {
            const std::vector<std::uint8_t> bytes = vfs.readFile("example/greeting.txt");
            mGreeting.assign(bytes.begin(), bytes.end());
            while (!mGreeting.empty() && (mGreeting.back() == '\n' || mGreeting.back() == '\r'))
                mGreeting.pop_back();
            mLogger->info("greeting: " + mGreeting);
        }

        void subscribe()
        {
            mEvents->subscribe<openeden::sdk::GameLoadedEvent>(
                kPluginId, [this](const openeden::sdk::GameLoadedEvent&) { onGameLoaded(); });
            mEvents->subscribe<openeden::sdk::GameSavedEvent>(
                kPluginId, [this](const openeden::sdk::GameSavedEvent&) { mLogger->info("event game saved"); });
            mEvents->subscribe<openeden::sdk::CellChangedEvent>(
                kPluginId, [this](const openeden::sdk::CellChangedEvent& event) {
                    mLogger->info(std::string("event cell changed ") + event.cellName);
                });
            mEvents->subscribe<openeden::sdk::PhysicsContactBeginEvent>(
                kPluginId, [this](const openeden::sdk::PhysicsContactBeginEvent&) { ++mContacts; });
        }

        void onGameLoaded()
        {
            mLogger->info("event game loaded");

            const int gamesSeen = mSettings->getInt(kPluginId, "games_seen", 0) + 1;
            mSettings->setInt(kPluginId, "games_seen", gamesSeen);
            mLogger->info("setting games_seen=" + std::to_string(gamesSeen));

            // Only the main thread may publish a marked event. It reaches Lua when a game is running.
            ++mCount;
            GreetedEvent event;
            event.count = mCount;
            std::strncpy(event.text, mGreeting.c_str(), sizeof(event.text) - 1);
            mEvents->publishImmediate(event);
            mLogger->info("event sent count=" + std::to_string(mCount));

            mCastPending = true;
        }

        void castDownFrom(const openeden::sdk::PhysicsVec3& position)
        {
            // Names, not bits: the host says what a query can hit
            const openeden::sdk::QueryMask ground = mPhysics->queryMask("world") | mPhysics->queryMask("heightmap");
            const openeden::sdk::PhysicsVec3 origin{ position.x, position.y, position.z + kCastHeightM };
            const openeden::sdk::PhysicsVec3 down{ 0.0f, 0.0f, -kCastLengthM };

            const auto ray = mPhysics->castRay(origin, down, {}, ground);
            mLogger->info(std::string("ray hit=") + (ray ? "yes" : "no"));

            const auto shape = mPhysics->castShape(openeden::sdk::SphereShape{ 0.5f }, {}, origin, down, {}, ground);
            mLogger->info(std::string("shape hit=") + (shape ? "yes" : "no"));
        }

        void onFrame()
        {
            if (mProfiler == nullptr || mPhysics == nullptr)
                return;
            const openeden::sdk::ProfileScope scope(*mProfiler, kPluginId, "example.frame");

            const openeden::sdk::BodyId body = mPhysics->bodyOfEntity(mEntities->playerEntity());
            openeden::sdk::BodyState player;
            if (!mPhysics->readBody(body, player))
                return;

            if (mCastPending)
            {
                mCastPending = false;
                castDownFrom(player.positionM);
            }

            const openeden::sdk::Vector3 from{ player.positionM.x, player.positionM.y,
                player.positionM.z + kDebugLineHeightM };
            const openeden::sdk::Vector3 to{ player.positionM.x + 1.0f, player.positionM.y,
                player.positionM.z + kDebugLineHeightM };
            mDebugDraw->drawLine(kPluginId, from, to, openeden::sdk::Color4f{});

            if (!mReportedFrame)
            {
                mReportedFrame = true;
                mLogger->info("debug draw submitted");
                mLogger->info("profiler scope used");
            }
        }

        // Static so that the deleter above, a plain function, can reach it
        static inline int sFreed = 0;

        openeden::sdk::Logger* mLogger = nullptr;
        openeden::sdk::EventBus* mEvents = nullptr;
        openeden::sdk::PhysicsQueryService* mPhysics = nullptr;
        openeden::sdk::EntityManager* mEntities = nullptr;
        openeden::sdk::SettingsService* mSettings = nullptr;
        openeden::sdk::SaveService* mSaves = nullptr;
        openeden::sdk::DebugDrawService* mDebugDraw = nullptr;
        openeden::sdk::ProfilerService* mProfiler = nullptr;

        std::string mGreeting;
        std::int32_t mCount = 0;
        int mSpawned = 0;
        int mContacts = 0;
        bool mCastPending = false;
        bool mReportedFrame = false;
    };

    std::unique_ptr<openeden::sdk::Plugin> createPlugin()
    {
        return std::make_unique<ExamplePlugin>();
    }

    const openeden::sdk::PluginDescriptor sDescriptor{ openeden::sdk::kSdkVersion, kPluginId, &createPlugin };
}

extern "C" OPENEDEN_PLUGIN_EXPORT const openeden::sdk::PluginDescriptor* openeden_plugin_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}
