#pragma once
// scripture-tab.hpp  v1.1

#include "media-gallery.hpp"
#include "color-preset-button.hpp"

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QFrame>
#include <QMouseEvent>

// ── Scripture slide button ────────────────────────────────────
class ScriptureSlideBtn : public QFrame {
    Q_OBJECT
public:
    ScriptureSlideBtn(int slideIdx, const QString &slideText,
                      QWidget *parent = nullptr);
    void    setActive(bool active);
    int     slideIndex() const { return m_slideIdx; }
    QString slideText()  const { return m_slideText; }
signals:
    void clicked(int slideIndex);
protected:
    void mousePressEvent(QMouseEvent *e) override;
private:
    QLabel  *m_numLbl;
    QLabel  *m_textLbl;
    int      m_slideIdx;
    QString  m_slideText;
    bool     m_active = false;
public:
    static const int BASE_W = 200;
    static const int BASE_H = 120;
};

// ── Scripture Tab ─────────────────────────────────────────────
class ScriptureTab : public QWidget {
    Q_OBJECT
public:
    explicit ScriptureTab(QWidget *parent = nullptr);
    void saveState();
    void loadState();
    // Exposes the background gallery so LyricsDock can wire the combined media list.
    MediaGallery *gallery() const { return m_gallery; }

public slots:
    void stopPlayback();
    void clearActiveCard();
    // Called by LyricsDock when WebSocket receives applyScripture command.
    // Updates the text fields, rebuilds slides, does NOT re-broadcast.
    void applyFromRemote(const QString &title, const QString &text);

signals:
    // Emitted when gallery background activates.
    void playbackStarted();
    // Emitted when Apply is clicked — carries full raw text for remote rebuild
    void scriptureStateChanged(const QString &title, const QString &text);
    // Emitted when a slide is clicked — carries title + slide text to display
    void scriptureSlideActivated(const QString &title, const QString &slideText);

private slots:
    void onApply();
    void onSlideClicked(int slideIndex);
    void onPrevSlide();
    void onNextSlide();
    void onColorPresetApplied(QColor fontColor, QColor outlineColor);

private:
    void buildUi();
    void rebuildSlides();
    void setActiveSlide(int index);
    QList<QString> parseSlides() const;
    QString statePath() const;

    QLineEdit       *m_titleEdit      = nullptr;
    QPlainTextEdit  *m_textEdit       = nullptr;

    // Slides header
    ColorPresetButton *m_colorBtn[3]  = {nullptr, nullptr, nullptr};

    QScrollArea     *m_slideArea      = nullptr;
    QWidget         *m_slideContainer = nullptr;
    QGridLayout     *m_slideGrid      = nullptr;
    QList<ScriptureSlideBtn*> m_slideBtns;

    QPushButton     *m_prevBtn        = nullptr;
    QPushButton     *m_nextBtn        = nullptr;
    QLabel          *m_slideCounter   = nullptr;

    MediaGallery    *m_gallery        = nullptr;

    int              m_activeSlide    = -1;

    // Saved color preset data (same format as lyrics tab)
    QColor  m_presetFont[3]    = { Qt::white, Qt::white, Qt::white };
    QColor  m_presetOutline[3] = { Qt::black, Qt::black, Qt::black };
};
