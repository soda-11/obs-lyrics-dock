#pragma once
// mv-tab.hpp  v2.0
//
// Video tab  — MvVideoPanel (full-width, unchanged from v1.0)
// Music tab  — MusicPicturePanel (background pictures + slideshow)
//               + MvMusicPanel (audio playlist, unchanged from v1.0)

#include "media-gallery.hpp"   // reuse MediaCard + MediaItem

#include <QWidget>
#include <QFrame>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QSpinBox>
#include <QScrollArea>
#include <QGridLayout>
#include <QCheckBox>
#include <QTimer>

// ══════════════════════════════════════════════════════════════
// MvVideoPanel  (unchanged from v1.0)
// ══════════════════════════════════════════════════════════════
class MvVideoPanel : public QWidget {
    Q_OBJECT
public:
    explicit MvVideoPanel(QWidget *parent = nullptr);
    void saveState();
    void loadState();

public slots:
    void stopPlayback();

signals:
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
    bool                m_endHandled    = false;
    int                 m_seekCooldown  = 0;

    QPushButton        *m_clearBtn      = nullptr;
    QPushButton        *m_seqBtn        = nullptr;
    QWidget            *m_videoControls = nullptr;
    QToolButton        *m_playPauseBtn  = nullptr;
    QSlider            *m_scrubber      = nullptr;
    QLabel             *m_timeLbl       = nullptr;
    QSpinBox           *m_fadeSpin      = nullptr;
    QTimer             *m_pollTimer     = nullptr;
};

// ══════════════════════════════════════════════════════════════
// MvMusicPanel
// ══════════════════════════════════════════════════════════════
class MvMusicPanel : public QWidget {
    Q_OBJECT
public:
    enum class LoopMode { None, All, One };

    explicit MvMusicPanel(QWidget *parent = nullptr);
    void saveState();
    void loadState();

public slots:
    void stopPlayback();

signals:
    void musicStarted();
    // Emitted when music is explicitly stopped or the playlist ends with no loop.
    // MusicTab uses this to also stop/blank the picture slideshow.
    void stopped();

private slots:
    void onAddMusic();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onRemoveMusic();
    void onStopMusic();
    void onPlayPause();
    void onPollTimer();
    void onMoveUp();
    void onMoveDown();
    void onLoopToggle();

private:
    void playMusicAt(int index);
    void setActiveRow(int index);
    void updateButtons();
    void applyLoopButtonStyle();
    QString statePath() const;

    QStringList   m_items;
    int           m_activeIndex   = -1;
    bool          m_endHandled    = false;
    bool          m_musicPaused   = false;
    int           m_seekCooldown  = 0;
    int64_t       m_trackDuration = 0;
    LoopMode      m_loopMode      = LoopMode::None;

    QListWidget  *m_list          = nullptr;
    QPushButton  *m_removeBtn     = nullptr;
    QPushButton  *m_stopBtn       = nullptr;
    QPushButton  *m_loopBtn       = nullptr;
    QToolButton  *m_upBtn         = nullptr;
    QToolButton  *m_downBtn       = nullptr;
    QToolButton  *m_playPauseBtn  = nullptr;
    QLabel       *m_nowPlayingLbl = nullptr;
    QWidget      *m_seekBar       = nullptr;
    QSlider      *m_musicScrubber = nullptr;
    QLabel       *m_musicTimeLbl  = nullptr;
    QTimer       *m_pollTimer     = nullptr;
};

// ══════════════════════════════════════════════════════════════
// PictureCard  — thumbnail card with checkbox for Music tab
// ══════════════════════════════════════════════════════════════
class PictureCard : public QFrame {
    Q_OBJECT
public:
    PictureCard(const QString &path, int index, QWidget *parent = nullptr);
    void setActive(bool active);
    void setIndex(int i) { m_index = i; }
    int  index() const { return m_index; }
    bool isChecked() const;
    void setChecked(bool checked);
    const QString &path() const { return m_path; }

