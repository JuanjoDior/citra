// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QWidget>

namespace Ui {
class ConfigureGeneral;
} // namespace Ui

class ConfigureGeneral : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureGeneral(QWidget* parent = nullptr);
    ~ConfigureGeneral();

    void applyConfiguration();
    void retranslateUi();

    bool sd_card_root_changed;

private slots:
    void onLanguageChanged(int index);

signals:
    void languageChanged(const QString& locale);

private:
    void setConfiguration();
    std::unique_ptr<Ui::ConfigureGeneral> ui;
};
