#pragma once
// ppt-tab.hpp  v3.0
// Presentation tab: single window source with optional PowerPoint COM navigation.
// One source captures any visible window. "PowerPoint source" checkbox enables
// COM automation for slide tracking and GoTo; unchecked = keystroke navigation.
// Selecting a Presentation service order auto-shows the source (no Show button).
// "Blank" button hides the source without leaving presentation mode; clicking the
// service order again always clears the blank state and shows the source.

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <QList>
#include <QString>

#include <windows.h>
#include <ole2.h>
#include <obs.h>

struct WinInfo {
    HWND    hwnd = nullptr;
    QString title;
    QString cls;
    QString exe;
};

class PptTab : public QWidget {
    Q_OBJECT
public:
    explicit PptTab(QWidget *parent = nullptr);
    ~PptTab();

    int     currentSlide() const { return m_curSlide;  }
    int     totalSlides()  const { return m_totSlides; }
    QString presTitle()    const { return m_presTitle; }

    // Returns 0 when active and visible, -1 when inactive or blanked.
    // Kept for backward compatibility with existing LyricsDock call sites.
    int     activeType()   const { return (m_active && !m_blank) ? 0 : -1; }
    bool    isActive()     const { return m_active; }
    bool    isBlanked()    const { return m_blank;  }
    // Returns true when a window is selected in the combo — always true (legacy).
    bool    isTypeEnabled(int /*type*/) const { return m_winCombo && m_winCombo->currentIndex() > 0; }

    void    nextSlide();
    void    prevSlide();
    void    gotoSlide(int n);
    // type >= 0: show (clears blank state). type < 0: fully stop.
    // windowExe: if non-empty, auto-select the first window whose exe matches.
    void    requestShow(int type, const QString &windowExe = {});
    // Release the held obs_source_t* ref immediately — call on SCENE_COLLECTION_CLEANUP
    // so OBS can unload all sources without hitting "Source Cleanup Error".
    void    releaseCapture();
    // Restore active UI state on startup without touching OBS sources.
    void    restoreActiveUi();
    // Remove stale legacy sources (SSO - Google Slides, SSO - Canva, etc.) from the
    // current OBS scene to prevent the shutdown crash from accumulated duplicates.
    void    cleanupLegacySources();

signals:
    // 0 = showing, -1 = stopped or blanked.
    void showSlidesRequested(int type);
    void slideStateChanged(int current, int total, const QString &title);
    // Emitted when user picks a window from the combo. value = window title (unique).
    void windowExeChanged(const QString &title);

private slots:
    void onScanWindows();
    void onConnectPpt();
    void onBlank();
    void onPrev();
    void onNext();
    void onGoto();
    void onPollTimer();

private:
    void buildUi();
    void scanWindows();
    void autoSelectWindow(const QString &preferredExe = {});
    void activatePresentation();
    void deactivatePresentation();
    void updateCaptureSource();
    void releaseCaptureSource();
    void ensureInScene();
    void hideFromScene();
    void updateNavUi();
    void updateBlankBtnStyle();
    void updateConnectBtnStyle();
    void sendKeyToActive(int vkey);
    IDispatch *getPptView() const;
    HWND selectedHwnd() const;

    // Primary source name (matches the name users already have in their OBS scenes).
    static constexpr const char *kSourceName       = "SSO - PowerPoint";
    // Fallback: created when neither primary nor any legacy name exists.
    static constexpr const char *kSourceNameNew    = "SSO - Presentation";

    QComboBox   *m_winCombo      = nullptr;
    QPushButton *m_scanBtn       = nullptr;
    QPushButton *m_pptConnectBtn = nullptr; // Connect/disconnect PowerPoint COM
    QPushButton *m_blankBtn      = nullptr;

    QLabel      *m_navStatusLbl = nullptr;
    QPushButton *m_prevBtn      = nullptr;
    QLabel      *m_slideCtr     = nullptr;
    QPushButton *m_nextBtn      = nullptr;
    QSpinBox    *m_gotoSpin     = nullptr;
    QPushButton *m_gotoBtn      = nullptr;

    QTimer       *m_pollTimer  = nullptr;
    obs_source_t *m_capture    = nullptr;
    QList<WinInfo> m_windows;

    bool    m_active       = false;
    bool    m_blank        = false;
    bool    m_pptConnected = false; // true when COM link to PowerPoint is established
    int     m_curSlide     = 0;
    int     m_totSlides    = 0;
    QString m_presTitle;
    int     m_pollCount    = 0;
};
