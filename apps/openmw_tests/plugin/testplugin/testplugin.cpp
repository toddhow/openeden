// A plugin built against the SDK only, compiled once per test ID (see apps/openmw_tests/CMakeLists.txt). It writes what
// happens to it to the file named by OPENEDEN_TEST_PLUGIN_LOG, so a test can read the order after the fact.
//
// Compile definitions:
//   OPENEDEN_TEST_PLUGIN_ID          the plugin's ID, as a string
//   OPENEDEN_TEST_PLUGIN_PROVIDES_A  provide the contract "openeden.test.a" v1.0
//   OPENEDEN_TEST_PLUGIN_REQUIRES_A  require that contract
//   OPENEDEN_TEST_PLUGIN_USES_KF     require openeden.compat.kf_converter and, in resolve, call it with a small .kf
//
// OPENEDEN_TEST_PLUGIN_THROW="<id>:start" or "<id>:phase" makes this plugin throw at that point.
//
// OPENEDEN_TEST_PLUGIN_WORLD="<x> <y>", a point in meters, makes it observe the game world and cast down over that point
// once the player rests there, for tools/plugin_world_check/check.sh.

#include <openeden/entity.hpp>
#include <openeden/event_bus.hpp>
#include <openeden/game_events.hpp>
#include <openeden/logging.hpp>
#include <openeden/physics.hpp>
#include <openeden/physics_events.hpp>
#include <openeden/plugin.hpp>
#include <openeden/save.hpp>
#include <openeden/settings.hpp>
#include <openeden/vfs.hpp>

#ifdef OPENEDEN_TEST_PLUGIN_USES_KF
#include <openeden/contracts/kf_converter_contract.hpp>

#include <span>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
    constexpr std::string_view kPluginId = OPENEDEN_TEST_PLUGIN_ID;
    // The answer the provider gives and the consumer checks, to prove a contract call crosses between libraries.
    [[maybe_unused]] constexpr int kContractAnswer = 42;

    void record(std::string_view event)
    {
        // A sink can be called from any thread that logs.
        static std::mutex mutex;
        const char* const path = std::getenv("OPENEDEN_TEST_PLUGIN_LOG");
        if (path == nullptr)
            return;
        const std::lock_guard lock(mutex);
        std::ofstream file(path, std::ios::app | std::ios::binary);
        file << kPluginId << ' ' << event << '\n';
    }

    void throwIfAsked(std::string_view stage)
    {
        const char* const value = std::getenv("OPENEDEN_TEST_PLUGIN_THROW");
        if (value == nullptr)
            return;
        const std::string wanted = std::string(kPluginId) + ':' + std::string(stage);
        if (wanted == value)
            throw std::runtime_error("test throw " + wanted);
    }

}

// A contract is shared between libraries, so it must not be in an anonymous namespace, where each library would have a
// type of its own. In the plugin that only calls it, GCC then sees no class that implements it and calls the pure
// virtual function directly. Real contracts are in openeden::contracts.
namespace openeden_test_contracts
{
    class TestAContract
    {
    public:
        static constexpr std::string_view kId = "openeden.test.a";
        static constexpr openeden::sdk::Version kVersion{ 1, 0 };

        virtual ~TestAContract() = default;
        [[nodiscard]] virtual int answer() const = 0;
    };
}

namespace
{
    using openeden_test_contracts::TestAContract;

#ifdef OPENEDEN_TEST_PLUGIN_PROVIDES_A
    class TestAImplementation final : public TestAContract
    {
    public:
        [[nodiscard]] int answer() const override { return kContractAnswer; }
    };
#endif

#ifdef OPENEDEN_TEST_PLUGIN_USES_KF
    // Writes the little-endian fields of a NIF file, as Nif::Reader::parse reads them.
    class NifBytes
    {
    public:
        void u32(std::uint32_t value) { append(&value, sizeof(value)); }
        void u16(std::uint16_t value) { append(&value, sizeof(value)); }
        void i32(std::int32_t value) { append(&value, sizeof(value)); }
        void f32(float value) { append(&value, sizeof(value)); }

        void raw(std::string_view text) { append(text.data(), text.size()); }

        void sizedString(std::string_view text)
        {
            u32(static_cast<std::uint32_t>(text.size()));
            raw(text);
        }

