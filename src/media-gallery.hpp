#pragma once
// media-gallery.hpp  v9.5.1

#include <QWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QSlider>
#include <QSpinBox>
#include <QFrame>
#include <QTimer>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QList>
#include <QString>
#include <QPixmap>

struct MediaItem { QString path; bool isVideo; };

// ── Thumbnail card ────────────────────────────────────────────
class MediaCard : public QFrame {
    Q_OBJECT
public:
    MediaCard(const MediaItem &item, int index, QWidget *parent=nullptr);
    void setActive(bool active);
    void setIndex(int i){ m_index=i; }
    int  index() const { return m_index; }
    const MediaItem &item() const { return m_item; }
    // 0 = hide badge, 1-6 = show number overlay in top-right of thumbnail.
    void setPositionBadge(int badge);
    // Dynamically resize the card width (used by rebuildGrid for fluid layout).
    void setCardWidth(int w);
signals:
    void clicked(int index);
    void removeRequested(int index);
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
private:
    void buildThumbnail();
    MediaItem  m_item;
    int        m_index;
    bool       m_active  = false;
    bool       m_hovered = false;
    int        m_badge   = 0;
    QLabel    *m_thumb;
    QLabel    *m_nameLbl;
    QLabel    *m_badgeLbl = nullptr;
    QPixmap    m_origThumb;
    QPoint     m_dragStartPos;
    bool       m_dragging = false;
public:
    // ~20% smaller than v9.2 (was 140×110)
    static const int CARD_W = 112;
    static const int CARD_H = 88;
    static const int THUMB_W = 104;   // CARD_W - 8
    static const int THUMB_H = 62;    // CARD_H - 22 (name label)
};

// ── Gallery widget ────────────────────────────────────────────
class MediaGallery : public QWidget {
    Q_OBJECT
public:
    // stateFileName: JSON file name (not full path) used by saveState/loadState.
    // Default "media-gallery.json" is used by the Lyrics tab.
    // Pass a different name (e.g. "scripture-gallery.json") for other tabs.
    explicit MediaGallery(QWidget *parent=nullptr,
                          const QString &stateFileName="media-gallery.json");
    void saveState();
    void loadState();
    // Returns the current list of media file paths in this gallery.
    QStringList itemPaths() const;
    // Generate a thumbnail for any media file (image or video) at the given size.
    // Uses Windows Shell for video frames. Returns null pixmap on failure.
    // crop=true: fill w×h exactly (KeepAspectRatioByExpanding + crop center).
    // crop=false: fit inside w×h preserving aspect ratio (KeepAspectRatio).
    static QPixmap generateThumbnail(const QString &path, int w, int h, bool crop = true);
    // Returns the path of the currently active (selected) item, or empty string if none.
    QString activeItemPath() const;
    int fadeDurationMs() const;
    void setFadeDurationMs(int ms);

signals:
    // Emitted when a card is clicked to play — LyricsDock stops MV tab.
    void mediaActivated();
    // Emitted when a VIDEO card is clicked — LyricsDock stops audio bin.
    void videoActivated();
    // Emitted when Stop is pressed — LyricsDock also stops MV tab UI.
    void stopAllRequested();
    // Emitted whenever the item list changes (add/remove/reorder).
    void mediaListChanged();

public slots:
    void reorderCards(int fromIndex, int toIndex);
    // Full stop: clears OBS source, deselects card, and forgets last selection.
    void stopPlayback();
    // Clears OBS source only — remembers which card was active for replayActive().
    void suspendPlayback();
    // UI-only deselect (no renderer call). Used when MV starts.
    void clearActiveCard();
    // Activate media at 1-based position (used by OBS hotkeys).
    void activateByPosition(int oneBased);
    // Re-push the currently active media to the renderer without changing UI state.
    // No-op when nothing is selected.  Called when switching slide tabs so each
    // tab's last-selected background resumes automatically.
    void replayActive();

private slots:
    void onAddMedia();
    void onClearMedia();
    void onCardClicked(int index);
    void onCardRemoveRequested(int index);
    void onPlayPause();
    void onPollTimer();
    void onMoveUp();
    void onMoveDown();
    void updateUpDownButtons();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void buildUi();
    void rebuildGrid();
    void relayoutCards();
    void setActiveCard(int index);
    void updateVideoControls();
    int  calcCols() const;
    QString galleryStatePath() const;

    QScrollArea  *m_scrollArea;
    QWidget      *m_container;
    QGridLayout  *m_grid;
    QList<MediaCard*> m_cards;

    QWidget     *m_videoControls;
    QToolButton *m_playPauseBtn;
    QSlider     *m_scrubber;
    QLabel      *m_timeLbl;
    QPushButton *m_clearBtn;
    QToolButton *m_upBtn   = nullptr;
    QToolButton *m_downBtn = nullptr;
    int          m_fadeDurationMs = 500;
    QTimer      *m_pollTimer;

    QList<MediaItem> m_items;
    int     m_activeIndex      = -1; // UI-selected card (cleared by clearActiveCard)
    int     m_lastActiveIndex  = -1; // remembered for replayActive (cleared only by stopPlayback)
    int     m_currentCols  = 3;
    bool    m_relayouting  = false;
    bool    m_paused       = false;
    int     m_seekCooldown = 0;
    QString m_stateFileName;
};
