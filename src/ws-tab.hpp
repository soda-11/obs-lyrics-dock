#pragma once
// ws-tab.hpp — WebSocket settings tab embedded in LyricsDock  v1.0

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QLineEdit>

class WsTab : public QWidget {
    Q_OBJECT
public:
    explicit WsTab(QWidget *parent = nullptr);

    // Called by the dock when this tab is shown / hidden,
    // so the QR overlay on OBS can follow the "show QR" checkbox.
    void onTabActivated();
    void onTabDeactivated();

    void saveSettings();
    void loadSettings();

private slots:
    void onEnableToggled(bool checked);
    void onApply();
    void onRefreshClients();
    void onShowQrToggled(bool checked);

private:
    void buildUi();
    void updateStatus();
    void pushQrToObs(bool show);
    QString settingsPath() const;

    QCheckBox   *m_enableCheck;
    QSpinBox    *m_portSpin;
    QLabel      *m_statusLbl;
    QLabel      *m_urlLbl;
    QLabel      *m_qrLbl;       // preview QR in the tab
    QCheckBox   *m_showQrCheck; // show QR on OBS preview
    QListWidget *m_clientList;
    QPushButton *m_applyBtn;
    QLineEdit   *m_pinEdit;
    QTimer      *m_refreshTimer;
    bool         m_tabActive = false;
};
