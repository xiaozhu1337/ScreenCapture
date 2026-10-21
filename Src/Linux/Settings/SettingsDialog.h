#pragma once
#include <QDialog>
class Settings;
struct Preferences;
class QTabWidget;
class QTableWidget;
class QButtonGroup;
class QMouseEvent;
class QResizeEvent;

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(Settings& settings, QWidget* parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    bool save(Preferences next);
    void addGeneral();
    void addShortcuts();
    void addToolbar();
    void addAbout();
    void refreshToolbar();
    void retranslate();
    Settings& settings;
    QTabWidget* tabs;
    QButtonGroup* navigation;
    QTableWidget* tools;
};
