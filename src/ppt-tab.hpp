#pragma once
// ppt-tab.hpp  v2.0
// Presentation tab: PowerPoint (COM nav), Google Slides, Canva (keystroke nav).
// Three independent sections — each has a manual window selector, an enable
// checkbox (via QGroupBox::setCheckable), and a "Show Slides" toggle button.
// Only one section can be active at a time.  Navigation (Prev/Next/Goto) is
// universal and targets whichever section is currently showing.

#include <QWidget>
#include <QGroupBox>
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

// One visible top-level window entry, captured during a scan.
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

    // Remote-control helpers (called by LyricsDock on WebSocket commands).
    int     currentSlide() const { return m_curSlide;  }
    int     totalSlides()  const { return m_totSlides; }
    QString presTitle()    const { return m_presTitle; }
    int     activeType()   const { return m_activeType; }
    void    nextSlide();
    void    prevSlide();
    void    gotoSlide(int n);
    // Called by remote: type 0/1/2 to show that source, -1 to stop.
    void    requestShow(int type);
    // Returns true if the given type's group box is checked (enabled by user).
    bool    isTypeEnabled(int type) const;

signals:
    // Emitted when the active presentation type changes.
    // type: 0=PowerPoint  1=GoogleSlides  2=Canva  -1=none (deactivated).
    void showSlidesRequested(int type);

    // Emitted when a group box is checked/unchecked (user enables/disables a source).
    void typeEnabledChanged(int type, bool enabled);
    // Emitted on each poll tick when PowerPoint is the active type and a
    // slideshow is running.  Triggers broadcastPptState in LyricsDock.
    void slideStateChanged(int current, int total, const QString &title);

private slots:
    void onScanWindows(int type);
    void onShowSlides(int type);
    void onPrev();
    void onNext();
    void onGoto();
    void onPollTimer();

private:
    static constexpr int kTypes = 3; // 0=PPT  1=GSlides  2=Canva

    struct PresSection {
        QGroupBox   *group   = nullptr;  // setCheckable(true) acts as enable toggle
        QComboBox   *winCombo = nullptr;
        QPushButton *scanBtn  = nullptr;
        QPushButton *showBtn  = nullptr;
        obs_source_t*capture  = nullptr;
        QList<WinInfo> windows;
    };

    void buildUi();
    void scanWindows(int type);
    void autoSelectWindow(int type); // auto-pick best matching window after a scan
    void activatePresentation(int type);
    void deactivatePresentation();
    void updateCaptureSource(int type);
    void releaseCaptureSource(int type);
    void ensureInScene(int type);   // add capture source to current OBS scene
    void hideFromScene(int type);   // hide capture source in current OBS scene
    void updateNavUi();
    void updateShowBtnStyles();
    void sendKeyToActive(int vkey);

    // COM: returns the active slideshow View IDispatch* for the first
    // PowerPoint presentation currently running a show.  Caller must Release().
    IDispatch *getPptView() const;

    HWND selectedHwnd(int type) const;

    PresSection m_sections[kTypes];
    int         m_activeType = -1; // -1=none, 0=PPT, 1=GSlides, 2=Canva

    // Shared navigation UI
    QLabel      *m_navStatusLbl = nullptr;
    QPushButton *m_prevBtn      = nullptr;
    QLabel      *m_slideCtr     = nullptr;
    QPushButton *m_nextBtn      = nullptr;
    QSpinBox    *m_gotoSpin     = nullptr;
    QPushButton *m_gotoBtn      = nullptr;

    QTimer      *m_pollTimer = nullptr;
    int          m_curSlide  = 0;
    int          m_totSlides = 0;
    QString      m_presTitle;
    int          m_pollCount = 0;
};
