#include "test_settingswidget.h"
#include "src/settingsdialog/settingswidget.h"
#include <QCheckBox>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QSettings>
#include <limits>
#include "src/settings/editorsettings.h"

namespace {
class CheckBoxSettingsWidget : public SettingsWidget
{
public:
    CheckBoxSettingsWidget() : SettingsWidget("Test", "Test", nullptr), checkbox(this) {}
    QCheckBox checkbox;
protected:
    void doLoad() override { checkbox.setChecked(!checkbox.isChecked()); }
    void doSave() override {}
};
}

void TestSettingsWidget::test_checkbox_disconnect()
{
    CheckBoxSettingsWidget widget;
    widget.connectInputs();
    QSignalSpy changes(&widget, &SettingsWidget::settingsChanged);
    QVERIFY(changes.isValid());
    widget.checkbox.setChecked(true);
    QCOMPARE(changes.count(), 1);
    changes.clear();
    widget.disconnectInputs();
    widget.checkbox.setChecked(false);
    QCOMPARE(changes.count(), 0);
}

void TestSettingsWidget::malformed_editor_geometry_values_are_bounded(){
    QTemporaryDir temp;QVERIFY(temp.isValid());const auto ini=temp.filePath("bad.ini");
    {QSettings file(ini,QSettings::IniFormat);file.beginGroup(SETTING_EDITOR);file.setValue("font_size",INT_MAX);file.setValue("gutter_font_size",INT_MIN);file.setValue("tab_width",0);file.setValue("line_spacing",std::numeric_limits<double>::quiet_NaN());file.setValue("tips_delay",INT_MAX);file.setValue("auto_save_interal",INT_MAX);file.endGroup();file.sync();}
    SettingsPersistor persistor(ini);EditorSettings settings(&persistor);settings.load();
    QCOMPARE(settings.fontSize(),72);QCOMPARE(settings.gutterFontSize(),6);QCOMPARE(settings.tabWidth(),1);QCOMPARE(settings.lineSpacing(),1.1);QCOMPARE(settings.tipsDelay(),10000);QCOMPARE(settings.autoSaveInterval(),1440);
}

void TestSettingsWidget::test_repeated_load_does_not_duplicate_connections()
{
    CheckBoxSettingsWidget widget;
    widget.connectInputs();
    QSignalSpy changes(&widget, &SettingsWidget::settingsChanged);
    QVERIFY(changes.isValid());
    for (int i = 0; i < 3; ++i) {
        changes.clear();
        widget.load();
        QCOMPARE(changes.count(), 1);
        QCOMPARE(changes.at(0).at(0).toBool(), false);
        QVERIFY(!widget.isSettingsChanged());
    }
    changes.clear();
    widget.checkbox.setChecked(!widget.checkbox.isChecked());
    QCOMPARE(changes.count(), 1);
    QCOMPARE(changes.at(0).at(0).toBool(), true);
    QVERIFY(widget.isSettingsChanged());
}
