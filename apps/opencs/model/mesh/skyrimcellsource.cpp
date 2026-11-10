#include "skyrimcellsource.hpp"

#include <cstdlib>
#include <fstream>

#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/world_view.hpp>

namespace CSMMesh
{
    struct SkyrimCellSource::Impl
    {
        std::unique_ptr<game_skyrimse::DataFolderFiles> files;
        std::unique_ptr<game_skyrimse::SkyrimWorldView> view;
        // Declared after the view and the source so that the store, which holds the source by reference, goes first.
        std::unique_ptr<game_skyrimse::SkyrimCellSource> source;
        std::unique_ptr<NeutralWorld::NeutralWorldStore> store;
    };

    SkyrimCellSource& SkyrimCellSource::instance()
    {
        static SkyrimCellSource source;
        return source;
    }

    SkyrimCellSource::SkyrimCellSource() = default;

    SkyrimCellSource::~SkyrimCellSource() = default;

    bool SkyrimCellSource::configure(
        const std::vector<std::filesystem::path>& plugins, const std::filesystem::path& dataFolder, std::string& error)
    {
        auto impl = std::make_unique<Impl>();
        impl->files = std::make_unique<game_skyrimse::DataFolderFiles>(dataFolder);
        impl->view = std::make_unique<game_skyrimse::SkyrimWorldView>(
            game_skyrimse::SkyrimWorldView::Options{}, impl->files.get());
        std::vector<std::string> problems;
        const std::size_t taken = impl->view->load(plugins, problems);
        if (taken == 0)
        {
            error = problems.empty() ? std::string("no plugin was given") : problems.front();
            return false;
        }
        const auto& stats = impl->view->stats();
        mSummary = std::to_string(taken) + " plugins, " + std::to_string(stats.mCells) + " cells, "
            + std::to_string(stats.mWorlds) + " worlds, " + std::to_string(stats.mBaseForms) + " base forms"
            + (problems.empty() ? "" : ", " + std::to_string(problems.size()) + " plugins refused");
        impl->source = std::make_unique<game_skyrimse::SkyrimCellSource>(*impl->view);
        impl->store = std::make_unique<NeutralWorld::NeutralWorldStore>();
        std::string sourceError;
        if (!impl->store->addSource(*impl->source, sourceError))
        {
            error = sourceError;
            return false;
        }
        mView = nullptr;
        mStore = nullptr;
        mImpl = std::move(impl);
        mView = mImpl->view.get();
        mStore = mImpl->store.get();
        return true;
    }

    bool SkyrimCellSource::configureFromEnvironment(std::string& error)
    {
        const char* listEnv = std::getenv("OPENEDEN_SKYRIMSE_PLUGINS");
        const char* dataEnv = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (listEnv == nullptr || *listEnv == '\0')
        {
            error = "OPENEDEN_SKYRIMSE_PLUGINS is not set (a text file with one plugin path per line, in load order)";
            return false;
        }
        std::ifstream list(listEnv);
        if (!list)
        {
            error = std::string("cannot read the plugin list ") + listEnv;
            return false;
        }
        std::vector<std::filesystem::path> plugins;
        for (std::string line; std::getline(list, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            plugins.emplace_back(line);
        }
        std::filesystem::path data;
        if (dataEnv != nullptr && *dataEnv != '\0')
            data = dataEnv;
        else if (!plugins.empty())
            data = plugins.front().parent_path();
        return configure(plugins, data, error);
    }
}
