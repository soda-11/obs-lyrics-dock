#pragma once
// overlay-tab.hpp  v1.3
// Floating touch overlay (mode bar + Prev/Next) and its settings tab.

#include "preview-window.hpp"

#include <QWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>

// ── Floating overlay window ───────────────────────────────────
// Frameless, always-on-top, translucent.
// Row 1: content mode buttons [CTW][SCR][LYR] — 30 % of nav button size.
// Row 2: presentation source buttons [📊 PPT][🌐 Google][🎨 Canva].
// Row 3: two large square arrow buttons ◀ ▶.
// All geometry is explicit (no layout manager) so resizing is pixel-exact.
// All colours are derived from a single hue value (0–359).
class PptOverlayWindow : public QWidget {
    Q_OBJECT
public:
    explicit PptOverlayWindow(QWidget *parent = nullptr);

    void    setOverlaySize(int totalWidthPx);
    void    setOverlayOpacity(int percent);      // 20–100
    void    setActiveMode(const QString &mode);  // "paragraph"|"scr"|"lyr"
    void    setPresActive(int type);             // 0/1/2=active pres, -1=none
    void    setPresEnabled(int type, bool on);   // enable/disable a pres button
    void    setHue(int hue);                     // 0–359
    QString activeMode() const { return m_activeMode; }

signals:
    void prevRequested(const QString &mode);
    void nextRequested(const QString &mode);
    void modeRequested(const QString &mode);
    void presRequested(int type);  // 0=PPT 1=Google 2=Canva

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *) override;

private:
    void rebuildGeometry();
    void applyNavBtnStyle(int navW, int navH);
    void refreshModeBtnStyles();
    void refreshAllStyles();

    // Colour helpers derived from m_hue
    QColor colBg()                const;
    QColor colBgBorder()          const;
    QColor colNavBg()             const;
    QColor colNavBgPressed()      const;
    QColor colNavAccent()         const;
    QColor colModeActive()        const;
    QColor colModeActivePressed() const;
    QColor colModeInactive()      const;
    QColor colModeInactiveHover() const;
    QColor colModeInactiveTxt()   const;

    static constexpr int   kModes    = 3;  // CTW SCR LYR
    static constexpr int   kPresTypes = 3; // PPT Google Canva
    static const char     *kModeKeys[kModes];
    static const char     *kModeLabels[kModes];
    static const char     *kPresLabels[kPresTypes];

    void refreshPresBtnStyles();

    QPushButton *m_modeBtns[kModes]     = {};
    QPushButton *m_presBtns[kPresTypes] = {};
    QPushButton *m_prevBtn              = nullptr;
    QPushButton *m_nextBtn              = nullptr;
    QPoint       m_dragPos;
    int          m_totalWidth           = 240;
    int          m_hue                  = 215;
    QString      m_activeMode           = "lyr";
    int          m_activePresType       = -1;
};

// ── Overlay settings tab ──────────────────────────────────────
// Lives as a dock tab; owns the PptOverlayWindow instance.
// Scene-switching: each mode maps to a user-selected OBS scene.
class OverlayTab : public QWidget {
    Q_OBJECT
public:
    explicit OverlayTab(QWidget *parent = nullptr);
    ~OverlayTab();

    // Called by LyricsDock when any tab pushes a slide (auto-follow).
    void setActiveMode(const QString &mode);
    // Called by LyricsDock to reflect active/inactive presentation source.
    void setPresActive(int type);
    // Called by LyricsDock when a PptTab section is enabled/disabled.
    void setPresEnabled(int type, bool on);

signals:
    void prevRequested(const QString &mode);
    void nextRequested(const QString &mode);
    void modeRequested(const QString &mode);
    void presRequested(int type);  // 0=PPT 1=Google 2=Canva

private slots:
    void onToggle(bool checked);
    void onSizeChanged(int val);
    void onOpacityChanged(int val);
    void onHueChanged(int val);
    void onRefreshScenes();
    void onScenesReady();
    void onPreviewOpen(int idx);
    void onPreviewTopChanged(int idx, bool top);
    // Intercepts PptOverlayWindow::modeRequested, switches OBS scene, then re-emits.
    void onModeRequestedInternal(const QString &mode);

private:
    void buildUi();
    void updateHueSwatch();
    void populateSceneCombos();
    void populateMonitorCombos();
    void switchToSceneForMode(const QString &mode);
    int  modeIndex(const QString &mode) const; // "ppt"→0, "paragraph"→1, "scr"→2, "lyr"→3
    void saveSettings();
    void loadSettings();

    PptOverlayWindow *m_overlay          = nullptr;
    QPushButton      *m_toggleBtn        = nullptr;
    QSlider          *m_sizeSlider       = nullptr;
    QLabel           *m_sizeLbl          = nullptr;
    QSlider          *m_opacitySlider    = nullptr;
    QLabel           *m_opacityLbl       = nullptr;
    QSlider          *m_hueSlider        = nullptr;
    QLabel           *m_hueSwatch        = nullptr;
    // Scene combos — indexed same as kModeKeys: 0=ctw 1=scr 2=lyr
    QComboBox        *m_sceneCombo[3]    = {};
    QPushButton      *m_refreshScenesBtn = nullptr;

    // Preview output windows (2 independent fullscreen displays)
    static constexpr int kPreviews = 2;
    PreviewWindow    *m_previewWin[kPreviews]       = {};
    QComboBox        *m_previewMonitor[kPreviews]   = {};
    QPushButton      *m_previewOpenBtn[kPreviews]   = {};
    QCheckBox        *m_previewTopChk[kPreviews]    = {};
};
