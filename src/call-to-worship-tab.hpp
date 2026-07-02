#pragma once
// call-to-worship-tab.hpp  v1.0

#include "scripture-tab.hpp"   // reuse ScriptureSlideBtn

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>

// ── Call to Worship Tab ───────────────────────────────────────
// Identical feature set to ScriptureTab but with independent
// state persistence and positioned before the Scripture tab.
// Both tabs share the same OBS text sources (srcScripTitle /
// srcScripText) — only one displays at a time.
class CallToWorshipTab : public QWidget {
    Q_OBJECT
public:
    explicit CallToWorshipTab(QWidget *parent = nullptr);
    void saveState();
    void loadState();
    MediaGallery *gallery() const { return m_gallery; }

public slots:
    void stopPlayback();
    void clearActiveCard();

signals:
    void playbackStarted();
    void stateChanged(const QString &title, const QString &text);
    void slideActivated(const QString &title, const QString &slideText);

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

    QColor  m_presetFont[3]    = { Qt::white, Qt::white, Qt::white };
    QColor  m_presetOutline[3] = { Qt::black, Qt::black, Qt::black };
};
