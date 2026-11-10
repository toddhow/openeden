// A small extension using every service extensions have. See README.md for what it does and why it is here.
//
// It includes SDK headers only, and asks the host for everything. It has no way to affect a saved game (decision 0005):
// what it keeps goes in its settings. No exception may leave an extension's callbacks in normal
// running; the host would catch one and disable the extension.

#include <openeden/debug_draw.hpp>
#include <openeden/entity.hpp>
#include <openeden/event_bus.hpp>
#include <openeden/extension.hpp>
#include <openeden/game_events.hpp>
#include <openeden/logging.hpp>
#include <openeden/physics.hpp>
#include <openeden/physics_events.hpp>
#include <openeden/profiling.hpp>
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
    constexpr std::string_view kExtensionId = "example";

    // How far above the player the downward casts start, and how long they are, in meters.
    constexpr float kCastHeightM = 10.0f;
    constexpr float kCastLengthM = 100.0f;

    // How far above the player the debug line is, in meters.
    constexpr float kDebugLineHeightM = 3.0f;

    /// Marked for forwarding, so global Lua scripts receive it as openeden_example_greeted. Its fields are flat:
    /// numbers, booleans and fixed char arrays only.
    struct GreetedEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 0x45584D01;

        std::int32_t count = 0;
        char text[32] = {};
    };

    class ExampleExtension final : public openeden::sdk::Extension, public openeden::sdk::EntityLifecycleListener
    {
    public:
        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            // Extensions run per frame only; they get no fixed physics steps (decision 0003).
            registrar.registerPhaseCallback(openeden::sdk::Phase::PostSimulation, [this](float) { onFrame(); });
        }

        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

        void start(openeden::sdk::Host& host) override
        {
            mLogger = host.getLoggingService()->getLogger(kExtensionId);
            mEvents = host.getEventBus();
            mPhysics = host.getPhysicsQueryService();
            mEntities = host.getEntityManager();
            mSettings = host.getSettingsService();
            mDebugDraw = host.getDebugDrawService();
            mProfiler = host.getProfilerService();

            readGreeting(*host.getVfsService());

            mLogger->info(
                "setting greeting_count=" + std::to_string(mSettings->getInt(kExtensionId, "greeting_count", 1)));

            mEntities->registerLifecycleListener(kExtensionId, this);

            const openeden::sdk::ForwardedField fields[] = {
                openeden::sdk::forwardedField("count", &GreetedEvent::count, offsetof(GreetedEvent, count)),
                openeden::sdk::forwardedField("text", &GreetedEvent::text, offsetof(GreetedEvent, text)),
            };
            mEvents->markForwardedToLua<GreetedEvent>(kExtensionId, "greeted", fields);

            subscribe();
        }

        void stop() override
        {
            mLogger->info("entities spawned=" + std::to_string(mSpawned) + " freed=" + std::to_string(sFreed));
            mLogger->info("contacts=" + std::to_string(mContacts));

            for (const openeden::sdk::EventId id :
                { openeden::sdk::GameLoadedEvent::kEventId, openeden::sdk::GameSavedEvent::kEventId,
                    openeden::sdk::CellChangedEvent::kEventId, openeden::sdk::PhysicsContactBeginEvent::kEventId })
                mEvents->unsubscribe(id, kExtensionId);
            mEntities->unregisterLifecycleListener(kExtensionId);
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
                kExtensionId, [this](const openeden::sdk::GameLoadedEvent&) { onGameLoaded(); });
            mEvents->subscribe<openeden::sdk::GameSavedEvent>(
                kExtensionId, [this](const openeden::sdk::GameSavedEvent&) { mLogger->info("event game saved"); });
            mEvents->subscribe<openeden::sdk::CellChangedEvent>(
                kExtensionId, [this](const openeden::sdk::CellChangedEvent& event) {
                    mLogger->info(std::string("event cell changed ") + event.cellName);
                });
            mEvents->subscribe<openeden::sdk::PhysicsContactBeginEvent>(
                kExtensionId, [this](const openeden::sdk::PhysicsContactBeginEvent&) { ++mContacts; });
        }

        void onGameLoaded()
        {
            mLogger->info("event game loaded");

            const int gamesSeen = mSettings->getInt(kExtensionId, "games_seen", 0) + 1;
            mSettings->setInt(kExtensionId, "games_seen", gamesSeen);
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
            const openeden::sdk::ProfileScope scope(*mProfiler, kExtensionId, "example.frame");

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
            mDebugDraw->drawLine(kExtensionId, from, to, openeden::sdk::Color4f{});

            // Shapes about 4 m in front of a player facing north (+y), one of each kind: a box standing on the ground
            // (depth tested), a sphere and a point drawn over everything, and a label (the game marks it with a small
            // cross). These are the shapes the game draws since task 0017's remaining work.
            const openeden::sdk::Vector3 p{ player.positionM.x, player.positionM.y, player.positionM.z };
            mDebugDraw->drawBox(kExtensionId, { p.x - 0.5f, p.y + 3.5f, p.z }, { p.x + 0.5f, p.y + 4.5f, p.z + 1.8f },
                openeden::sdk::Color4f{ 0.2f, 1.0f, 0.2f, 1.0f });
            mDebugDraw->drawSphere(kExtensionId, { p.x + 2.5f, p.y + 4.0f, p.z + 1.5f }, 1.2f,
                openeden::sdk::Color4f{ 1.0f, 0.5f, 0.1f, 1.0f }, false);
            mDebugDraw->drawPoint(kExtensionId, { p.x - 2.5f, p.y + 4.0f, p.z + 1.5f },
                openeden::sdk::Color4f{ 1.0f, 0.1f, 0.1f, 1.0f }, 4.0f, false);
            mDebugDraw->drawText(kExtensionId, { p.x, p.y + 4.0f, p.z + 2.2f }, "example",
                openeden::sdk::Color4f{ 0.3f, 0.6f, 1.0f, 1.0f }, 1.0f, false);
            mDebugDraw->drawLine(kExtensionId, { p.x, p.y + 1.0f, p.z + 1.0f }, { p.x, p.y + 4.0f, p.z + 1.0f },
                openeden::sdk::Color4f{ 1.0f, 1.0f, 0.2f, 1.0f }, 1.0f, false);

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
        openeden::sdk::DebugDrawService* mDebugDraw = nullptr;
        openeden::sdk::ProfilerService* mProfiler = nullptr;

        std::string mGreeting;
        std::int32_t mCount = 0;
        int mSpawned = 0;
        int mContacts = 0;
        bool mCastPending = false;
        bool mReportedFrame = false;
    };

    std::unique_ptr<openeden::sdk::Extension> createExtension()
    {
        return std::make_unique<ExampleExtension>();
    }

    const openeden::sdk::ExtensionDescriptor sDescriptor{ openeden::sdk::kSdkVersion, kExtensionId, &createExtension };
}

extern "C" OPENEDEN_EXTENSION_EXPORT const openeden::sdk::ExtensionDescriptor* openeden_extension_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}