        [[nodiscard]] std::span<const std::uint8_t> bytes() const { return mBytes; }

    private:
        void append(const void* data, std::size_t size)
        {
            const auto* const begin = static_cast<const std::uint8_t*>(data);
            mBytes.insert(mBytes.end(), begin, begin + size);
        }

        std::vector<std::uint8_t> mBytes;
    };

    // A .kf at Morrowind's version: text keys for one clip "idle", and one bone moving for a second. Field order from
    // components/nif: Reader::parse, NiObjectNET::read, Extra::read, NiTextKeyExtraData::read, TypedExtra::read,
    // NiTimeController::read, NiKeyframeController::read, NiKeyframeData::read and KeyMapT::read.
    NifBytes makeSyntheticKf()
    {
        constexpr std::int32_t kNone = -1;
        constexpr std::uint32_t kVersion4002 = 0x04000002;
        constexpr std::uint32_t kInterpolationLinear = 1;
        constexpr std::uint16_t kControllerActive = 8;

        NifBytes nif;
        nif.raw("NetImmerse File Format, Version 4.0.0.2\n");
        nif.u32(kVersion4002);
        nif.u32(5); // records

        // 0: the root, with the first extra and the first controller
        nif.sizedString("NiSequenceStreamHelper");
        nif.sizedString("synthetic");
        nif.i32(1);
        nif.i32(3);

        // 1: text keys, then the bone's name
        nif.sizedString("NiTextKeyExtraData");
        nif.i32(2);
        nif.u32(0);
        nif.u32(2);
        nif.f32(0.0f);
        nif.sizedString("idle: start");
        nif.f32(1.0f);
        nif.sizedString("idle: stop");

        // 2: the name of the bone controller 3 moves
        nif.sizedString("NiStringExtraData");
        nif.i32(kNone);
        nif.u32(0);
        nif.sizedString("bip01");

        // 3: the controller
        nif.sizedString("NiKeyframeController");
        nif.i32(kNone);
        nif.u16(kControllerActive);
        nif.f32(1.0f);
        nif.f32(0.0f);
        nif.f32(0.0f);
        nif.f32(1.0f);
        nif.i32(kNone);
        nif.i32(4);

        // 4: no rotation keys, two translation keys, no scale keys
        nif.sizedString("NiKeyframeData");
        nif.u32(0);
        nif.u32(2);
        nif.u32(kInterpolationLinear);
        for (const float time : { 0.0f, 1.0f })
        {
            nif.f32(time);
            nif.f32(0.0f);
            nif.f32(70.0f * time);
            nif.f32(70.0f);
        }
        nif.u32(0);

        nif.u32(1); // roots
        nif.i32(0);
        return nif;
    }

    bool callConverter(openeden::contracts::KfConverterContract& converter)
    {
        using openeden::contracts::KfConverterContract;
        constexpr std::string_view kGlbMagic = "glTF";

        const NifBytes kf = makeSyntheticKf();
        const KfConverterContract::ConversionResult result = converter.convertKfToGltf(kf.bytes(), {});
        if (!result.success)
        {
            record("kf failed conversion: " + result.errorMessage);
            return false;
        }
        record("kf converted");

        const bool magicOk = result.glbData.size() >= kGlbMagic.size()
            && std::memcmp(result.glbData.data(), kGlbMagic.data(), kGlbMagic.size()) == 0;
        record(magicOk ? "kf glb magic ok" : "kf failed glb magic");
        record(result.sidecarJson.find("\"idle\"") != std::string::npos ? "kf sidecar names clip idle"
                                                                         : "kf failed sidecar: " + result.sidecarJson);

        NifBytes garbage;
        garbage.raw("this is not a nif file");
        const KfConverterContract::ConversionResult refused = converter.convertKfToGltf(garbage.bytes(), {});
        record(!refused.success && !refused.errorMessage.empty() ? "kf garbage refused" : "kf failed garbage accepted");
        return true;
    }
#endif

    // With OPENEDEN_TEST_PLUGIN_DATA, the plugin uses the data services, for tools/plugin_data_check/check.sh.
    constexpr const char* kChunkText = "chunk-from-plugin";
    constexpr std::uint32_t kChunkVersion = 3;

