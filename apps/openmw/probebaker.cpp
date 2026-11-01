#include "probebaker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/fallback/fallback.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/mwanimation/color.hpp>
#include <components/render/hdrmath.hpp>
#include <components/render/probebake.hpp>
#include <components/render/probes.hpp>
#include <components/settings/values.hpp>

#include "mwbase/environment.hpp"
#include "mwbase/world.hpp"
#include "mwphysics/joltlayers.hpp"
#include "mwphysics/physicssystem.hpp"
#include "mwrender/rendermanager.hpp"
#include "mwworld/cellstore.hpp"
#include "mwworld/class.hpp"
#include "mwworld/ptr.hpp"

namespace OMW
{
    namespace
    {
        // Frames to wait after the player's cell is the right one, so that its physics bodies exist.
        constexpr unsigned int settleFrames = 90;
        // Frames to wait for the cell before giving up.
        constexpr unsigned int giveUpFrames = 3000;
        // Probes baked between two looks at the clock.
        constexpr std::size_t probesPerSlice = 4;
        constexpr std::chrono::seconds saveEvery{ 30 };
        // What a bake ray can hit: static geometry and doors, no actors, projectiles or water.
        constexpr int bakeMask = MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::DOOR;

        class JoltRayWorld final : public Render::Gi::RayWorld
        {
        public:
            explicit JoltRayWorld(const MWPhysics::PhysicsSystem& physics)
                : mPhysics(physics)
            {
            }

            bool cast(
                const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const override
            {
                const auto result = mPhysics.castRay(from, to, bakeMask);
                if (!result.mHit)
                    return false;
                hitPos = result.mHitPos;
                hitNormal = result.mHitNormal; // the baker turns it to face the ray (Render::Gi::facingNormal)
                return true;
            }

        private:
            const MWPhysics::PhysicsSystem& mPhysics;
        };

        Render::Gi::LightFalloff readFalloff()
        {
            Render::Gi::LightFalloff f;
            f.useConstant = Fallback::Map::getBool("LightAttenuation_UseConstant");
            f.constantValue = Fallback::Map::getFloat("LightAttenuation_ConstantValue");
            f.useLinear = Fallback::Map::getBool("LightAttenuation_UseLinear");
            f.linearMethod = Fallback::Map::getInt("LightAttenuation_LinearMethod");
            f.linearValue = Fallback::Map::getFloat("LightAttenuation_LinearValue");
            f.linearRadiusMult = Fallback::Map::getFloat("LightAttenuation_LinearRadiusMult");
            f.useQuadratic = Fallback::Map::getBool("LightAttenuation_UseQuadratic");
            f.quadraticMethod = Fallback::Map::getInt("LightAttenuation_QuadraticMethod");
            f.quadraticValue = Fallback::Map::getFloat("LightAttenuation_QuadraticValue");
            f.quadraticRadiusMult = Fallback::Map::getFloat("LightAttenuation_QuadraticRadiusMult");
            f.classic = Settings::shaders().mClassicFalloff;
            return f;
        }

        std::string readFile(const std::filesystem::path& path)
        {
            std::ifstream in(path, std::ios::binary);
            std::ostringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }

        // Writes to a temporary file and renames it, so a killed game never leaves half a file under the real name.
        void writeFile(const std::filesystem::path& path, const std::string& text)
        {
            std::filesystem::create_directories(path.parent_path());
            std::filesystem::path temporary = path;
            temporary += ".tmp";
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                out << text;
            }
            std::filesystem::rename(temporary, path);
        }
    }

    struct ProbeBaker::State
    {
        std::string cellName;
        std::filesystem::path outFolder;
        MWRender::RenderManager& rendering;
        unsigned int frames = 0;
        unsigned int settled = 0;
        bool finished = false;

        std::unique_ptr<JoltRayWorld> world;
        std::unique_ptr<Render::Gi::VolumeBaker> baker;
        Render::Gi::BakeSettings settings;
        Render::Gi::CellId cellId;
        std::filesystem::path outFile;
        std::filesystem::path partialFile;
        std::filesystem::path progressFile;
        std::size_t lightCount = 0;
        // Everything about the lights and the falloff that changes the bake, as text, for the signature.
        std::string lightText;
        MWWorld::CellStore* bakedCell = nullptr;
        std::chrono::steady_clock::time_point started;
        std::chrono::steady_clock::time_point lastSave;
        std::size_t lastLogged = 99;

        State(std::string name, std::filesystem::path out, MWRender::RenderManager& r)
            : cellName(std::move(name))
            , outFolder(std::move(out))
            , rendering(r)
        {
        }

