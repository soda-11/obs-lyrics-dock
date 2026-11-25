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

// What a Presentation segment remembers about its capture window.
// comMode: -1 = auto (COM on when the window is POWERPNT.EXE), 0 = off, 1 = on.
struct PptBinding {
    QString title;
    QString exe;
    QString cls;
    int     comMode = -1;
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
    // type >= 0: show the currently bound item (clears blank state). type < 0: fully stop.
    void    requestShow(int type);
    // Bind a Presentation segment: load its saved window into the shared
    // capture source, set COM on/off for it, reset slide state, and show it.
    void    bindItem(const PptBinding &b);
    // Remember a binding without touching OBS (startup restore).
    void    setBinding(const PptBinding &b);
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
    // Emitted ONLY when the user picks a window in the combo (never for programmatic
    // selection), so the dock can store it on the bound item.
    void windowChosen(const QString &title, const QString &exe, const QString &cls);
    // Emitted when the user toggles the COM button. mode: 1 = on, 0 = off.
    void comModeChanged(int mode);

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
    // Re-enumerate windows into m_windows/combo without emitting signals.
    // keepSelection=true re-selects the previously selected HWND if still present.
    void rebuildWindowList(bool keepSelection);
    int  autoDetectIndex() const;                 // fresh detection, index into m_windows or -1
    int  findWindowFor(const PptBinding &b) const; // saved window -> index into m_windows or -1
    bool applyBinding();                           // resolve + select + COM + capture + show
    bool connectCom();
    void disconnectCom();
    QString selectedTitle() const;
    bool    selectedIsPpt() const;
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
    bool    m_comWanted    = false; // bound item wants COM; poll retries until PowerPoint is reachable
    PptBinding m_binding;           // window/COM settings of the bound segment
    bool    m_hasBinding   = false;
    bool    m_unresolved   = false; // bound window not found; source blank, poll retries
    int     m_programmatic = 0;     // >0 while selecting in code; combo handler must not save
    int     m_curSlide     = 0;
    int     m_totSlides    = 0;
    QString m_presTitle;
    int     m_pollCount    = 0;
};