    class TestSaveHandler final : public openeden::sdk::SaveChunkHandler
    {
    public:
        [[nodiscard]] std::uint32_t currentVersion() const noexcept override { return kChunkVersion; }

        void writeSaveChunk(openeden::sdk::SaveChunkWriter& writer) override
        {
            writer.write(kChunkText, std::strlen(kChunkText));
        }

        void readSaveChunk(std::uint32_t chunkVersion, openeden::sdk::SaveChunkReader& reader) override
        {
            std::string text(reader.size(), '\0');
            reader.read(text.data(), text.size());
            record("chunk v" + std::to_string(chunkVersion) + " " + text);
        }
    };

    struct PingEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 0x54455354;

        std::int32_t count = 0;
        float value = 0.5f;
        bool flag = true;
        char text[16] = {};
    };

    struct UnmarkedEvent
    {
        static constexpr openeden::sdk::EventId kEventId = 0x54455355;

        std::int32_t count = 0;
    };

    // Set while started. With OPENEDEN_TEST_PLUGIN_SINK_LOGS, the sink logs while it receives a line, which must not hang.
    openeden::sdk::Logger* sSinkLogger = nullptr;

    class RecordingSink final : public openeden::sdk::LogSink
    {
    public:
        void log(std::string_view /*pluginId*/, openeden::sdk::LogLevel /*level*/, std::string_view message) override
        {
            // Engine log lines arrive with their line ending.
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.remove_suffix(1);
            record("sink " + std::string(message));
            if (sSinkLogger != nullptr && std::getenv("OPENEDEN_TEST_PLUGIN_SINK_LOGS") != nullptr)
                sSinkLogger->info("line from inside the sink");
        }
    };

    struct PhaseName
    {
        openeden::sdk::Phase mPhase;
        const char* mName;
    };

    constexpr PhaseName kPhases[] = {
        { openeden::sdk::Phase::Input, "Input" },
        { openeden::sdk::Phase::PreSimulation, "PreSimulation" },
        { openeden::sdk::Phase::PostSimulation, "PostSimulation" },
        { openeden::sdk::Phase::Animation, "Animation" },
        { openeden::sdk::Phase::PreRender, "PreRender" },
        { openeden::sdk::Phase::Ui, "Ui" },
    };

    // With OPENEDEN_TEST_PLUGIN_WORLD: one line per event, prefixed "world ", read by tools/plugin_world_check/check.sh.
    constexpr std::string_view kWorldDataSlot = "world_check";
    // A fixed segment around the height where the Lua integration tests place the player (1745 units, about 25 m), so the
    // fractions depend only on the point cast over and the scene, never on where the player happens to rest.
    constexpr float kProbeTopM = 60.0f;
    constexpr float kProbeLengthM = 120.0f;
    constexpr float kProbeSphereRadiusM = 0.25f;
    // How close the player must be to the point, and for how many frames its body must not move.
    constexpr float kProbeReachM = 1.0f;
    constexpr int kProbeSettledFrames = 30;
    constexpr int kFloatDigits = 9;

    std::string textOf(openeden::sdk::EntityId id)
    {
        return std::to_string(id.index) + ':' + std::to_string(id.generation);
    }

    std::string textOf(openeden::sdk::BodyId id)
    {
        return std::to_string(id.index) + ':' + std::to_string(id.generation);
    }

    std::string textOf(float value)
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.*g", kFloatDigits, value);
        return buffer;
    }

    std::string textOf(const openeden::sdk::PhysicsVec3& value)
    {
        return textOf(value.x) + ' ' + textOf(value.y) + ' ' + textOf(value.z);
    }

    const char* nameOf(openeden::sdk::EntityLifecycleEvent event)
    {
        using openeden::sdk::EntityLifecycleEvent;
        switch (event)
        {
            case EntityLifecycleEvent::Spawned:
                return "Spawned";
            case EntityLifecycleEvent::CellLoaded:
                return "CellLoaded";
            case EntityLifecycleEvent::Died:
                return "Died";
            case EntityLifecycleEvent::Revived:
                return "Revived";
            case EntityLifecycleEvent::CellUnloaded:
                return "CellUnloaded";
            case EntityLifecycleEvent::Despawned:
                return "Despawned";
        }
        return "Unknown";
    }

    // Per-entity data, to see when the core frees it.
    struct WorldEntityTag
    {
        openeden::sdk::EntityId mId;
    };

    int sWorldDeleters = 0;

    void deleteWorldEntityTag(void* data)
    {
        // Owned by the entity manager since attachData succeeded. Delete is the deleter's job in this API.
        const std::unique_ptr<WorldEntityTag> tag(static_cast<WorldEntityTag*>(data));
        ++sWorldDeleters;
        record("world deleter " + textOf(tag->mId));
    }

    // Owned by TestPlugin between start and stop. All callbacks must run on the thread that called start.
    class WorldChecker final : public openeden::sdk::EntityLifecycleListener
    {
    public:
        WorldChecker(openeden::sdk::Host& host, const char* probe)
            : mMainThread(std::this_thread::get_id())
            , mEvents(host.getEventBus())
            , mEntities(host.getEntityManager())
            , mPhysics(host.getPhysicsQueryService())
        {
            if (std::sscanf(probe, "%f %f", &mProbeXM, &mProbeYM) != 2)
                record("world bad-probe");
            if (mEvents == nullptr || mEntities == nullptr || mPhysics == nullptr)
            {
                record("world missing-service");
                return;
            }

            const openeden::sdk::QueryMask worldMask = mPhysics->queryMask("world");
            const openeden::sdk::QueryMask heightmapMask = mPhysics->queryMask("heightmap");
            mActorMask = mPhysics->queryMask("actor");
            mGroundMask = worldMask | heightmapMask;
            record(worldMask != openeden::sdk::kQueryMaskNone && heightmapMask != openeden::sdk::kQueryMaskNone
                        && mActorMask != openeden::sdk::kQueryMaskNone
                    ? "world masks-published"
                    : "world mask-missing");

            mEntities->registerLifecycleListener(kPluginId, this);
            subscribeAll();
        }

        ~WorldChecker() override
        {
            if (mEvents == nullptr || mEntities == nullptr || mPhysics == nullptr)
                return;
            mEntities->unregisterLifecycleListener(kPluginId);
            unsubscribeAll();
            record("world final events=" + std::to_string(mEventCount) + " spawned=" + std::to_string(mSpawned)
                + " despawned=" + std::to_string(mDespawned) + " deleters=" + std::to_string(sWorldDeleters));
        }

        WorldChecker(const WorldChecker&) = delete;
        WorldChecker& operator=(const WorldChecker&) = delete;

        void onEntityLifecycle(openeden::sdk::EntityLifecycleEvent event, openeden::sdk::EntityId entityId) override
        {
            using openeden::sdk::EntityLifecycleEvent;
            note("life");
            record(std::string("world life ") + nameOf(event) + ' ' + textOf(entityId) + ' '
                + mEntities->recordId(entityId));
            if (event == EntityLifecycleEvent::Despawned)
                ++mDespawned;
            if (event != EntityLifecycleEvent::Spawned || !entityId.isValid())
                return;
            ++mSpawned;
            auto tag = std::make_unique<WorldEntityTag>();
            tag->mId = entityId;
            if (mEntities->attachData(entityId, kWorldDataSlot, tag.get(), &deleteWorldEntityTag))
                tag.release();
            else
                record("world attach-failed " + textOf(entityId));
        }

        /// Call once per simulated frame. It allocates only in the frames it records something.
        void onFrame()
        {
            noteThreadOnly("phase");
            // After every load, since the player gets a new body each time (found in review of task 0017)
            if (mPhysics != nullptr && mCheckPlayerBody)
            {
                mCheckPlayerBody = false;
                const openeden::sdk::EntityId loadedPlayer = mEntities->playerEntity();
                const openeden::sdk::BodyId loadedBody = mPhysics->bodyOfEntity(loadedPlayer);
                record(mPhysics->isBodyValid(loadedBody) && mPhysics->entityOfBody(loadedBody) == loadedPlayer
                        ? "world player-body-after-load ok"
                        : "world player-body-after-load wrong");
            }

            if (mPhysics == nullptr || !mGameLoaded || mProbed)
                return;

            const openeden::sdk::EntityId player = mEntities->playerEntity();
            if (!player.isValid())
                return;
            if (!mPlayerRecorded)
            {
                mPlayerRecorded = true;
                record("world player-entity " + textOf(player));
            }

            const openeden::sdk::BodyId body = mPhysics->bodyOfEntity(player);
            openeden::sdk::BodyState state;
            if (!mPhysics->readBody(body, state))
                return;

            // A teleport moves the player in steps while the cell loads, so wait until it rests at the point.
            const bool nearProbe = std::abs(state.positionM.x - mProbeXM) <= kProbeReachM
                && std::abs(state.positionM.y - mProbeYM) <= kProbeReachM;
            mSettledFrames = nearProbe && state.positionM == mLastPosition ? mSettledFrames + 1 : 0;
            mLastPosition = state.positionM;
            if (mSettledFrames < kProbeSettledFrames)
                return;

            mProbed = true;
            probe(player, body, state);
        }

    private:
        void noteThreadOnly(std::string_view what)
        {
            if (std::this_thread::get_id() != mMainThread)
                record("world wrong-thread " + std::string(what));
        }

        void note(std::string_view what)
        {
            noteThreadOnly(what);
            ++mEventCount;
        }

        void probe(openeden::sdk::EntityId player, openeden::sdk::BodyId body, const openeden::sdk::BodyState& state)
        {
            record("world player-position " + textOf(state.positionM));

            const openeden::sdk::BodyId bodyBack = mPhysics->bodyOfEntity(mPhysics->entityOfBody(body));
            record(mPhysics->entityOfBody(body) == player && bodyBack == body ? "world player-body-roundtrip ok"
                                                                               : "world player-body-roundtrip wrong");

            const openeden::sdk::PhysicsVec3 down{ 0.0f, 0.0f, -kProbeLengthM };
            const openeden::sdk::PhysicsVec3 origin{ mProbeXM, mProbeYM, kProbeTopM };
            recordHit("ray", mPhysics->castRay(origin, down, {}, mGroundMask));
            recordHit("sphere",
                mPhysics->castShape(openeden::sdk::SphereShape{ kProbeSphereRadiusM }, openeden::sdk::PhysicsQuat{},
                    origin, down, {}, mGroundMask));

            // The player is the one actor over its own head, so this ray finds a body that certainly has an entity.
            const openeden::sdk::PhysicsVec3 overPlayer{ state.positionM.x, state.positionM.y, kProbeTopM };
            const std::optional<openeden::sdk::RayHit> actorHit = mPhysics->castRay(overPlayer, down, {}, mActorMask);
            if (!actorHit.has_value())
            {
                record("world actor-ray miss");
                return;
            }
            const openeden::sdk::EntityId hitEntity = mPhysics->entityOfBody(actorHit->body);
            record(std::string("world actor-ray entity=") + (hitEntity == player ? "player" : textOf(hitEntity)));
        }

        void recordHit(std::string_view name, const std::optional<openeden::sdk::RayHit>& hit)
        {
            if (!hit.has_value())
            {
                record("world " + std::string(name) + " miss");
                return;
            }
            record(
                "world " + std::string(name) + " fraction=" + textOf(hit->fraction) + " point=" + textOf(hit->pointM));
            // Separate lines: which body a run hits is compared apart from the numbers.
            record("world " + std::string(name) + "-entity " + textOf(mPhysics->entityOfBody(hit->body)));
        }

        // The array always ends in a NUL, says game_events.hpp; do not trust that here.
        static std::string nameIn(const char (&name)[openeden::sdk::kEventNameCapacity])
        {
            return std::string(name, std::find(std::begin(name), std::end(name), '\0'));
        }

        template <typename Event, typename Text>
        void watch(Text text)
        {
            mEvents->subscribe<Event>(kPluginId, [this, text](const Event& event) {
                note("event");
                record("world " + text(event));
            });
        }

        void subscribeAll()
        {
            using namespace openeden::sdk;
            watch<PhysicsHitEvent>([](const PhysicsHitEvent& event) {
                return "physics-hit " + textOf(event.body) + ' ' + textOf(event.other) + ' ' + textOf(event.pointM);
            });
            watch<PhysicsContactBeginEvent>([](const PhysicsContactBeginEvent& event) {
                return "contact-begin " + textOf(event.bodyA) + ' ' + textOf(event.bodyB) + ' ' + textOf(event.pointM);
            });
            watch<PhysicsContactEndEvent>([](const PhysicsContactEndEvent& event) {
                return "contact-end " + textOf(event.bodyA) + ' ' + textOf(event.bodyB);
            });
            watch<GameLoadedEvent>([this](const GameLoadedEvent&) {
                mGameLoaded = true;
                mCheckPlayerBody = true;
                return std::string("game-loaded");
            });
            watch<GameSavedEvent>([](const GameSavedEvent&) { return std::string("game-saved"); });
            watch<CellChangedEvent>(
                [](const CellChangedEvent& event) { return "cell-changed " + nameIn(event.cellName); });
            watch<CellLoadedEvent>([](const CellLoadedEvent& event) { return "cell-loaded " + nameIn(event.cellName); });
            watch<CellUnloadedEvent>(
                [](const CellUnloadedEvent& event) { return "cell-unloaded " + nameIn(event.cellName); });
            watch<ActorHitEvent>([](const ActorHitEvent& event) {
                return "actor-hit " + textOf(event.attacker) + ' ' + textOf(event.target) + ' ' + textOf(event.damage)
                    + ' ' + (event.successful ? "successful" : "failed");
            });
            watch<EntityLifecycleNotice>([](const EntityLifecycleNotice& event) {
                return std::string("notice ") + nameOf(event.kind) + ' ' + textOf(event.entity);
            });
        }

        void unsubscribeAll()
        {
            using namespace openeden::sdk;
            for (const EventId id : { PhysicsHitEvent::kEventId, PhysicsContactBeginEvent::kEventId,
                     PhysicsContactEndEvent::kEventId, GameLoadedEvent::kEventId, GameSavedEvent::kEventId,
                     CellChangedEvent::kEventId, CellLoadedEvent::kEventId, CellUnloadedEvent::kEventId,
                     ActorHitEvent::kEventId, EntityLifecycleNotice::kEventId })
                mEvents->unsubscribe(id, kPluginId);
        }

        std::thread::id mMainThread;
        openeden::sdk::EventBus* mEvents;
        openeden::sdk::EntityManager* mEntities;
        openeden::sdk::PhysicsQueryService* mPhysics;
        openeden::sdk::QueryMask mGroundMask = openeden::sdk::kQueryMaskNone;
        openeden::sdk::QueryMask mActorMask = openeden::sdk::kQueryMaskNone;
        float mProbeXM = 0.0f;
        float mProbeYM = 0.0f;
        openeden::sdk::PhysicsVec3 mLastPosition{};
        int mSettledFrames = 0;
        int mEventCount = 0;
        int mSpawned = 0;
        int mDespawned = 0;
        bool mGameLoaded = false;
        bool mPlayerRecorded = false;
        bool mProbed = false;
        bool mCheckPlayerBody = false;
    };

    class TestPlugin final : public openeden::sdk::Plugin
    {
    public:
        ~TestPlugin() override { record("destroy"); }

        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            record("register");
#ifdef OPENEDEN_TEST_PLUGIN_PROVIDES_A
            registrar.provide<TestAContract>(mImplementation);
#endif
            for (const PhaseName& phase : kPhases)
            {
                const std::string event = std::string("phase ") + phase.mName;
                const bool isPostSimulation = phase.mPhase == openeden::sdk::Phase::PostSimulation;
                registrar.registerPhaseCallback(phase.mPhase, [this, event, isPostSimulation](float /*dt*/) {
                    record(event);
                    throwIfAsked("phase");
                    publishTestEvents();
                    if (isPostSimulation && mWorld != nullptr)
                        mWorld->onFrame();
                });
            }
        }

        bool resolve(const openeden::sdk::ServiceLocator& locator) override
        {
            record("resolve");
#ifdef OPENEDEN_TEST_PLUGIN_REQUIRES_A
            const TestAContract* const contract = locator.find<TestAContract>();
            if (contract == nullptr || contract->answer() != kContractAnswer)
                return false;
#elif defined(OPENEDEN_TEST_PLUGIN_USES_KF)
            auto* const converter = locator.find<openeden::contracts::KfConverterContract>();
            if (converter == nullptr)
            {
                record("kf failed contract not found");
                return false;
            }
            record("kf contract found");
            // A throw from here would be the test's failure too: nothing is recorded as converted.
            callConverter(*converter);
#else
            static_cast<void>(locator);
#endif
            return true;
        }

        void start(openeden::sdk::Host& host) override
        {
            record("start");
            throwIfAsked("start");

            mLogging = host.getLoggingService();
            if (mLogging == nullptr)
                return;
            // The sink goes first so that it sees this plugin's own line too.
            mSink = std::make_shared<RecordingSink>();
            mLogging->addSink(mSink);
            if (openeden::sdk::Logger* const logger = mLogging->getLogger(kPluginId))
            {
                sSinkLogger = logger;
                logger->info("hello from plugin");
            }
            throwIfAsked("start-after-sink");

            if (std::getenv("OPENEDEN_TEST_PLUGIN_DATA") != nullptr)
                startDataServices(host);
            if (const char* const probe = std::getenv("OPENEDEN_TEST_PLUGIN_WORLD"))
                mWorld = std::make_unique<WorldChecker>(host, probe);
        }

        void stop() override
        {
            sSinkLogger = nullptr;
            record("stop");
            mWorld.reset();
            if (mSaves != nullptr)
                mSaves->unregisterHandler(kPluginId);
            if (mLogging != nullptr && mSink != nullptr)
                mLogging->removeSink(mSink);
            mSink.reset();
        }

    private:
        void startDataServices(openeden::sdk::Host& host)
        {
            const auto textOf = [&](std::string_view path) {
                const std::vector<std::uint8_t> bytes = host.getVfsService()->readFile(path);
                return std::string(bytes.begin(), bytes.end());
            };
            record("vfs " + textOf("openeden_test/hello.txt"));
            record("vfs-override " + textOf("openeden_test/overridden.txt"));

            openeden::sdk::SettingsService& settings = *host.getSettingsService();
            record("setting answer " + std::to_string(settings.getInt(kPluginId, "answer", -1)));
            record("setting broken " + std::to_string(settings.getInt(kPluginId, "broken", -1)));
            settings.setInt(kPluginId, "changed", 7);

            mSaveHandler = std::make_shared<TestSaveHandler>();
            mSaves = host.getSaveService();
            mSaves->registerHandler(kPluginId, mSaveHandler);

            mEvents = host.getEventBus();
            const openeden::sdk::ForwardedField fields[] = {
                openeden::sdk::forwardedField("count", &PingEvent::count, offsetof(PingEvent, count)),
                openeden::sdk::forwardedField("value", &PingEvent::value, offsetof(PingEvent, value)),
                openeden::sdk::forwardedField("flag", &PingEvent::flag, offsetof(PingEvent, flag)),
                openeden::sdk::forwardedField("text", &PingEvent::text, offsetof(PingEvent, text)),
            };
            mEvents->markForwardedToLua<PingEvent>(kPluginId, "ping", fields);
        }

        void publishTestEvents()
        {
            if (mEvents == nullptr || mPublished)
                return;
            mPublished = true;

            PingEvent event;
            event.count = 1;
            std::strcpy(event.text, "first");
            mEvents->publishImmediate(event);

            // Not valid UTF-8: dropped, and the events around it still arrive in order.
            PingEvent bad;
            bad.text[0] = '\xFF';
            mEvents->publishImmediate(bad);

            mEvents->publishImmediate(UnmarkedEvent{ 1 });

            event.count = 2;
            std::strcpy(event.text, "second");
            mEvents->publishImmediate(event);
        }

#ifdef OPENEDEN_TEST_PLUGIN_PROVIDES_A
        TestAImplementation mImplementation;
#endif
        openeden::sdk::LoggingService* mLogging = nullptr;
        std::shared_ptr<RecordingSink> mSink;
        std::shared_ptr<TestSaveHandler> mSaveHandler;
        openeden::sdk::SaveService* mSaves = nullptr;
        openeden::sdk::EventBus* mEvents = nullptr;
        bool mPublished = false;
        std::unique_ptr<WorldChecker> mWorld;
    };

    std::unique_ptr<openeden::sdk::Plugin> createPlugin()
    {
        return std::make_unique<TestPlugin>();
    }

    const openeden::sdk::PluginDescriptor sDescriptor{ openeden::sdk::kSdkVersion, kPluginId, &createPlugin };
}

extern "C" OPENEDEN_PLUGIN_EXPORT const openeden::sdk::PluginDescriptor* openeden_plugin_entry(
    const openeden::sdk::HostInfo* /*host*/)
{
    return &sDescriptor;
}
