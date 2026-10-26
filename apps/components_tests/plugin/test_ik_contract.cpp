#include <components/detournavigator/collisionshapetype.hpp>
#include <components/plugin/host_impl.hpp>
#include <components/plugin/lifecycle_manager.hpp>
#include <components/plugin/manifest_io.hpp>
#include <components/plugin/plugin_loader.hpp>
#include <openeden/contracts/animation_contract.hpp>
#include <openeden/physics.hpp>
#include <openeden/version.hpp>
#include <plugins/creativemotion/creativemotion_plugin.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk;
    using namespace openeden::plugin;
    using namespace openeden::contracts;
    using namespace openeden::creativemotion;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;

    float length(const PhysicsVec3& v) noexcept
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    float distance(const PhysicsVec3& a, const PhysicsVec3& b) noexcept
    {
        return length({ a.x - b.x, a.y - b.y, a.z - b.z });
    }

    float dot(const PhysicsVec3& a, const PhysicsVec3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    PhysicsVec3 rotateVecByQuat(const PhysicsQuat& q, const PhysicsVec3& v) noexcept
    {
        const PhysicsVec3 qv{ q.x, q.y, q.z };
        const PhysicsVec3 uv{
            qv.y * v.z - qv.z * v.y,
            qv.z * v.x - qv.x * v.z,
            qv.x * v.y - qv.y * v.x
        };
        const PhysicsVec3 uuv{
            qv.y * uv.z - qv.z * uv.y,
            qv.z * uv.x - qv.x * uv.z,
            qv.x * uv.y - qv.y * uv.x
        };
        return {
            v.x + 2.0f * (q.w * uv.x + uuv.x),
            v.y + 2.0f * (q.w * uv.y + uuv.y),
            v.z + 2.0f * (q.w * uv.z + uuv.z)
        };
    }

    float shortestAngularDistance(float a, float b) noexcept
    {
        float diff = std::fmod(b - a + kPi, kTwoPi);
        if (diff < 0.0f)
            diff += kTwoPi;
        return std::abs(diff - kPi);
    }

    ::testing::AssertionResult Vec3Near(
        const char* m_expr, const char* n_expr, const char* tol_expr,
        const PhysicsVec3& a, const PhysicsVec3& b, float tol)
    {
        if (std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol && std::abs(a.z - b.z) <= tol)
            return ::testing::AssertionSuccess();
        return ::testing::AssertionFailure()
            << m_expr << " (" << a.x << ", " << a.y << ", " << a.z << ") and "
            << n_expr << " (" << b.x << ", " << b.y << ", " << b.z << ") differ by more than "
            << tol_expr << " (" << tol << ")";
    }

    std::filesystem::path findCreativeMotionManifestPath()
    {
#ifdef OPENEDEN_SOURCE_DIR
        const std::filesystem::path fromMacro
            = std::filesystem::path(OPENEDEN_SOURCE_DIR) / "plugins" / "creativemotion" / "plugin.json";
        if (std::filesystem::exists(fromMacro))
            return fromMacro;
#endif
        std::filesystem::path cur = std::filesystem::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            const std::filesystem::path candidate = cur / "plugins" / "creativemotion" / "plugin.json";
            if (std::filesystem::exists(candidate))
                return candidate;
            if (!cur.has_parent_path() || cur.parent_path() == cur)
                break;
            cur = cur.parent_path();
        }
        return std::filesystem::path("plugins/creativemotion/plugin.json");
    }

    PluginManifest loadCreativeMotionManifest()
    {
        LoadError err{};
        const auto path = findCreativeMotionManifestPath();
        auto manifestOpt = loadManifestFile(path, err);
        EXPECT_TRUE(manifestOpt.has_value()) << err.message;
        return manifestOpt.value_or(PluginManifest{});
    }

    std::filesystem::path findCreativeMotionSharedLibraryDir()
    {
        const std::string kLibName =
#if defined(_WIN32)
            "creativemotion.dll";
#elif defined(__APPLE__)
            "libcreativemotion.dylib";
#else
            "libcreativemotion.so";
#endif

        std::vector<std::filesystem::path> roots;
#ifdef OPENEDEN_SOURCE_DIR
        roots.push_back(std::filesystem::path(OPENEDEN_SOURCE_DIR));
        roots.push_back(std::filesystem::path(OPENEDEN_SOURCE_DIR) / "build");
        roots.push_back(std::filesystem::path(OPENEDEN_SOURCE_DIR) / "build-vs");
        roots.push_back(std::filesystem::path(OPENEDEN_SOURCE_DIR) / "build-jolt");
#endif
        std::filesystem::path cur = std::filesystem::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            roots.push_back(cur);
            roots.push_back(cur / "build-jolt");
            roots.push_back(cur / "build-vs");
            roots.push_back(cur / "build");
            if (!cur.has_parent_path() || cur.parent_path() == cur)
                break;
            cur = cur.parent_path();
        }

        for (const auto& root : roots)
        {
            for (const auto& rel : {
                     std::filesystem::path("plugins/creativemotion"),
                     std::filesystem::path("plugins/creativemotion/Release"),
                     std::filesystem::path("plugins/creativemotion/Debug"),
                     std::filesystem::path("plugins/creativemotion/RelWithDebInfo"),
                 })
            {
                const auto candidateDir = root / rel;
                if (std::filesystem::exists(candidateDir / kLibName))
                    return candidateDir;
            }
        }
        return {};
    }

    class AnimationConsumerPlugin final : public Plugin
    {
    public:
        void registerServices(Registrar& /*registrar*/) override {}

        bool resolve(const ServiceLocator& locator) override
        {
            mAnimation = locator.find<AnimationContract>();
            return mAnimation != nullptr;
        }

        void start(Host& /*host*/) override { mStarted = true; }

        void stop() override
        {
            mStopped = true;
            mAnimation = nullptr;
        }

        [[nodiscard]] AnimationContract* animation() const noexcept { return mAnimation; }
        [[nodiscard]] bool started() const noexcept { return mStarted; }
        [[nodiscard]] bool stopped() const noexcept { return mStopped; }

    private:
        AnimationContract* mAnimation = nullptr;
        bool mStarted = false;
        bool mStopped = false;
    };

    class IkContractTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            mHost = std::make_unique<HostImpl>();
            mLifecycle = std::make_unique<LifecycleManager>(*mHost);

            auto cmPlugin = std::make_unique<CreativeMotionPlugin>();
            mCmPlugin = cmPlugin.get();
            mLifecycle->addPlugin(loadCreativeMotionManifest(), std::move(cmPlugin));

            PluginManifest consumerManifest{};
            consumerManifest.id = "ik_test_consumer";
            consumerManifest.name = "IK Test Consumer";
            consumerManifest.pluginVersion = "1.0.0";
            consumerManifest.requiredContracts.push_back({ "openeden.animation", Version{ 1, 2 } });

            auto consumer = std::make_unique<AnimationConsumerPlugin>();
            mConsumer = consumer.get();
            mLifecycle->addPlugin(consumerManifest, std::move(consumer));

            ASSERT_TRUE(mLifecycle->initialize());
            mContract = mConsumer->animation();
            ASSERT_NE(mContract, nullptr);
        }

        void TearDown() override
        {
            if (mLifecycle)
                mLifecycle->shutdown();
        }

        std::unique_ptr<HostImpl> mHost;
        std::unique_ptr<LifecycleManager> mLifecycle;
        CreativeMotionPlugin* mCmPlugin = nullptr;
        AnimationConsumerPlugin* mConsumer = nullptr;
        AnimationContract* mContract = nullptr;
    };

    // =========================================================================
    // 1. Contract Metadata & Versioning
    // =========================================================================

    TEST_F(IkContractTest, AnimationContractVersionIs12)
    {
        EXPECT_EQ(AnimationContract::kId, "openeden.animation");
        EXPECT_EQ(AnimationContract::kVersion, (Version{ 1, 2 }));

        const PluginManifest manifest = loadCreativeMotionManifest();
        ASSERT_GE(manifest.providedContracts.size(), 1u);
        bool foundAnimationV12 = false;
        for (const auto& provided : manifest.providedContracts)
        {
            if (provided.contract == "openeden.animation" && provided.version == (Version{ 1, 2 }))
            {
                foundAnimationV12 = true;
                break;
            }
        }
        EXPECT_TRUE(foundAnimationV12) << "CreativeMotion manifest must provide openeden.animation v1.2";
    }

    // =========================================================================
    // 2. solveTwoBone: Known Reach Scenarios
    // =========================================================================

    TEST_F(IkContractTest, TwoBoneIkSolvesStandardChains)
    {
        // 2a. Standard 3-4-5 right triangle with unequal bones
        // Upper bone = 3 m, Lower bone = 4 m, target distance = 5 m.
        // Hinge bend should be exactly 90 degrees (pi / 2 rad).
        // Mid joint at x = (3^2 + 5^2 - 4^2) / (2 * 5) = 1.8 m, y = sqrt(3^2 - 1.8^2) = 2.4 m on +Y pole side.
        {
            const TwoBoneChain chain{
                { 0.0f, 0.0f, 0.0f },
                { 3.0f, 0.0f, 0.0f },
                { 7.0f, 0.0f, 0.0f }
            };
            const PhysicsVec3 target{ 5.0f, 0.0f, 0.0f };
            const PhysicsVec3 pole{ 0.0f, 1.0f, 0.0f };
            const HingeLimits limits{ 0.0f, kPi };

            const TwoBoneResult res = mContract->solveTwoBone(chain, target, pole, limits);
            ASSERT_TRUE(res.valid);
            EXPECT_TRUE(res.reached);
            EXPECT_PRED_FORMAT3(Vec3Near, res.end, target, 1e-4f);
            EXPECT_PRED_FORMAT3(Vec3Near, res.mid, (PhysicsVec3{ 1.8f, 2.4f, 0.0f }), 1e-4f);
            EXPECT_NEAR(res.bendRad, kPi / 2.0f, 1e-4f);

            // Bone lengths strictly preserved
            EXPECT_NEAR(distance(chain.root, res.mid), 3.0f, 1e-4f);
            EXPECT_NEAR(distance(res.mid, res.end), 4.0f, 1e-4f);
        }

        // 2b. Straight limb (target at full extension)
        // Unit bones (1 m, 1 m), target at distance 2 m.
        {
            const TwoBoneChain chain{
                { 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                { 1.0f, 1.0f, 0.0f }
            };
            const PhysicsVec3 target{ 2.0f, 0.0f, 0.0f };
            const PhysicsVec3 pole{ 0.0f, 0.0f, 1.0f };

            const TwoBoneResult res = mContract->solveTwoBone(chain, target, pole, {});
            ASSERT_TRUE(res.valid);
            EXPECT_TRUE(res.reached);
            EXPECT_PRED_FORMAT3(Vec3Near, res.end, target, 1e-4f);
            EXPECT_PRED_FORMAT3(Vec3Near, res.mid, (PhysicsVec3{ 1.0f, 0.0f, 0.0f }), 1e-4f);
            EXPECT_NEAR(res.bendRad, 0.0f, 1e-4f);
            EXPECT_NEAR(distance(chain.root, res.mid), 1.0f, 1e-4f);
            EXPECT_NEAR(distance(res.mid, res.end), 1.0f, 1e-4f);
        }

        // 2c. Target beyond reach clamps along ray to target
        // Unit bones (max reach = 2 m), target at (3, 0, 0).
        {
            const TwoBoneChain chain{
                { 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                { 1.0f, 1.0f, 0.0f }
            };
            const PhysicsVec3 target{ 3.0f, 0.0f, 0.0f };
            const PhysicsVec3 pole{ 0.0f, 0.0f, 1.0f };

            const TwoBoneResult res = mContract->solveTwoBone(chain, target, pole, {});
            ASSERT_TRUE(res.valid);
            EXPECT_FALSE(res.reached);
            EXPECT_PRED_FORMAT3(Vec3Near, res.end, (PhysicsVec3{ 2.0f, 0.0f, 0.0f }), 1e-4f);
            EXPECT_PRED_FORMAT3(Vec3Near, res.mid, (PhysicsVec3{ 1.0f, 0.0f, 0.0f }), 1e-4f);
            EXPECT_NEAR(res.bendRad, 0.0f, 1e-4f);
        }

        // 2d. Hinge fold limit stops end at minimum reachable distance
        // Bones 3 m and 4 m, target at 5 m, but hinge limited to max 45 degrees (pi / 4).
        // Min reachable distance: sqrt(3^2 + 4^2 + 2 * 3 * 4 * cos(pi / 4)) = sqrt(25 + 24 * 0.7071) ~= 6.4785 m > 5 m.
        {
            const TwoBoneChain chain{
                { 0.0f, 0.0f, 0.0f },
                { 3.0f, 0.0f, 0.0f },
                { 7.0f, 0.0f, 0.0f }
            };
            const HingeLimits limits{ 0.0f, kPi / 4.0f };
            const PhysicsVec3 target{ 0.0f, 5.0f, 0.0f };
            const PhysicsVec3 pole{ 1.0f, 0.0f, 0.0f };

            const TwoBoneResult res = mContract->solveTwoBone(chain, target, pole, limits);
            ASSERT_TRUE(res.valid);
            EXPECT_FALSE(res.reached);

            const float minReachable = std::sqrt(25.0f + 24.0f * std::cos(kPi / 4.0f));
            EXPECT_PRED_FORMAT3(Vec3Near, res.end, (PhysicsVec3{ 0.0f, minReachable, 0.0f }), 1e-3f);
            EXPECT_NEAR(res.bendRad, kPi / 4.0f, 1e-3f);
            EXPECT_NEAR(distance(chain.root, res.mid), 3.0f, 1e-4f);
            EXPECT_NEAR(distance(res.mid, res.end), 4.0f, 1e-4f);
        }

        // 2e. Full folding at root with equal bones
        {
            const TwoBoneChain chain{
                { 1.0f, 2.0f, 3.0f },
                { 2.0f, 2.0f, 3.0f },
                { 2.0f, 3.0f, 3.0f }
            };
            const PhysicsVec3 target = chain.root;
            const PhysicsVec3 pole{ 0.0f, 0.0f, 1.0f };

            const TwoBoneResult res = mContract->solveTwoBone(chain, target, pole, {});
            ASSERT_TRUE(res.valid);
            EXPECT_TRUE(res.reached);
            EXPECT_PRED_FORMAT3(Vec3Near, res.end, chain.root, 1e-4f);
            EXPECT_NEAR(distance(chain.root, res.mid), 1.0f, 1e-4f);
            EXPECT_NEAR(distance(res.mid, res.end), 1.0f, 1e-4f);
        }
    }

    // =========================================================================
    // 3. solveTwoBone: Degenerate Inputs Handling Without UB
    // =========================================================================

    TEST_F(IkContractTest, TwoBoneIkHandlesDegenerateInputsWithoutUB)
    {
        const TwoBoneChain validChain{
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f },
            { 2.0f, 0.0f, 0.0f }
        };
        const PhysicsVec3 validTarget{ 1.0f, 1.0f, 0.0f };
        const PhysicsVec3 validPole{ 0.0f, 1.0f, 0.0f };

        // 3a. Zero-length upper bone returns valid == false with positions unchanged
        {
            const TwoBoneChain zeroUpper{
                { 1.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f },
                { 2.0f, 1.0f, 1.0f }
            };
            const TwoBoneResult res = mContract->solveTwoBone(zeroUpper, validTarget, validPole, {});
            EXPECT_FALSE(res.valid);
            EXPECT_EQ(res.mid, zeroUpper.mid);
            EXPECT_EQ(res.end, zeroUpper.end);
        }

        // 3b. Zero-length lower bone returns valid == false with positions unchanged
        {
            const TwoBoneChain zeroLower{
                { 0.0f, 0.0f, 0.0f },
                { 1.0f, 0.0f, 0.0f },
                { 1.0f, 0.0f, 0.0f }
            };
            const TwoBoneResult res = mContract->solveTwoBone(zeroLower, validTarget, validPole, {});
            EXPECT_FALSE(res.valid);
            EXPECT_EQ(res.mid, zeroLower.mid);
            EXPECT_EQ(res.end, zeroLower.end);
        }

        // 3c. Target on root with restricted hinge limits uses current end direction
        {
            const HingeLimits restricted{ 0.0f, kPi / 2.0f };
            const TwoBoneResult res = mContract->solveTwoBone(validChain, { 0.0f, 0.0f, 0.0f }, validPole, restricted);
            EXPECT_TRUE(res.valid);
            EXPECT_FALSE(res.reached);
            EXPECT_TRUE(std::isfinite(res.mid.x) && std::isfinite(res.mid.y) && std::isfinite(res.mid.z));
            EXPECT_TRUE(std::isfinite(res.end.x) && std::isfinite(res.end.y) && std::isfinite(res.end.z));
            EXPECT_NEAR(distance(validChain.root, res.end), std::sqrt(2.0f), 1e-3f);
        }

        // 3d. Parallel pole direction falls back deterministically without NaN/crash
        {
            const TwoBoneChain chain{
                { 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                { 1.0f, 1.0f, 0.0f }
            };
            const PhysicsVec3 target{ std::sqrt(2.0f), 0.0f, 0.0f };
            const PhysicsVec3 parallelPole{ 1.0f, 0.0f, 0.0f };

            const TwoBoneResult res = mContract->solveTwoBone(chain, target, parallelPole, {});
            EXPECT_TRUE(res.valid);
            EXPECT_TRUE(res.reached);
            EXPECT_TRUE(std::isfinite(res.mid.x) && std::isfinite(res.mid.y));
            EXPECT_PRED_FORMAT3(Vec3Near, res.end, target, 1e-3f);
            EXPECT_NEAR(res.bendRad, kPi / 2.0f, 1e-3f);
        }

        // 3e. Non-finite target coordinates (NaN / Inf) return valid == false
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float inf = std::numeric_limits<float>::infinity();

            EXPECT_FALSE(mContract->solveTwoBone(validChain, { nan, 0.0f, 0.0f }, validPole, {}).valid);
            EXPECT_FALSE(mContract->solveTwoBone(validChain, { 0.0f, nan, 0.0f }, validPole, {}).valid);
            EXPECT_FALSE(mContract->solveTwoBone(validChain, { 0.0f, 0.0f, nan }, validPole, {}).valid);
            EXPECT_FALSE(mContract->solveTwoBone(validChain, { inf, 0.0f, 0.0f }, validPole, {}).valid);
            EXPECT_FALSE(mContract->solveTwoBone(validChain, { 0.0f, -inf, 0.0f }, validPole, {}).valid);
        }

        // 3f. Non-finite pole coordinates return valid == false
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float inf = std::numeric_limits<float>::infinity();

            EXPECT_FALSE(mContract->solveTwoBone(validChain, validTarget, { nan, 1.0f, 0.0f }, {}).valid);
            EXPECT_FALSE(mContract->solveTwoBone(validChain, validTarget, { 0.0f, inf, 0.0f }, {}).valid);
        }

        // 3g. Non-finite chain points return valid == false
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const TwoBoneChain badRoot{ { nan, 0.0f, 0.0f }, validChain.mid, validChain.end };
            EXPECT_FALSE(mContract->solveTwoBone(badRoot, validTarget, validPole, {}).valid);

            const TwoBoneChain badMid{ validChain.root, { 0.0f, nan, 0.0f }, validChain.end };
            EXPECT_FALSE(mContract->solveTwoBone(badMid, validTarget, validPole, {}).valid);
        }

        // 3h. Invalid hinge limits return valid == false
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            EXPECT_FALSE(mContract->solveTwoBone(validChain, validTarget, validPole, { 2.0f, 1.0f }).valid); // min > max
            EXPECT_FALSE(mContract->solveTwoBone(validChain, validTarget, validPole, { -0.1f, 2.0f }).valid); // min < 0
            EXPECT_FALSE(mContract->solveTwoBone(validChain, validTarget, validPole, { 0.5f, 3.5f }).valid); // max > pi
            EXPECT_FALSE(mContract->solveTwoBone(validChain, validTarget, validPole, { nan, 2.0f }).valid); // NaN limit
        }
    }

    // =========================================================================
    // 4. solveFootPlacement: Uneven Terrain, Slopes, Steps & Pelvis Offset
    // =========================================================================

    TEST_F(IkContractTest, FootPlacementSolvesUnevenTerrain)
    {
        // Standard humanoid leg chain:
        // Hip (root) at z = 0.95 m
        // Knee (mid) at (0.0, 0.05, 0.52) m (upper bone length ~ 0.433 m)
        // Ankle (end) at (0.0, 0.0, 0.15) m (lower bone length ~ 0.373 m, total ~ 0.806 m)
        // Nominal ankle height from ground: 0.15 m
        const TwoBoneChain legChain{
            { 0.0f, 0.0f, 0.95f },
            { 0.0f, 0.05f, 0.52f },
            { 0.0f, 0.0f, 0.15f }
        };
        const float nominalHeight = 0.15f;

        // 4a. Flat level ground at nominal height (distance = 0.95 m down to z = 0.0)
        FootPlacementInput inputLevel{};
        inputLevel.legChain = legChain;
        inputLevel.groundDistanceM = 0.95f;
        inputLevel.nominalHeightM = nominalHeight;
        inputLevel.groundNormal = { 0.0f, 0.0f, 1.0f };
        inputLevel.poleDirection = { 0.0f, 1.0f, 0.0f };
        inputLevel.kneeLimits = { 0.05f, 2.8f };

        const FootPlacementResult resLevel = mContract->solveFootPlacement(inputLevel);
        ASSERT_TRUE(resLevel.ik.valid);
        EXPECT_TRUE(resLevel.ik.reached);
        EXPECT_NEAR(resLevel.ik.end.z, 0.15f, 0.015f);
        EXPECT_NEAR(resLevel.pelvisOffsetZM, 0.0f, 1e-4f);

        // Foot alignment on flat ground is identity / normal-aligned
        const PhysicsVec3 alignedUpLevel = rotateVecByQuat(resLevel.footAlignment, { 0.0f, 0.0f, 1.0f });
        EXPECT_NEAR(dot(alignedUpLevel, inputLevel.groundNormal), 1.0f, 0.02f);

        // 4b. Positive slope (+15 degrees)
        // Rising terrain forward: ground normal tilted backward along Y
        const float rad15 = 15.0f * kPi / 180.0f;
        FootPlacementInput inputPosSlope = inputLevel;
        inputPosSlope.groundDistanceM = 0.90f; // ground raised by 0.05 m under ankle
        inputPosSlope.groundNormal = { 0.0f, -std::sin(rad15), std::cos(rad15) };

        const FootPlacementResult resPosSlope = mContract->solveFootPlacement(inputPosSlope);
        ASSERT_TRUE(resPosSlope.ik.valid);
        EXPECT_TRUE(resPosSlope.ik.reached);
        EXPECT_NEAR(resPosSlope.ik.end.z, 0.05f + nominalHeight, 0.015f);

        // Foot alignment rotates to match the +15 deg ground normal slope
        const PhysicsVec3 alignedUpPos = rotateVecByQuat(resPosSlope.footAlignment, { 0.0f, 0.0f, 1.0f });
        EXPECT_NEAR(dot(alignedUpPos, inputPosSlope.groundNormal), 1.0f, 0.02f);

        // 4c. Negative slope (-15 degrees)
        // Descending terrain forward: ground normal tilted forward along Y
        FootPlacementInput inputNegSlope = inputLevel;
        inputNegSlope.groundDistanceM = 1.00f; // ground dropped by 0.05 m under ankle
        inputNegSlope.groundNormal = { 0.0f, std::sin(rad15), std::cos(rad15) };

        const FootPlacementResult resNegSlope = mContract->solveFootPlacement(inputNegSlope);
        ASSERT_TRUE(resNegSlope.ik.valid);
        EXPECT_TRUE(resNegSlope.ik.reached);
        EXPECT_NEAR(resNegSlope.ik.end.z, -0.05f + nominalHeight, 0.015f);

        // Foot alignment rotates to match the -15 deg ground normal slope
        const PhysicsVec3 alignedUpNeg = rotateVecByQuat(resNegSlope.footAlignment, { 0.0f, 0.0f, 1.0f });
        EXPECT_NEAR(dot(alignedUpNeg, inputNegSlope.groundNormal), 1.0f, 0.02f);

        // 4d. Step / Ledge (+0.20 m step up)
        // Ground contact is at z = 0.20 m, distance = 0.75 m
        FootPlacementInput inputStep = inputLevel;
        inputStep.groundDistanceM = 0.75f;
        inputStep.groundNormal = { 0.0f, 0.0f, 1.0f };

        const FootPlacementResult resStep = mContract->solveFootPlacement(inputStep);
        ASSERT_TRUE(resStep.ik.valid);
        EXPECT_TRUE(resStep.ik.reached);
        EXPECT_NEAR(resStep.ik.end.z, 0.20f + nominalHeight, 0.015f);

        // Knee bend must adapt and bend significantly more than on flat level ground
        EXPECT_GT(resStep.ik.bendRad, resLevel.ik.bendRad);

        // 4e. Over-extended leg requires pelvis offset compensation (pelvis dip)
        // Ground is 1.40 m below hip; maximum leg extension is ~0.806 m
        FootPlacementInput inputOver = inputLevel;
        inputOver.groundDistanceM = 1.40f;

        const FootPlacementResult resOver = mContract->solveFootPlacement(inputOver);
        ASSERT_TRUE(resOver.ik.valid);
        // Over-extension must report negative pelvis offset Z (downward compensation)
        EXPECT_LT(resOver.pelvisOffsetZM, 0.0f);
    }

    // =========================================================================
    // 5. solveLookAt: Angular Clamping & Temporal Smoothing
    // =========================================================================

    TEST_F(IkContractTest, LookAtClampsAndSmooths)
    {
        const PhysicsVec3 eyePos{ 0.0f, 0.0f, 1.7f };
        const PhysicsVec3 headFwd{ 0.0f, 1.0f, 0.0f }; // +Y is forward
        const float maxYaw = 1.2217f;   // 70 degrees
        const float maxPitch = 0.7854f; // 45 degrees
        const float speed = 8.0f;       // rad/s

        // 5a. Target within angular limits tracks gaze and reports inRange == true
        {
            // Target at +20 deg yaw, +10 deg pitch, 2 m distance
            const float targetYaw = 20.0f * kPi / 180.0f;
            const float targetPitch = 10.0f * kPi / 180.0f;
            const PhysicsVec3 target{
                std::sin(targetYaw) * 2.0f,
                std::cos(targetYaw) * 2.0f,
                1.7f + std::sin(targetPitch) * 2.0f
            };

            LookAtInput input{};
            input.eyePosition = eyePos;
            input.headForward = headFwd;
            input.targetPosition = target;
            input.currentYawRad = 0.0f;
            input.currentPitchRad = 0.0f;
            input.maxYawRad = maxYaw;
            input.maxPitchRad = maxPitch;
            input.speedRadPerS = speed;
            input.dt = 1.0f; // Long dt for full convergence

            const LookAtResult res = mContract->solveLookAt(input);
            EXPECT_TRUE(res.inRange);
            EXPECT_NEAR(res.yawRad, targetYaw, 0.02f);
            EXPECT_NEAR(res.pitchRad, targetPitch, 0.02f);
        }

        // 5b. Target beyond yaw limit clamps to maxYaw and reports inRange == false
        {
            // Target at +85 deg yaw (1.4835 rad > 1.2217 rad limit)
            const float targetYaw = 85.0f * kPi / 180.0f;
            const PhysicsVec3 target{
                std::sin(targetYaw) * 2.0f,
                std::cos(targetYaw) * 2.0f,
                1.7f
            };

            LookAtInput input{};
            input.eyePosition = eyePos;
            input.headForward = headFwd;
            input.targetPosition = target;
            input.currentYawRad = 0.0f;
            input.currentPitchRad = 0.0f;
            input.maxYawRad = maxYaw;
            input.maxPitchRad = maxPitch;
            input.speedRadPerS = speed;
            input.dt = 1.0f;

            const LookAtResult res = mContract->solveLookAt(input);
            EXPECT_FALSE(res.inRange);
            EXPECT_NEAR(res.yawRad, maxYaw, 1e-3f);
        }

        // 5c. Target beyond pitch limit clamps to maxPitch and reports inRange == false
        {
            // Target at +60 deg pitch (1.0472 rad > 0.7854 rad limit)
            const float targetPitch = 60.0f * kPi / 180.0f;
            const PhysicsVec3 target{
                0.0f,
                std::cos(targetPitch) * 2.0f,
                1.7f + std::sin(targetPitch) * 2.0f
            };

            LookAtInput input{};
            input.eyePosition = eyePos;
            input.headForward = headFwd;
            input.targetPosition = target;
            input.currentYawRad = 0.0f;
            input.currentPitchRad = 0.0f;
            input.maxYawRad = maxYaw;
            input.maxPitchRad = maxPitch;
            input.speedRadPerS = speed;
            input.dt = 1.0f;

            const LookAtResult res = mContract->solveLookAt(input);
            EXPECT_FALSE(res.inRange);
            EXPECT_NEAR(res.pitchRad, maxPitch, 1e-3f);
        }

        // 5d. Smooth exponential decay over incremental dt steps without jumping or overshooting
        {
            const float targetYaw = 0.60f; // ~34.4 degrees
            const PhysicsVec3 target{
                std::sin(targetYaw) * 3.0f,
                std::cos(targetYaw) * 3.0f,
                1.7f
            };

            LookAtInput input{};
            input.eyePosition = eyePos;
            input.headForward = headFwd;
            input.targetPosition = target;
            input.maxYawRad = maxYaw;
            input.maxPitchRad = maxPitch;
            input.speedRadPerS = speed;
            input.dt = 0.016f; // 60 FPS frame step

            float current = 0.0f;
            for (int step = 0; step < 8; ++step)
            {
                input.currentYawRad = current;
                const LookAtResult stepRes = mContract->solveLookAt(input);
                EXPECT_GT(stepRes.yawRad, current) << "Must progress toward target angle";
                EXPECT_LT(stepRes.yawRad, targetYaw) << "Must not overshoot target angle";
                current = stepRes.yawRad;
            }
        }
    }

    // =========================================================================
    // 6. solveTurnSmoothing: Exponential Decay & ±pi Boundary Continuity
    // =========================================================================

    TEST_F(IkContractTest, TurnSmoothingContinuous)
    {
        // 6a. Half-life exponential decay: at dt == halfLife, delta advances by exactly 50%
        {
            TurnSmoothingInput input{};
            input.currentYawRad = 0.0f;
            input.targetYawRad = 1.0f;
            input.halfLifeS = 0.08f;
            input.dt = 0.08f;

            const float smoothed = mContract->solveTurnSmoothing(input);
            EXPECT_NEAR(smoothed, 0.5f, 1e-3f);
        }

        // 6b. Smooth convergence across the +pi <-> -pi boundary along the shortest arc
        // Current = +3.10 rad (~+177.6 deg), Target = -3.10 rad (~-177.6 deg).
        // Shortest arc is across +/-pi (+0.083185 rad), NOT around the long way (-6.20 rad).
        {
            TurnSmoothingInput input{};
            input.currentYawRad = 3.10f;
            input.targetYawRad = -3.10f;
            input.halfLifeS = 0.08f;
            input.dt = 0.08f; // One half-life

            const float smoothed = mContract->solveTurnSmoothing(input);
            const float distFromCurrent = shortestAngularDistance(input.currentYawRad, smoothed);
            const float totalDist = shortestAngularDistance(input.currentYawRad, input.targetYawRad);

            // Must advance approximately half of the shortest arc
            EXPECT_NEAR(distFromCurrent, totalDist * 0.5f, 0.005f);

            // Multistep convergence across boundary to target
            float cur = 3.10f;
            for (int i = 0; i < 25; ++i)
            {
                input.currentYawRad = cur;
                cur = mContract->solveTurnSmoothing(input);
            }
            EXPECT_LT(shortestAngularDistance(cur, input.targetYawRad), 0.002f);
        }

        // 6c. Symmetrical shortest-arc crossing from negative to positive
        {
            TurnSmoothingInput input{};
            input.currentYawRad = -3.10f;
            input.targetYawRad = 3.10f;
            input.halfLifeS = 0.08f;
            input.dt = 0.08f;

            const float smoothed = mContract->solveTurnSmoothing(input);
            const float distFromCurrent = shortestAngularDistance(input.currentYawRad, smoothed);
            const float totalDist = shortestAngularDistance(input.currentYawRad, input.targetYawRad);
            EXPECT_NEAR(distFromCurrent, totalDist * 0.5f, 0.005f);
        }

        // 6d. Target == Current returns current yaw without drift or overshoot
        {
            TurnSmoothingInput input{};
            input.currentYawRad = 1.45f;
            input.targetYawRad = 1.45f;
            input.halfLifeS = 0.08f;
            input.dt = 0.05f;

            EXPECT_NEAR(mContract->solveTurnSmoothing(input), 1.45f, 1e-6f);
        }

        // 6e. Zero dt leaves yaw unchanged
        {
            TurnSmoothingInput input{};
            input.currentYawRad = 0.75f;
            input.targetYawRad = 2.50f;
            input.halfLifeS = 0.08f;
            input.dt = 0.0f;

            EXPECT_NEAR(mContract->solveTurnSmoothing(input), 0.75f, 1e-6f);
        }
    }

    // =========================================================================
    // 7. Actor Collision Shape Type Capsule
    // =========================================================================

    TEST(ActorCollisionShapeTypeTest, ActorCollisionShapeTypeCapsule)
    {
        // 7a. Verify enum value is 3
        EXPECT_EQ(static_cast<int>(DetourNavigator::CollisionShapeType::Capsule), 3);

        // 7b. Verify toCollisionShapeType parses 0, 1, 2, 3
        EXPECT_EQ(DetourNavigator::toCollisionShapeType(0), DetourNavigator::CollisionShapeType::Aabb);
        EXPECT_EQ(DetourNavigator::toCollisionShapeType(1), DetourNavigator::CollisionShapeType::RotatingBox);
        EXPECT_EQ(DetourNavigator::toCollisionShapeType(2), DetourNavigator::CollisionShapeType::Cylinder);
        EXPECT_EQ(DetourNavigator::toCollisionShapeType(3), DetourNavigator::CollisionShapeType::Capsule);
        EXPECT_EQ(DetourNavigator::toCollisionShapeType(4), DetourNavigator::CollisionShapeType::Compound);

        // 7c. Verify invalid values throw std::invalid_argument
        EXPECT_THROW(DetourNavigator::toCollisionShapeType(5), std::invalid_argument);
        EXPECT_THROW(DetourNavigator::toCollisionShapeType(-1), std::invalid_argument);
        EXPECT_THROW(DetourNavigator::toCollisionShapeType(99), std::invalid_argument);
    }

    // =========================================================================
    // 8. Dynamic Library Loading: C ABI Boundary & AnimationContract v1.2
    // =========================================================================

    TEST(CreativeMotionDynamicLoadingTest, DynamicLibraryExposesAnimationContractV12)
    {
        const auto libDir = findCreativeMotionSharedLibraryDir();
        ASSERT_FALSE(libDir.empty()) << "Built creativemotion shared library not found in build output";

        HostImpl host;
        DiscoveredPlugin discovered{};
        discovered.directory = libDir;
        discovered.manifestPath = findCreativeMotionManifestPath();
        discovered.manifest = loadCreativeMotionManifest();
        discovered.manifest.toolchain = getHostToolchain();

        PluginLoadResult loadRes = loadPluginLibrary(discovered, host.getHostInfo());
        ASSERT_TRUE(loadRes.success) << "Stage: " << loadRes.error.stage << " - " << loadRes.error.message;
        ASSERT_NE(loadRes.plugin.instance, nullptr);
        ASSERT_NE(loadRes.plugin.library, nullptr);
        EXPECT_TRUE(loadRes.plugin.library->isLoaded());

        LifecycleManager lifecycle(host);
        lifecycle.addPlugin(
            std::move(loadRes.plugin.manifest), std::move(loadRes.plugin.instance), std::move(loadRes.plugin.library));

        PluginManifest consumerManifest{};
        consumerManifest.id = "dynamic_ik_consumer";
        consumerManifest.name = "Dynamic IK Consumer";
        consumerManifest.pluginVersion = "1.0.0";
        consumerManifest.requiredContracts.push_back({ "openeden.animation", Version{ 1, 2 } });

        auto consumerPlugin = std::make_unique<AnimationConsumerPlugin>();
        AnimationConsumerPlugin* consumerRaw = consumerPlugin.get();
        lifecycle.addPlugin(consumerManifest, std::move(consumerPlugin));

        ASSERT_TRUE(lifecycle.initialize());
        EXPECT_TRUE(lifecycle.isPluginActive("creativemotion"));
        EXPECT_TRUE(lifecycle.isPluginActive("dynamic_ik_consumer"));

        AnimationContract* anim = consumerRaw->animation();
        ASSERT_NE(anim, nullptr);
        EXPECT_EQ(AnimationContract::kVersion, (Version{ 1, 2 }));

        // Verify pure kinematic IK functions are callable across dynamic library C ABI
        // 8a. solveTwoBone
        const TwoBoneChain chain{
            { 0.0f, 0.0f, 0.0f },
            { 3.0f, 0.0f, 0.0f },
            { 7.0f, 0.0f, 0.0f }
        };
        const TwoBoneResult twoBoneRes = anim->solveTwoBone(chain, { 5.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, {});
        EXPECT_TRUE(twoBoneRes.valid);
        EXPECT_TRUE(twoBoneRes.reached);

        // 8b. solveFootPlacement
        FootPlacementInput footInput{};
        footInput.legChain = chain;
        footInput.groundDistanceM = 1.0f;
        footInput.nominalHeightM = 0.15f;
        footInput.groundNormal = { 0.0f, 0.0f, 1.0f };
        footInput.poleDirection = { 0.0f, 1.0f, 0.0f };
        const FootPlacementResult footRes = anim->solveFootPlacement(footInput);
        EXPECT_TRUE(footRes.ik.valid);

        // 8c. solveLookAt
        LookAtInput lookInput{};
        lookInput.eyePosition = { 0.0f, 0.0f, 1.7f };
        lookInput.headForward = { 0.0f, 1.0f, 0.0f };
        lookInput.targetPosition = { 0.5f, 2.0f, 1.7f };
        lookInput.speedRadPerS = 8.0f;
        lookInput.dt = 0.5f;
        const LookAtResult lookRes = anim->solveLookAt(lookInput);
        EXPECT_TRUE(lookRes.inRange);

        // 8d. solveTurnSmoothing
        TurnSmoothingInput turnInput{};
        turnInput.currentYawRad = 0.0f;
        turnInput.targetYawRad = 1.0f;
        turnInput.halfLifeS = 0.08f;
        turnInput.dt = 0.08f;
        const float smoothed = anim->solveTurnSmoothing(turnInput);
        EXPECT_NEAR(smoothed, 0.5f, 1e-3f);

        lifecycle.shutdown();
        EXPECT_FALSE(lifecycle.isPluginActive("creativemotion"));
    }

    // =========================================================================
    // 9. Off-by-Default Settings Schema
    // =========================================================================

    TEST(CreativeMotionSettingsTest, IkSettingsDefaultToOff)
    {
        const PluginManifest manifest = loadCreativeMotionManifest();

        const auto findSetting = [&](std::string_view key) -> const SettingDesc* {
            for (const auto& s : manifest.settings)
            {
                if (s.key == key)
                    return &s;
            }
            return nullptr;
        };

        const SettingDesc* enabled = findSetting("enabled");
        ASSERT_NE(enabled, nullptr);
        EXPECT_EQ(enabled->type, "bool");
        EXPECT_FALSE(enabled->defaultBool);

        const SettingDesc* footPlanting = findSetting("footPlanting");
        ASSERT_NE(footPlanting, nullptr);
        EXPECT_EQ(footPlanting->type, "bool");
        EXPECT_FALSE(footPlanting->defaultBool);

        const SettingDesc* lookAt = findSetting("lookAt");
        ASSERT_NE(lookAt, nullptr);
        EXPECT_EQ(lookAt->type, "bool");
        EXPECT_FALSE(lookAt->defaultBool);

        const SettingDesc* smootherTurns = findSetting("smootherTurns");
        ASSERT_NE(smootherTurns, nullptr);
        EXPECT_EQ(smootherTurns->type, "bool");
        EXPECT_FALSE(smootherTurns->defaultBool);
    }
} // namespace
