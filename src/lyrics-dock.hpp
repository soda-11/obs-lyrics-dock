#pragma once
// lyrics-dock.hpp  v9.8.0

#include "song-manager.hpp"
#include "media-gallery.hpp"
#include "websocket-server.hpp"
#include "ws-tab.hpp"
#include "mv-tab.hpp"
#include "scripture-tab.hpp"
#include "color-preset-button.hpp"
#include <QWidget>
#include <QTabWidget>
#include <QListWidget>
#include <QScrollArea>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QToolButton>
#include <QSlider>
#include <QSplitter>
#include <QTextEdit>
#include <QDialog>
#include <QColor>
#include <QMouseEvent>
#include <QSpinBox>
#include <QFontComboBox>

// ── Song Library popup ────────────────────────────────────────
class SongLibraryPopup : public QDialog {
    Q_OBJECT
public:
    explicit SongLibraryPopup(QWidget *parent = nullptr);
    void           setSequenceIds(const QList<QString> &ids);
    QList<QString> addedIds()            const { return m_addedIds; }
    QList<QString> removedIds()          const { return m_removedIds; }
    QList<QString> deletedFromLibrary()  const { return m_deletedIds; }
signals:
    void newSongAdded();
private slots:
    void onSearch(const QString &text);
    void onSelectionChanged();
    void onAdd();
    void onRemove();
    void onDeleteSong();
    void onAddNewSong();
private:
    void refreshList();
    QLineEdit      *m_searchEdit;
    QListWidget    *m_list;
    QTextEdit      *m_lyricsPreview;
    QPushButton    *m_addBtn, *m_removeBtn, *m_deleteBtn;
    QList<QString>  m_addedIds;
    QList<QString>  m_removedIds;
    QList<QString>  m_deletedIds;        // permanently deleted from library
    QList<QString>  m_sequenceIds;
};

// ── Slide card ────────────────────────────────────────────────
class SlideButton : public QFrame {
    Q_OBJECT
public:
    SlideButton(const QString &section, const QString &text,
                int slideIndex, int sectionIndex, int slideNumber,
                QWidget *parent = nullptr);
    void setActive(bool active);
    void applyScale(double scale, const QString &fontFamily = "Arial", int fixedWidth = -1);
    int  slideIndex()   const { return m_slideIdx; }
    int  sectionIndex() const { return m_sectionIdx; }
signals:
    void clicked(int slideIndex);
    void editRequested(int sectionIndex);
protected:
    void mousePressEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
private:
    QLabel  *m_sectionLbl, *m_textLbl, *m_slideNumLbl;
    int      m_slideIdx, m_sectionIdx;
    bool     m_active = false;
    static const int BASE_W = 200;
    static const int BASE_H = 120;
};

// ── Main dock ─────────────────────────────────────────────────
class LyricsDock : public QWidget {
    Q_OBJECT
public:
    explicit LyricsDock(QWidget *parent = nullptr);
    void saveState();
    void loadState();
    void setWsServer(WebSocketServer *srv) { m_wsServer = srv; }
    QJsonArray buildSequenceJson() const;
    void       broadcastSequence();
    // Called by WebSocket server when a remote client pushes a slide.
    // Finds the song in the sequence, selects it, highlights the tile.
    void pushSlideFromRemote(const QString &songTitle,
                             int slideIndex,
                             const QString &lyrics,
                             const QString &section);
    // Called by WebSocket server to update the Scripture tab from a remote client.
    void applyScriptureFromRemote(const QString &title, const QString &text);

protected:
    void resizeEvent(QResizeEvent *) override;

private slots:
    void onAddToSequence();
    void onRemoveFromSequence();
    void onMoveSequenceUp();
    void onMoveSequenceDown();
    void onSequenceSongSelected(int row);
    void onSequenceEditSong();
    void onSlideClicked(int slideIndex);
    void onSlideEditRequested(int sectionIndex);
    void onBlackoutClicked();
    void onNextSlide();
    void onPrevSlide();
    void onZoomIn();
    void onZoomOut();
    void onZoomSlider(int value);
    void onColorPresetApplied(QColor fontColor, QColor outlineColor);
    void onSequenceContextMenu(const QPoint &pos);

private:
    void buildUi();
    void loadSongSlides(const Song &song);
    void clearSlideGrid();
    void setActiveSlide(int index);
    void pushCurrentSlide();
    void updateSlideCounter();
    void applyZoomToAll();
    int     computeGlobalSlideOffset(int forRow) const;
    QString sequenceCurrentSongId() const;
    void    updateSequenceItem(int row, const Song &song);
    void    removeSequenceRow(int row);

    QPushButton    *m_addToSeqBtn;
    QListWidget    *m_sequenceList;
    QPushButton    *m_removeFromSeqBtn, *m_seqUpBtn,
                   *m_seqDownBtn,       *m_seqEditBtn;
    QList<QString>  m_sequenceSongIds;

    QScrollArea    *m_slideArea;
    QWidget        *m_slideContainer;
    QGridLayout    *m_slideGrid;
    QList<SlideButton*> m_slideButtons;

    QPushButton    *m_prevBtn, *m_nextBtn, *m_blackoutBtn;
    ColorPresetButton *m_colorBtn[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    QSpinBox       *m_linesPerSlideSpin = nullptr;
    QLabel         *m_slideCounter;
    QToolButton    *m_zoomInBtn, *m_zoomOutBtn;
    QSlider        *m_zoomSlider;
    QLabel         *m_zoomLabel;
    QFontComboBox  *m_slideFontCombo   = nullptr;
    QString         m_slideFontFamily  = "Arial";
    QSlider        *m_cardWidthSlider  = nullptr;
    QLabel         *m_cardWidthLabel   = nullptr;
    QToolButton    *m_cardWidthOutBtn  = nullptr;
    QToolButton    *m_cardWidthInBtn   = nullptr;
    int             m_cardWidthScale   = 30;      // 10/20/…/100 % of viewport width

    MediaGallery     *m_mediaGallery   = nullptr;
    WebSocketServer  *m_wsServer       = nullptr;
    WsTab            *m_wsTab          = nullptr;
    MvTab            *m_mvTab          = nullptr;
    ScriptureTab     *m_scriptureTab   = nullptr;
    Song   m_currentSong;
    int    m_activeSlide           = -1;
    int    m_currentSongSlideOffset = 0; // sum of slide counts of all songs before current
    double m_slideScale  = 1.0;
    QList<LyricsSlide> m_slides;         // one entry per slide tile (English or Chinese)
    QList<int>         m_slideSectionIndices;
};