        // The text that identifies a bake, to tell whether a partial file belongs to this one.
        std::string signature() const
        {
            const auto& v = baker->volume();
            std::ostringstream s;
            s << "rays " << settings.rays << " bounces " << settings.bounces << " seed " << settings.seed << " lights "
              << lightCount << " size " << v.size[0] << " " << v.size[1] << " " << v.size[2] << " spacing " << v.spacing
              << " origin " << v.origin.x << " " << v.origin.y << " " << v.origin.z << " ambient " << settings.ambient.x
              << " " << settings.ambient.y << " " << settings.ambient.z << " falloff " << settings.falloff.useConstant
              << settings.falloff.constantValue << settings.falloff.useLinear << settings.falloff.linearMethod
              << settings.falloff.linearValue << settings.falloff.linearRadiusMult << settings.falloff.useQuadratic
              << settings.falloff.quadraticMethod << settings.falloff.quadraticValue
              << settings.falloff.quadraticRadiusMult << settings.falloff.classic << " lightdata " << lightText;
            return s.str();
        }

        Render::Gi::ProbeSet makeSet(const Render::Gi::Volume& volume) const
        {
            Render::Gi::ProbeSet set;
            set.cell = cellId;
            set.rays = settings.rays;
            set.bounces = settings.bounces;
            set.seed = settings.seed;
            set.ambient = settings.ambient;
            set.volumes.push_back(volume);
            return set;
        }

        void savePartial() const
        {
            const auto set = makeSet(baker->volume());
            if (!Render::Gi::validate(set).empty())
                return;
            writeFile(partialFile, Render::Gi::writeCanonical(set));
            std::ostringstream p;
            p << signature() << "\nnext " << baker->next() << "\ninvalid";
            for (std::size_t i = 0; i < baker->invalid().size(); ++i)
                if (baker->invalid()[i])
                    p << ' ' << i;
            p << '\n';
            writeFile(progressFile, p.str());
        }

        void tryResume()
        {
            if (!std::filesystem::exists(progressFile) || !std::filesystem::exists(partialFile))
                return;
            std::istringstream progress(readFile(progressFile));
            std::string line;
            std::getline(progress, line);
            if (line != signature())
            {
                Log(Debug::Warning) << "[GI] ignoring " << progressFile.string() << ": it is from other settings";
                return;
            }
            std::string word;
            std::size_t next = 0;
            progress >> word >> next;
            progress >> word;
            std::vector<bool> invalid(baker->total(), false);
            std::size_t index;
            while (progress >> index)
                if (index < invalid.size())
                    invalid[index] = true;
            progress.clear();
            progress >> word;
            if (word != "end")
            {
                Log(Debug::Warning) << "[GI] cannot resume: " << progressFile.string() << " is incomplete";
                return;
            }
            Render::Gi::ProbeError error;
            const auto set = Render::Gi::parseProbes(readFile(partialFile), partialFile.string(), error);
            if (!set || set->volumes.empty())
            {
                Log(Debug::Warning) << "[GI] cannot resume: " << error.describe();
                return;
            }
            baker->resume(set->volumes.front(), next, invalid);
            Log(Debug::Info) << "[GI] resumed at probe " << baker->next() << " of " << baker->total();
        }