    static const int CARD_W  = 112;
    static const int CARD_H  = 106;
    static const int THUMB_W = 100;
    static const int THUMB_H = 62;

signals:
    void clicked(int index);
    void checkToggled(int index, bool checked);
    void removeRequested(int index);

protected:
    void mousePressEvent(QMouseEvent *e) override;

private:
    void buildThumbnail();
    QString     m_path;
    int         m_index;
    bool        m_active = false;
    QCheckBox  *m_check  = nullptr;
    QLabel     *m_thumb  = nullptr;
    QLabel     *m_nameLbl = nullptr;
};

// ══════════════════════════════════════════════════════════════
// MusicPicturePanel  — background pictures panel for Music tab
// ══════════════════════════════════════════════════════════════
class MusicPicturePanel : public QWidget {
    Q_OBJECT
public:
    explicit MusicPicturePanel(QWidget *parent = nullptr);
    void saveState();
    void loadState();

public slots:
    // Stop slideshow timer and clear active card highlight (no OBS call).
    void stopSlideshow();
    // Stop slideshow + call lyrics_renderer_media_clear().
    void clearDisplay();
    // UI-only deselect (called when Lyrics-tab background takes over).
    void clearActiveCard();

signals:
    // Emitted when any picture is pushed to OBS.
    void pictureActivated();

private slots:
    void onAddPictures();
    void onCardClicked(int index);
    void onCheckToggled(int index, bool checked);
    void onCardRemoveRequested(int index);
    void onBlank();
    void onSlideshowTimer();

private:
    void buildUi();
    void rebuildGrid();
    void setActiveCard(int index);
    void pushPicture(int index);
    void updateSlideshowState();
    int  calcCols() const;
    bool eventFilter(QObject *obj, QEvent *event) override;
    QString statePath() const;

    QList<QString>       m_paths;
    QList<PictureCard*>  m_cards;
    QScrollArea         *m_scrollArea     = nullptr;
    QWidget             *m_container      = nullptr;
    QGridLayout         *m_grid           = nullptr;
    int                  m_activeIndex    = -1;
    int                  m_currentCols    = 3;

    QTimer              *m_slideshowTimer = nullptr;
    int                  m_slideshowPos   = 0;  // index within checked-cards list
    QSpinBox            *m_intervalSpin   = nullptr;
    QPushButton         *m_blankBtn       = nullptr;
    QPushButton         *m_slideshowBtn   = nullptr;
};

// ══════════════════════════════════════════════════════════════
// VideoTab  — full-width video panel (replaces left half of old MvTab)
// ══════════════════════════════════════════════════════════════
class VideoTab : public QWidget {
    Q_OBJECT
public:
    explicit VideoTab(QWidget *parent = nullptr);
    void saveState() { if (m_videoPanel) m_videoPanel->saveState(); }
    void loadState()  { if (m_videoPanel) m_videoPanel->loadState(); }

public slots:
    void stopPlayback();

signals:
    // Emitted when a video starts — LyricsDock uses this to stop Music tab.
    void playbackStarted();

private:
    MvVideoPanel *m_videoPanel = nullptr;
};

// ══════════════════════════════════════════════════════════════
// MusicTab  — picture panel (left) + music panel (right)
// ══════════════════════════════════════════════════════════════
class MusicTab : public QWidget {
    Q_OBJECT
public:
    explicit MusicTab(QWidget *parent = nullptr);
    void saveState();
    void loadState();

public slots:
    // Full stop: stops music, stops slideshow, clears OBS picture layer.
    void stopPlayback();
    // UI-only: stop highlights without clearing OBS (used when Video tab takes over).
    void clearActiveUI();

signals:
    // Emitted when music or a picture is activated.
    void playbackStarted();

private:
    MusicPicturePanel *m_picturePanel = nullptr;
    MvMusicPanel      *m_musicPanel   = nullptr;
};
