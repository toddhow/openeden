// Task 0057 and 0058: draws the launcher's Mods and World LOD pages off screen and saves them as PNG files, on
// generated data only (a fake game folder with plugin headers, three small mods, an openmw.cfg). It uses the real
// page classes with a configuration manager that points at the work folder, so the real user folders are never
// read or written. Not part of the product: built only with OPENEDEN_BUILD_LAUNCHER_PAGEGRAB.
//
// usage: openeden-launcher-pagegrab <work folder, below I:/Projects/OpenEden-work>
// Run it with QT_QPA_PLATFORM=offscreen (set here when unset).
#include "modspage.hpp"
#include "worldlodpage.hpp"

#include <components/config/gamesettings.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/qtconversion.hpp>
#include <components/modorganizer/profilestore.hpp>

#include <boost/program_options.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    void put32(std::string& out, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }

    std::string subrecord(const char* tag, const std::string& data)
    {
        std::string out(tag, 4);
        put32(out, static_cast<std::uint32_t>(data.size()));
        return out + data;
    }

    /// A TES3 file that only has a header: author, description, the master list.
    std::string makeTes3(const std::string& author, const std::vector<std::string>& masters)
    {
        std::string hedr;
        put32(hedr, 0x3f99999a); // version 1.2 as a float
        put32(hedr, 0); // flags
        std::string a = author;
        a.resize(32, '\0');
        hedr += a;
        std::string d = "generated for the launcher page check";
        d.resize(256, '\0');
        hedr += d;
        put32(hedr, 0);
        std::string body = subrecord("HEDR", hedr);
        for (const std::string& master : masters)
        {
            body += subrecord("MAST", master + std::string(1, '\0'));
            body += subrecord("DATA", std::string(8, '\0'));
        }
        std::string out("TES3", 4);
        put32(out, static_cast<std::uint32_t>(body.size()));
        put32(out, 0);
        put32(out, 0);
        return out + body;
    }

    void write(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << text;
    }

    void pump(int milliseconds)
    {
        QTimer timer;
        timer.setSingleShot(true);
        QEventLoop loop;
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(milliseconds);
        loop.exec();
    }

    bool grab(QWidget& widget, const QString& file)
    {
        const QImage image = widget.grab().toImage();
        std::printf("%s %dx%d\n", file.toLocal8Bit().constData(), image.width(), image.height());
        return image.save(file);
    }

    void click(QWidget& root, const QString& text)
    {
        for (QPushButton* button : root.findChildren<QPushButton*>())
            if (button->text() == text && button->isVisibleTo(&root) && button->isEnabled())
            {
                button->click();
                return;
            }
        std::printf("no button \"%s\"\n", text.toLocal8Bit().constData());
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fputs("usage: openeden-launcher-pagegrab <work folder>\n", stderr);
        return 2;
    }
    const fs::path work = fs::absolute(argv[1]);
    const std::string workText = work.generic_string();
    if (workText.find("OpenEden-work") == std::string::npos)
    {
        std::fputs("the work folder must be below OpenEden-work\n", stderr);
        return 2;
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // ---- generated data
    std::error_code ec;
    fs::remove_all(work / "data", ec);
    const fs::path game = work / "data" / "game" / "Data Files";
    const fs::path mods = work / "data" / "mods";
    const fs::path cfg = work / "data" / "cfg";
    const fs::path userData = work / "data" / "userdata";
    fs::create_directories(cfg);
    fs::create_directories(userData / "openeden" / "lod");
    write(game / "Morrowind.esm", makeTes3("Bethesda", {}));
    write(game / "Morrowind.bsa", "bsa");
    write(mods / "Better Bodies" / "BetterBodies.esp", makeTes3("A. Modder", { "Morrowind.esm" }));
    write(mods / "Better Bodies" / "meshes" / "b" / "body.nif", "x");
    write(mods / "Better Bodies" / "textures" / "skin.dds", "x");
    write(mods / "Fancy Lights" / "FancyLights.esp", makeTes3("B. Modder", { "Morrowind.esm", "BetterBodies.esp" }));
    write(mods / "Fancy Lights" / "meshes" / "b" / "body.nif", "y");
    write(mods / "Fancy Lights" / "meshes" / "l" / "lantern.nif", "y");
    write(mods / "Wrapped Quests" / "Data Files" / "Quests.esp", makeTes3("C. Modder", { "Morrowind.esm" }));
    write(mods / "Wrapped Quests" / "Data Files" / "textures" / "skin.dds", "y");
    write(mods / "Packed Textures.edenpak", "EDENPAK01 generated");
    write(userData / "openeden" / "lod" / "terrain.edenpak", std::string(3'400'000, 'x'));

    auto q = [](const fs::path& p) { return QString::fromStdString(p.generic_string()); };
    QString cfgText;
    cfgText += "data=\"" + q(game) + "\"\n";
    cfgText += "data=\"" + q(mods / "Better Bodies") + "\"\n";
    cfgText += "data=\"" + q(mods / "Fancy Lights") + "\"\n";
    cfgText += "data=\"" + q(mods / "Packed Textures.edenpak") + "\"\n";
    cfgText += "data-local=\"" + q(work / "data" / "local") + "\"\n";
    cfgText += "fallback-archive=Morrowind.bsa\n";
    cfgText += "content=Morrowind.esm\ncontent=BetterBodies.esp\ncontent=FancyLights.esp\n";
    write(cfg / "openmw.cfg", cfgText.toStdString());

    // ---- the pages, on a configuration manager that points at the work folder
    namespace bpo = boost::program_options;
    bpo::options_description description;
    Files::ConfigurationManager::addCommonOptions(description);
    const std::string configArg = "--config=" + (cfg).string();
    const std::string userArg = "--user-data=" + userData.string();
    const char* fake[] = { "pagegrab", "--replace=config", configArg.c_str(), userArg.c_str() };
    bpo::variables_map variables;
    bpo::store(bpo::parse_command_line(4, fake, description), variables);
    bpo::notify(variables);
    Files::ConfigurationManager manager(true);
    manager.readConfiguration(variables, description, true);
    std::printf("user config: %s\nuser data: %s\n", manager.getUserConfigPath().string().c_str(),
        manager.getUserDataPath().string().c_str());
    if (manager.getUserConfigPath() != cfg)
    {
        std::fputs(
            "the configuration manager did not take the work folder; stopping before touching anything\n", stderr);
        return 3;
    }

    QApplication app(argc, argv);
    const QString shots = q(work / "shots");
    QDir().mkpath(shots);

    // The data= line of a pack must survive the launcher's path validation (task 0057, the follow-up of task 0064)
    {
        Config::GameSettings settings(manager);
        QFile file(q(cfg / "openmw.cfg"));
        file.open(QIODevice::ReadOnly | QIODevice::Text);
        QTextStream stream(&file);
        settings.readFile(stream, q(cfg));
        std::printf("data dirs kept by validatePaths:\n");
        for (const Config::SettingValue& dir : settings.getDataDirs())
            std::printf("  %s\n", dir.value.toLocal8Bit().constData());
    }

    // The first run of the Mods page reads openmw.cfg; here the store is made up front (the way a player with no
    // global openmw.cfg gets it) so that the locked game data row shows
    {
        std::ifstream in(cfg / "openmw.cfg", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ModOrganizer::Store store;
        ModOrganizer::importIfEmpty(store, ModOrganizer::snapshotFromConfigText(text));
        for (const std::string& error : ModOrganizer::saveStore(ModOrganizer::storePaths(cfg), store))
            std::printf("store: %s\n", error.c_str());
    }

    int failures = 0;
    {
        Launcher::ModsPage page(manager);
        page.setGameRunningCheck([] { return false; }); // other sessions may have a real game running
        page.resize(1100, 700);
        page.show();
        page.loadSettings();
        pump(1500); // the plugin report runs on a worker
        failures += !grab(page, shots + "/mods_1_mods.png");
        if (auto* tabs = page.findChild<QTabWidget*>())
        {
            tabs->setCurrentIndex(1);
            pump(300);
            failures += !grab(page, shots + "/mods_2_plugins.png");
            // Put FancyLights before its master BetterBodies: a hint appears, nothing is reordered by itself
            for (QTableWidget* table : page.findChildren<QTableWidget*>())
                if (table->accessibleName() == "Plugins in load order")
                    table->selectRow(2);
            click(page, "Move up");
            pump(1500);
            failures += !grab(page, shots + "/mods_2b_plugin_hint.png");
            tabs->setCurrentIndex(2);
            click(page, "Scan for conflicts");
            pump(1500);
            failures += !grab(page, shots + "/mods_3_conflicts.png");
            tabs->setCurrentIndex(3);
            click(page, "Check");
            pump(1500);
            failures += !grab(page, shots + "/mods_4_repack.png");
            tabs->setCurrentIndex(0);
        }
        // Apply the profile: openmw.cfg is rewritten (owned keys only) and a backup is made
        click(page, "Apply profile to openmw.cfg");
        pump(300);
        failures += !grab(page, shots + "/mods_5_applied.png");
        page.saveSettings();
        pump(100);
        {
            std::ifstream in(cfg / "openmw.cfg", std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::printf("openmw.cfg after apply:\n%s\n", text.c_str());
            int backups = 0;
            for (const auto& entry : fs::directory_iterator(cfg))
                if (entry.path().filename().string().rfind("openmw.cfg.bak", 0) == 0)
                    ++backups;
            std::printf("backups: %d\n", backups);
        }
    }
    {
        Launcher::WorldLodPage page(manager);
        page.setGameRunningCheck([] { return false; }); // other sessions may have a real game running
        page.resize(1100, 900);
        page.show();
        page.loadSettings();
        pump(800);
        failures += !grab(page, shots + "/worldlod_1_default.png");
        for (QCheckBox* box : page.findChildren<QCheckBox*>())
            if (box->text() == "Enable world LOD" || box->text() == "Far object clusters")
                box->setChecked(true);
        pump(300);
        failures += !grab(page, shots + "/worldlod_2_enabled.png");
        // The bake commands are stood in for by openmw.exe of tools/launcher_check/fakebake.cpp
        click(page, "Bake terrain tiles");
        pump(400);
        failures += !grab(page, shots + "/worldlod_3_running.png");
        pump(2500);
        failures += !grab(page, shots + "/worldlod_4_finished.png");
        click(page, "Bake object clusters");
        pump(3000);
        failures += !grab(page, shots + "/worldlod_5_failed.png");
    }
    std::printf("failures: %d\n", failures);
    return failures;
}
