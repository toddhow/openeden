#include "modulesettingspage.hpp"

#include <algorithm>
#include <limits>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include <apps/opencs/model/module/modulesettings.hpp>

namespace CSVPrefs
{
    using namespace openeden::editor;

    namespace
    {
        constexpr double cSpinLimit = 1.0e9;
        constexpr int cSpinDecimals = 4;
        constexpr double cDefaultSpinStep = 0.1;
    }

    ModuleSettingsPage::ModuleSettingsPage(CSMModule::ModuleSettings& settings, QWidget* parent)
        : QScrollArea(parent)
        , mSettings(settings)
    {
        setObjectName("moduleSettingsPage");
        setWidgetResizable(true);
        auto* content = new QWidget(this);
        auto* layout = new QVBoxLayout(content);
        setWidget(content);

        std::string currentModule;
        QFormLayout* form = nullptr;
        // Registration order keeps a module's settings together in the order it declared them.
        std::vector<const CSMModule::SettingEntry*> entries;
        for (const CSMModule::SettingEntry& entry : settings.entries())
            entries.push_back(&entry);
        std::stable_sort(entries.begin(), entries.end(),
            [](const CSMModule::SettingEntry* a, const CSMModule::SettingEntry* b) { return a->mModule < b->mModule; });

        for (const CSMModule::SettingEntry* entry : entries)
        {
            if (form == nullptr || entry->mModule != currentModule)
            {
                currentModule = entry->mModule;
                auto* title = new QLabel(QString::fromStdString(currentModule), content);
                title->setObjectName(QString::fromStdString("moduleSettingsTitle_" + currentModule));
                QFont font = title->font();
                font.setBold(true);
                title->setFont(font);
                layout->addWidget(title);
                form = new QFormLayout();
                layout->addLayout(form);
            }
            const PropertyDesc& property = entry->mDesc.property;
            const std::string id = property.id;
            QWidget* editor = nullptr;
            switch (property.type)
            {
                case PropertyType::Bool:
                {
                    auto* box = new QCheckBox(content);
                    box->setChecked(std::get<bool>(entry->mValue));
                    connect(
                        box, &QCheckBox::toggled, this, [this, id](bool on) { mSettings.set(id, PropertyValue(on)); });
                    editor = box;
                    break;
                }
                case PropertyType::Int:
                {
                    auto* spin = new QSpinBox(content);
                    spin->setRange(static_cast<int>(std::max<double>(property.min, std::numeric_limits<int>::min())),
                        static_cast<int>(std::min<double>(property.max, std::numeric_limits<int>::max())));
                    spin->setValue(static_cast<int>(std::get<std::int64_t>(entry->mValue)));
                    if (!property.unit.empty())
                        spin->setSuffix(" " + QString::fromStdString(property.unit));
                    connect(spin, &QSpinBox::valueChanged, this,
                        [this, id](int value) { mSettings.set(id, PropertyValue(static_cast<std::int64_t>(value))); });
                    editor = spin;
                    break;
                }
                case PropertyType::Float:
                {
                    auto* spin = new QDoubleSpinBox(content);
                    spin->setDecimals(cSpinDecimals);
                    spin->setRange(std::max(property.min, -cSpinLimit), std::min(property.max, cSpinLimit));
                    spin->setSingleStep(property.step > 0 ? property.step : cDefaultSpinStep);
                    spin->setValue(std::get<double>(entry->mValue));
                    if (!property.unit.empty())
                        spin->setSuffix(" " + QString::fromStdString(property.unit));
                    connect(spin, &QDoubleSpinBox::valueChanged, this,
                        [this, id](double value) { mSettings.set(id, PropertyValue(value)); });
                    editor = spin;
                    break;
                }
                case PropertyType::String:
                {
                    auto* line = new QLineEdit(QString::fromStdString(std::get<std::string>(entry->mValue)), content);
                    connect(line, &QLineEdit::editingFinished, this,
                        [this, id, line] { mSettings.set(id, PropertyValue(line->text().toStdString())); });
                    editor = line;
                    break;
                }
                case PropertyType::Choice:
                {
                    auto* combo = new QComboBox(content);
                    for (const std::string& choice : property.choices)
                        combo->addItem(QString::fromStdString(choice));
                    combo->setCurrentText(QString::fromStdString(std::get<std::string>(entry->mValue)));
                    connect(combo, &QComboBox::currentTextChanged, this,
                        [this, id](const QString& text) { mSettings.set(id, PropertyValue(text.toStdString())); });
                    editor = combo;
                    break;
                }
                case PropertyType::Vec3:
                    break; // refused at registration
            }
            if (editor == nullptr)
                continue;
            editor->setToolTip(QString::fromStdString(property.tooltip));
            editor->setObjectName(QString::fromStdString("moduleSetting_" + id));
            form->addRow(QString::fromStdString(property.label.empty() ? property.id : property.label), editor);
        }
        layout->addStretch(1);
    }
}