        bool begin()
        {
            auto gameWorld = MWBase::Environment::get().getWorld();
            MWWorld::CellStore* cell = gameWorld->getPlayerPtr().getCell();
            if (!cell || cell->getCell()->isExterior())
            {
                Log(Debug::Error) << "[GI] \"" << cellName << "\" is not an interior cell; nothing to bake";
                return false;
            }
            // The name the game gave the cell, which is the one the runtime asks for.
            bakedCell = cell;
            cellId = Render::Gi::CellId{ true, std::string(cell->getCell()->getNameId()), 0, 0 };
            auto* physics = gameWorld->getPhysicsSystem();

            std::vector<std::pair<std::pair<int, int>, Render::Gi::BakeLight>> found;
            cell->forEachType<ESM::Light>([&](MWWorld::Ptr ptr) {
                const ESM::Light* light = ptr.get<ESM::Light>()->mBase;
                if (light->mData.mFlags & ESM::Light::OffDefault)
                    return true;
                Render::Gi::BakeLight bl;
                // The reference's position: the light node's offset inside its model is not known to the baker.
                const auto& pos = ptr.getRefData().getPosition();
                bl.position = vsg::vec3(pos.pos[0], pos.pos[1], pos.pos[2]);
                bl.color = Render::Hdr::toLinear(MWAnim::rgbColor(light->mData.mColor));
                if (light->mData.mFlags & ESM::Light::Negative)
                    bl.color = bl.color * -1.f;
                bl.radius = static_cast<float>(light->mData.mRadius);
                const ESM::RefNum num = ptr.getCellRef().getRefNum();
                found.push_back({ { static_cast<int>(num.mContentFile), static_cast<int>(num.mIndex) }, bl });
                return true;
            });
            // The same order every run: by reference number.
            std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<Render::Gi::BakeLight> lights;
            for (const auto& f : found)
                lights.push_back(f.second);
            lightCount = lights.size();
            {
                std::ostringstream lt;
                for (const auto& l : lights)
                    lt << l.position.x << ',' << l.position.y << ',' << l.position.z << ',' << l.color.x << ','
                       << l.color.y << ',' << l.color.z << ',' << l.radius << ';';
                lightText = lt.str();
            }

            const auto& rendering = Settings::rendering();
            settings.rays = static_cast<std::uint32_t>(rendering.mGiBakeRays.get());
            settings.bounces = static_cast<std::uint32_t>(rendering.mGiBakeBounces.get());
            settings.seed = static_cast<std::uint32_t>(rendering.mGiBakeSeed.get());
            settings.ambient = this->rendering.getLinearAmbient();
            settings.falloff = readFalloff();

            vsg::vec3 low, high;
            physics->getWorldBounds(low, high);
            const auto finite
                = [](const vsg::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
            if (!finite(low) || !finite(high) || low.x > high.x || low.y > high.y || low.z > high.z)
            {
                Log(Debug::Error) << "[GI] the physics world of \"" << cellName << "\" is empty; nothing to bake";
                return false;
            }
            auto volume = Render::Gi::layoutVolume(low, high, rendering.mGiBakeSpacing, Render::Gi::maxVolumeProbes);
            world = std::make_unique<JoltRayWorld>(*physics);
            baker = std::make_unique<Render::Gi::VolumeBaker>(settings, *world, std::move(lights), std::move(volume));

            const std::string key = Render::Gi::cellKey(cellId);
            outFile = outFolder / "openeden" / "probes" / (key + ".json");
            partialFile = outFolder / "openeden" / "probes" / (key + ".partial");
            progressFile = outFolder / "openeden" / "probes" / (key + ".progress");
            Log(Debug::Info) << "[GI] baking \"" << cellId.name << "\": " << baker->total() << " probes, " << lightCount
                             << " light(s), " << settings.rays << " rays, " << settings.bounces
                             << " bounce(s), spacing " << baker->volume().spacing;
            tryResume();
            started = lastSave = std::chrono::steady_clock::now();
            return true;
        }

        void finish()
        {
            const auto set = makeSet(baker->finish());
            const std::string problem = Render::Gi::validate(set);
            if (!problem.empty())
            {
                Log(Debug::Error) << "[GI] the bake is not valid: " << problem;
                return;
            }
            writeFile(outFile, Render::Gi::writeCanonical(set));
            std::error_code ignored;
            std::filesystem::remove(partialFile, ignored);
            std::filesystem::remove(progressFile, ignored);
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::size_t invalidCount = 0;
            for (const bool b : baker->invalid())
                invalidCount += b ? 1 : 0;
            Log(Debug::Info) << "[GI] wrote " << outFile.string() << " in " << seconds << " s (" << invalidCount
                             << " probe(s) inside geometry were filled from their neighbours)";
        }

        // True when finished.
        bool update()
        {
            ++frames;
            if (!baker)
            {
                auto gameWorld = MWBase::Environment::get().getWorld();
                const MWWorld::CellStore* cell = gameWorld->getPlayerPtr().getCell();
                const bool right = cell && !cell->getCell()->isExterior()
                    && Misc::StringUtils::ciEqual(cell->getCell()->getNameId(), cellName);
                if (right)
                    ++settled;
                if (frames > giveUpFrames)
                {
                    Log(Debug::Error) << "[GI] the cell \"" << cellName << "\" did not load; nothing was baked";
                    return true;
                }
                if (settled < settleFrames)
                    return false;
                if (!begin())
                    return true;
            }
            // The player left the cell (a door, a teleport): the world the rays hit is gone, so stop with nothing
            // written.
            if (MWBase::Environment::get().getWorld()->getPlayerPtr().getCell() != bakedCell)
            {
                Log(Debug::Error) << "[GI] the player left \"" << cellId.name
                                  << "\" during the bake; nothing was written";
                return true;
            }
            const auto budget = std::chrono::milliseconds(Settings::rendering().mGiBakeBudget.get());
            const auto start = std::chrono::steady_clock::now();
            while (!baker->done() && std::chrono::steady_clock::now() - start < budget)
                baker->step(probesPerSlice);
            const std::size_t tenth = baker->next() * 10 / std::max<std::size_t>(1, baker->total());
            if (tenth != lastLogged)
            {
                lastLogged = tenth;
                Log(Debug::Info) << "[GI] " << tenth * 10 << "% (" << baker->next() << " of " << baker->total() << ")";
            }
            if (baker->done())
            {
                finish();
                return true;
            }
            if (std::chrono::steady_clock::now() - lastSave > saveEvery)
            {
                savePartial();
                lastSave = std::chrono::steady_clock::now();
            }
            return false;
        }
    };

    ProbeBaker::ProbeBaker(std::string cellName, std::filesystem::path outFolder, MWRender::RenderManager& rendering)
        : mState(std::make_unique<State>(std::move(cellName), std::move(outFolder), rendering))
    {
    }

    ProbeBaker::~ProbeBaker() = default;

    bool ProbeBaker::update()
    {
        if (mState->finished)
            return true;
        mState->finished = mState->update();
        return mState->finished;
    }
}
