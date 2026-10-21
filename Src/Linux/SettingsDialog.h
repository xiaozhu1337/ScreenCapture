#pragma once
#include <QDialog>
class Settings;

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(Settings& settings, QWidget* parent = nullptr);
};
