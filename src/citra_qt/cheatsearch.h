#pragma once

#include <functional>
#include <memory>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>

namespace Ui {
class CheatSearch;
} // namespace Ui

struct FoundItem {
    QString address;
    QString value;
};

class CheatSearch : public QDialog {
    Q_OBJECT

public:
    explicit CheatSearch(QWidget* parent = nullptr);
    ~CheatSearch();

private:
    std::unique_ptr<Ui::CheatSearch> ui;
    std::vector<FoundItem> previous_found;

    void OnScan(bool is_next_scan);
    void OnScanTypeChanged(int index);
    void OnValueTypeChanged(int index);
    void OnHexCheckedChanged(bool checked);
    void LoadTable(const std::vector<FoundItem>& items);

    template <typename T>
    std::vector<FoundItem> FirstSearch(const T value, std::function<bool(int, int, int)> comparer);

    template <typename T>
    std::vector<FoundItem> NextSearch(const T value, std::function<bool(int, int, int)> comparer);

    bool Equals(int a, int b, int c);
    bool LessThan(int a, int b, int c);
    bool GreaterThan(int a, int b, int c);
    bool Between(int min, int max, int value);
};

class ModifyAddressDialog : public QDialog {
    Q_OBJECT

public:
    ModifyAddressDialog(QWidget* parent, const QString& address, int type, const QString& value);
    ~ModifyAddressDialog();

    QString return_value;

private:
    QLineEdit* address_block;
    QComboBox* type_select;
    QLineEdit* value_block;

    void OnOkClicked();
};
