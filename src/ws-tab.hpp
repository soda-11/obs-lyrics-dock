#pragma once
// ws-tab.hpp — WebSocket settings tab  v1.5
// Preview capture: QTimer on UI thread acquires graphics mutex, renders the
// OBS composite output into a texrender, stages to CPU, maps and JPEG-encodes.
// Double-buffered: stage at end of tick N, map at start of tick N+1 (GPU is
// done by then), so obs_enter_graphics hold time is <5 ms.

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QLineEdit>
#include <QSlider>
#include <QByteArray>

class WsTab : public QWidget {
    Q_OBJECT
public:
    explicit WsTab(QWidget *parent = nullptr);
    ~WsTab();

    void onTabActivated();
    void onTabDeactivated();

    void saveSettings();
    void loadSettings();

private slots:
    void onEnableToggled(bool checked);
    void onApply();
    void onRefreshClients();
    void onShowQrToggled(bool checked);
    void onPreviewEnable(bool checked);
    void onFpsChanged(int val);
    void onQualityChanged(int val);
    void onSizeChanged(int index);
    void onCaptureTick();   // fired by m_captureTimer, does the GPU work

private:
    void buildUi();
    void updateStatus();
    void pushQrToObs(bool show);
    void stopPreview();
    QString settingsPath() const;

    // WebSocket controls
    QCheckBox   *m_enableCheck;
    QSpinBox    *m_portSpin;
    QLabel      *m_statusLbl;
    QLabel      *m_urlLbl;
    QLabel      *m_qrLbl;
    QCheckBox   *m_showQrCheck;
    QListWidget *m_clientList;
    QPushButton *m_applyBtn;
    QLineEdit   *m_pinEdit;
    QTimer      *m_refreshTimer;
    QTimer      *m_captureTimer  = nullptr;
    bool         m_tabActive = false;

    // Preview stream controls
    QCheckBox   *m_previewEnable  = nullptr;
    QSlider     *m_fpsSlider      = nullptr;
    QLabel      *m_fpsLbl         = nullptr;
    QSlider     *m_qualitySlider  = nullptr;
    QLabel      *m_qualityLbl     = nullptr;
    QComboBox   *m_sizeCombo      = nullptr;
    QLabel      *m_previewStatus  = nullptr;

    // GPU resources — only touched while holding graphics mutex
    void *m_texrender    = nullptr;
    void *m_stageSurface = nullptr;
    bool  m_staged       = false;

    int m_captureW   = 1280;
    int m_captureH   = 720;
    int m_captureQ   = 75;
};
