#pragma once
// settings-dialog.hpp  v9.5.1
#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QLineEdit>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    void saveSettings();
    void loadSettings();

private slots:
    void onEnableToggled(bool checked);
    void onApply();
    void onRefreshClients();

private:
    void buildUi();
    void updateStatus();
    void drawQrCode(const QString &text);
    QString settingsPath() const;

    QCheckBox   *m_enableCheck;
    QSpinBox    *m_portSpin;
    QLabel      *m_statusLbl;
    QLabel      *m_urlLbl;
    QLabel      *m_qrLbl;
    QListWidget *m_clientList;
    QPushButton *m_applyBtn;
    QLineEdit   *m_pinEdit;
    QTimer      *m_refreshTimer;
};
