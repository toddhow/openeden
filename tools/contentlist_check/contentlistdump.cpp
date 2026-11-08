// Task 0083 stage 5: prints what the launcher's content list model holds for the plugins in some folders, so that the
// output before and after the model moved to Content::probe can be compared byte for byte. Aggregates of the user's
// own data only: the tool prints file properties of the plugins you point it at (names, authors, descriptions), so run
// it on generated plugins, or on your own game data and keep the output local.
//
// usage: openeden-contentlist-dump <encoding> <folder> [<folder> ...]
#include <components/contentselector/model/contentmodel.hpp>
#include <components/contentselector/model/esmfile.hpp>

#include <QCoreApplication>
#include <QFileInfo>
#include <QIcon>
#include <QTextStream>

#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3)
    {
        std::fputs("usage: openeden-contentlist-dump <encoding> <folder> [<folder> ...]\n", stderr);
        return 2;
    }
    QIcon warning, error;
    ContentSelectorModel::ContentModel model(nullptr, warning, error, true);
    model.setEncoding(argv[1]);
    for (int i = 2; i < argc; ++i)
        model.addFiles(QString::fromLocal8Bit(argv[i]), true);
    model.sortFiles();

    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    for (int row = 0; row < model.rowCount(); ++row)
    {
        const ContentSelectorModel::EsmFile* file = model.item(row);
        out << "row " << row << "\n";
        for (int prop = 0; prop <= ContentSelectorModel::EsmFile::FileProperty_GameFile; ++prop)
        {
            const QVariant value = file->fileProperty(static_cast<ContentSelectorModel::EsmFile::FileProperty>(prop));
            // the date and path depend on where and when the files were made, they are printed by name only
            if (prop == ContentSelectorModel::EsmFile::FileProperty_FilePath)
                out << "  prop " << prop << " = " << QFileInfo(value.toString()).fileName() << "\n";
            else
                out << "  prop " << prop << " = " << value.toString() << "\n";
        }
        out << "  gameFiles = " << file->gameFiles().join("|") << "\n";
        out << "  tooltip = " << file->toolTip() << "\n";
        const QModelIndex index = model.index(row, 0);
        out << "  flags = " << static_cast<int>(model.flags(index)) << "\n";
        out << "  modelTooltip = " << model.data(index, Qt::ToolTipRole).toString() << "\n";
    }
    out << "rows " << model.rowCount() << "\n";
    return 0;
}
