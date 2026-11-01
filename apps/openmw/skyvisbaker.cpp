#include "skyvisbaker.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/render/skyvisbake.hpp>
#include <components/render/skyvisibility.hpp>
#include <components/settings/values.hpp>

#include "mwbase/environment.hpp"
#include "mwbase/world.hpp"
#include "mwphysics/joltlayers.hpp"
#include "mwphysics/physicssystem.hpp"
#include "mwrender/rendermanager.hpp"
#include "mwworld/cellstore.hpp"
#include "mwworld/ptr.hpp"

namespace OMW
{
    namespace
    {
        // Frames to wait after the player is in the right cell, so that its physics bodies and its neighbours' exist.
        constexpr unsigned int settleFrames = 90;
        // Frames to wait for a cell before giving up on it.
        constexpr unsigned int giveUpFrames = 3000;
        // Probes baked between two looks at the clock.
        constexpr std::size_t probesPerSlice = 2;
        constexpr std::chrono::seconds saveEvery{ 30 };
        // The most cells one command may name (a whole region is a few hundred).
        constexpr std::size_t maxCells = 4096;
        // What a bake ray can hit: static geometry and doors, no actors, projectiles or water.
        constexpr int bakeMask = MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::DOOR;
        // The terrain is looked for between these heights (Morrowind's land runs from about -2000 to 8000).
        constexpr float groundTop = 20000.f;
        constexpr float groundBottom = -20000.f;

        class JoltSkyWorld final : public Render::Gi::SkyRayWorld
        {
        public:
            explicit JoltSkyWorld(const MWPhysics::PhysicsSystem& physics)
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
                hitNormal = result.mHitNormal;
                return true;
            }

            bool groundHeight(float x, float y, float& z) const override
            {
                // A ray exactly on the edge between two terrain squares can slip through, and a cell's border
                // columns lie on such edges, so a miss is tried again a unit to each side (the same order every
                // time, so both cells sharing a border column get the same answer).
                constexpr float nudge = 1.f;
                constexpr float offsets[5][2]
                    = { { 0.f, 0.f }, { nudge, 0.f }, { -nudge, 0.f }, { 0.f, nudge }, { 0.f, -nudge } };
                for (const auto& offset : offsets)
                {
                    const float px = x + offset[0];
                    const float py = y + offset[1];
                    const auto result = mPhysics.castRay(
                        vsg::vec3(px, py, groundTop), vsg::vec3(px, py, groundBottom), MWPhysics::Layers::HEIGHTMAP);
                    if (result.mHit)
                    {
                        z = result.mHitPos.z;
                        return true;
                    }
                }
                return false;
            }

