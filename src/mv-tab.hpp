#pragma once
// mv-tab.hpp  v1.0
//
// MV (Music & Video) tab — two panels:
//   Left:  MvVideoPanel  — thumbnail grid, fade transition, sequential play
//   Right: MvMusicPanel  — title-only playlist, auto-advance on end

#include "media-gallery.hpp"   // reuse MediaCard + MediaItem

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QSpinBox>
#include <QScrollArea>
#include <QGridLayout>
#include <QTimer>

// ── Video Panel ───────────────────────────────────────────────
// Shows video thumbnails, supports fade transition, optional
// sequential auto-play. When a video ends without sequential on,
// screen goes blank. Audio routes to OBS via existing mediaAudioCb.
class MvVideoPanel : public QWidget {
    Q_OBJECT
public:
    explicit MvVideoPanel(QWidget *parent = nullptr);
    void saveState();
    void loadState();

public slots:
    // Stop current playback and deselect active card (no fade).
    // Called by MvTab when music panel starts, or by LyricsDock
    // when Lyrics tab takes over.
    void stopPlayback();

signals:
    // Emitted when a video starts playing (used by MvTab to stop music).
    void videoStarted();

private slots:
    void onAddVideo();
    void onCardClicked(int index);
    void onCardRemoveRequested(int index);
    void onClearVideo();
    void onSequentialToggle(bool checked);
    void onPlayPause();
    void onPollTimer();

private:
    void buildUi();
    void rebuildGrid();
    void setActiveCard(int index);
    void updateVideoControls();
    void playVideoAt(int index);
    int  calcCols() const;
    QString statePath() const;
    bool eventFilter(QObject *obj, QEvent *event) override;

    QList<MediaItem>    m_items;
    QList<MediaCard*>   m_cards;
    QScrollArea        *m_scrollArea    = nullptr;
    QWidget            *m_container     = nullptr;
    QGridLayout        *m_grid          = nullptr;
    int                 m_activeIndex   = -1;
    int                 m_currentCols   = 3;

    bool                m_sequential    = false;
    bool                m_paused        = false;
    bool                m_endHandled    = false; // guard one-shot on ENDED
    int                 m_seekCooldown  = 0;     // cycles to skip end-detection after seek

    QPushButton        *m_clearBtn      = nullptr;
    QPushButton        *m_seqBtn        = nullptr;
    QWidget            *m_videoControls = nullptr;
    QToolButton        *m_playPauseBtn  = nullptr;
    QSlider            *m_scrubber      = nullptr;
    QLabel             *m_timeLbl       = nullptr;
    QSpinBox           *m_fadeSpin      = nullptr;
    QTimer             *m_pollTimer     = nullptr;
};

// ── Music Panel ───────────────────────────────────────────────
// Title-only playlist. Plays audio through OBS (ffmpeg_source →
// mediaAudioCb → obs_source_output_audio). No image rendered.
// Auto-advances to next track when current track ends.
class MvMusicPanel : public QWidget {
    Q_OBJECT
public:
    explicit MvMusicPanel(QWidget *parent = nullptr);
    void saveState();
    void loadState();

public slots:
    void stopPlayback();

signals:
    void musicStarted();

private slots:
    void onAddMusic();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onRemoveMusic();
    void onStopMusic();
    void onPlayPause();
    void onPollTimer();

private:
    void playMusicAt(int index);
    void setActiveRow(int index);
    void updateButtons();
    QString statePath() const;

    QStringList   m_items;
    int           m_activeIndex   = -1;
    bool          m_endHandled    = false;
    bool          m_musicPaused   = false;
    int           m_seekCooldown  = 0;
    int64_t       m_trackDuration = 0;  // cached once known; 0 = not yet loaded

    QListWidget  *m_list          = nullptr;
    QPushButton  *m_removeBtn     = nullptr;
    QPushButton  *m_stopBtn       = nullptr;
    QToolButton  *m_playPauseBtn  = nullptr;
    QLabel       *m_nowPlayingLbl = nullptr;
    QWidget      *m_seekBar       = nullptr;
    QSlider      *m_musicScrubber = nullptr;
    QLabel       *m_musicTimeLbl  = nullptr;
    QTimer       *m_pollTimer     = nullptr;
};

// ── MV Tab ────────────────────────────────────────────────────
// Container widget placed in LyricsDock's QTabWidget "MV" tab.
class MvTab : public QWidget {
    Q_OBJECT
public:
    explicit MvTab(QWidget *parent = nullptr);
    void saveState();
    void loadState();

public slots:
    // Stops both panels and clears the OBS media source.
    void stopPlayback();

signals:
    // Emitted whenever video or music starts. LyricsDock connects this
    // to stop the Lyrics-tab MediaGallery and clear lyrics text.
    void playbackStarted();

private:
    MvVideoPanel *m_videoPanel = nullptr;
    MvMusicPanel *m_musicPanel = nullptr;
};