        private:
            const MWPhysics::PhysicsSystem& mPhysics;
        };

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
                out.flush();
                if (!out.good())
                {
                    Log(Debug::Error) << "[GI] cannot write " << temporary.string();
                    return;
                }
            }
            std::filesystem::rename(temporary, path);
        }

        // The spacing the setting asks for, as one that cuts a cell into 4 to 64 equal parts: 128, 256, 512, 1024 or
        // 2048 (the nearest, going up on a tie).
        float snapSpacing(float wanted)
        {
            float best = 128.f;
            for (float candidate = 256.f; candidate <= 2048.f; candidate *= 2.f)
                if (std::abs(candidate - wanted) <= std::abs(best - wanted))
                    best = candidate;
            return best;
        }

        bool readInt(std::string_view text, int& out)
        {
            const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
            return ec == std::errc() && ptr == text.data() + text.size();
        }

        bool readCell(std::string_view text, std::pair<int, int>& out)
        {
            const std::size_t comma = text.find(',');
            if (comma == std::string_view::npos)
                return false;
            return readInt(text.substr(0, comma), out.first) && readInt(text.substr(comma + 1), out.second);
        }
    }

    std::vector<std::pair<int, int>> parseSkyBakeCells(const std::string& argument, std::string& error)
    {
        std::vector<std::pair<int, int>> cells;
        std::pair<int, int> low, high;
        const std::size_t colon = argument.find(':');
        bool ok;
        if (colon == std::string::npos)
        {
            ok = readCell(argument, low);
            high = low;
        }
        else
            ok = readCell(std::string_view(argument).substr(0, colon), low)
                && readCell(std::string_view(argument).substr(colon + 1), high);
        if (!ok || high.first < low.first || high.second < low.second)
        {
            error = "expected \"<x>,<y>\" or \"<x0>,<y0>:<x1>,<y1>\" with the first corner not above the second";
            return cells;
        }
        const long long width = static_cast<long long>(high.first) - low.first + 1;
        const long long height = static_cast<long long>(high.second) - low.second + 1;
        const long long count = width > static_cast<long long>(maxCells) || height > static_cast<long long>(maxCells)
            ? static_cast<long long>(maxCells) + 1
            : width * height;
        if (count > static_cast<long long>(maxCells))
        {
            error
                = "that names " + std::to_string(count) + " cells; at most " + std::to_string(maxCells) + " at a time";
            return cells;
        }
        for (int y = low.second; y <= high.second; ++y)
            for (int x = low.first; x <= high.first; ++x)
                cells.emplace_back(x, y);
        return cells;
    }

    struct SkyVisBaker::State
    {
        std::string argument;
        std::filesystem::path outFolder;
        std::vector<std::pair<int, int>> cells;
        std::size_t cellIndex = 0;
        bool prepared = false;
        bool entered = false;
        unsigned int waited = 0;
        unsigned int settled = 0;
        bool finished = false;
        std::size_t written = 0;
        std::size_t skipped = 0;
        std::size_t failed = 0;

        Render::Gi::SkyBakeSettings settings;
        std::unique_ptr<JoltSkyWorld> world;
        std::unique_ptr<Render::Gi::SkyVisBaker> baker;
        MWWorld::CellStore* bakedCell = nullptr;
        std::filesystem::path outFile;
        std::filesystem::path partialFile;
        std::filesystem::path progressFile;
        std::chrono::steady_clock::time_point started;
        std::chrono::steady_clock::time_point lastSave;
        std::size_t lastLogged = 99;

        State(std::string arg, std::filesystem::path out)
            : argument(std::move(arg))
            , outFolder(std::move(out))
        {
        }

        std::pair<int, int> current() const { return cells[cellIndex]; }

        void setPaths()
        {
            const auto [x, y] = current();
            const std::filesystem::path base = outFolder / Render::Gi::skyVisFilePath(x, y);
            outFile = base;
            partialFile = base;
            partialFile.replace_extension(".partial");
            progressFile = base;
            progressFile.replace_extension(".progress");
        }

        // Everything that changes the bake, as text, to tell whether a file or a partial file belongs to it.
        std::string signature() const
        {
            std::ostringstream s;
            s << "skyvis rays " << settings.rays << " seed " << settings.seed << " spacing " << settings.spacing
              << " levelspacing " << settings.levelSpacing << " levels " << settings.levels << " distance "
              << settings.skyDistance;
            return s.str();
        }

        bool fileMatchesSettings(const Render::Gi::SkyVisField& f) const
        {
            return f.rays == settings.rays && f.seed == settings.seed && f.spacing == settings.spacing
                && f.levelSpacing == settings.levelSpacing && f.levels == settings.levels;
        }

        void prepare()
        {
            prepared = true;
            const auto& renderSettings = Settings::rendering();
            settings.rays = static_cast<std::uint32_t>(renderSettings.mGiBakeSkyRays.get());
            settings.seed = static_cast<std::uint32_t>(renderSettings.mGiBakeSeed.get());
            settings.spacing = snapSpacing(renderSettings.mGiBakeSkySpacing);
        }

        // True when the cell already has a file made with these settings.
        bool alreadyBaked()
        {
            if (!std::filesystem::exists(outFile))
                return false;
            Render::Gi::ProbeError error;
            const auto field = Render::Gi::parseSkyVis(readFile(outFile), outFile.string(), error);
            if (!field)
            {
                Log(Debug::Warning) << "[GI] " << error.describe() << "; baking the cell again";
                return false;
            }
            return field->cellX == current().first && field->cellY == current().second && fileMatchesSettings(*field);
        }

        void savePartial() const
        {
            if (!baker)
                return;
            writeFile(partialFile, Render::Gi::writeCanonical(baker->field()));
            std::ostringstream p;
            p << signature() << "\nnext " << baker->next() << "\ninvalid";
            for (std::size_t i = 0; i < baker->invalid().size(); ++i)
                if (baker->invalid()[i])
                    p << ' ' << i;
            p << "\nend\n";
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
            const auto field = Render::Gi::parseSkyVis(readFile(partialFile), partialFile.string(), error);
            if (!field)
            {
                Log(Debug::Warning) << "[GI] cannot resume: " << error.describe();
                return;
            }
            if (!baker->resume(*field, next, invalid))
            {
                Log(Debug::Warning) << "[GI] cannot resume: " << partialFile.string() << " does not match this bake";
                return;
            }
            Log(Debug::Info) << "[GI] resumed at probe " << baker->next() << " of " << baker->total();
        }

        void begin()
        {
            auto gameWorld = MWBase::Environment::get().getWorld();
            bakedCell = gameWorld->getPlayerPtr().getCell();
            world = std::make_unique<JoltSkyWorld>(*gameWorld->getPhysicsSystem());
            baker = std::make_unique<Render::Gi::SkyVisBaker>(settings, *world, current().first, current().second);
            Log(Debug::Info) << "[GI] baking exterior cell " << current().first << "," << current().second << ": "
                             << baker->total() << " probes, " << settings.rays << " rays, spacing " << settings.spacing;
            tryResume();
            started = lastSave = std::chrono::steady_clock::now();
            lastLogged = 99;
        }

        void nextCell()
        {
            baker.reset();
            world.reset();
            entered = false;
            waited = 0;
            settled = 0;
            ++cellIndex;
        }

        void finishCell()
        {
            const Render::Gi::SkyVisField field = baker->finish();
            const std::string problem = Render::Gi::validate(field);
            if (!problem.empty())
            {
                Log(Debug::Error) << "[GI] the bake is not valid: " << problem;
                std::error_code ignoredPartial;
                std::filesystem::remove(partialFile, ignoredPartial);
                std::filesystem::remove(progressFile, ignoredPartial);
                ++failed;
                nextCell();
                return;
            }
            writeFile(outFile, Render::Gi::writeCanonical(field));
            std::error_code ignored;
            std::filesystem::remove(partialFile, ignored);
            std::filesystem::remove(progressFile, ignored);
            std::size_t invalidCount = 0;
            for (const bool b : baker->invalid())
                invalidCount += b ? 1 : 0;
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            Log(Debug::Info) << "[GI] wrote " << outFile.string() << " in " << seconds << " s (" << invalidCount
                             << " probe(s) inside geometry were filled from their neighbours)";
            ++written;
            nextCell();
        }

        void moveTo(int x, int y)
        {
            ESM::Position pos;
            pos.pos[0] = (static_cast<float>(x) + 0.5f) * Render::Gi::exteriorCellSize;
            pos.pos[1] = (static_cast<float>(y) + 0.5f) * Render::Gi::exteriorCellSize;
            pos.pos[2] = 0.f;
            pos.rot[0] = pos.rot[1] = pos.rot[2] = 0.f;
            MWBase::Environment::get().getWorld()->changeToCell(ESM::RefId::esm3ExteriorCell(x, y), pos, true);
        }

        // True when finished.
        bool update()
        {
            if (!prepared)
            {
                prepare();
                std::string error;
                cells = parseSkyBakeCells(argument, error);
                if (cells.empty())
                {
                    Log(Debug::Error) << "[GI] --bake-gi-exterior: " << error;
                    return true;
                }
                Log(Debug::Info) << "[GI] baking the sky visibility of " << cells.size() << " exterior cell(s)";
            }
            if (cellIndex >= cells.size())
            {
                Log(Debug::Info) << "[GI] done: " << written << " written, " << skipped << " already baked, " << failed
                                 << " failed";
                return true;
            }
            if (!baker)
            {
                setPaths();
                if (!entered)
                {
                    if (alreadyBaked())
                    {
                        Log(Debug::Info) << "[GI] exterior cell " << current().first << "," << current().second
                                         << " is already baked with these settings; skipped";
                        ++skipped;
                        nextCell();
                        return false;
                    }
                    moveTo(current().first, current().second);
                    entered = true;
                    return false;
                }
                auto gameWorld = MWBase::Environment::get().getWorld();
                const MWWorld::CellStore* cell = gameWorld->getPlayerPtr().getCell();
                const bool right = cell && cell->getCell()->isExterior()
                    && cell->getCell()->getGridX() == current().first
                    && cell->getCell()->getGridY() == current().second;
                if (right)
                    ++settled;
                else
                    settled = 0;
                if (++waited > giveUpFrames)
                {
                    Log(Debug::Error) << "[GI] exterior cell " << current().first << "," << current().second
                                      << " did not load; nothing was baked for it";
                    ++failed;
                    nextCell();
                    return false;
                }
                if (settled < settleFrames)
                    return false;
                begin();
            }
            // The player left the cell: the world the rays hit is gone, so give this cell up with nothing written.
            if (MWBase::Environment::get().getWorld()->getPlayerPtr().getCell() != bakedCell)
            {
                Log(Debug::Error) << "[GI] the player left the cell during the bake; nothing was written for it";
                ++failed;
                nextCell();
                return false;
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
                finishCell();
                return false;
            }
            if (std::chrono::steady_clock::now() - lastSave > saveEvery)
            {
                savePartial();
                lastSave = std::chrono::steady_clock::now();
            }
            return false;
        }
    };

    SkyVisBaker::SkyVisBaker(
        std::string argument, std::filesystem::path outFolder, MWRender::RenderManager& /*rendering*/)
        : mState(std::make_unique<State>(std::move(argument), std::move(outFolder)))
    {
    }

    SkyVisBaker::~SkyVisBaker() = default;

    bool SkyVisBaker::update()
    {
        if (mState->finished)
            return true;
        mState->finished = mState->update();
        return mState->finished;
    }
}
