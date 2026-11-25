#pragma once
// lyrics-dock.hpp  v12.0
class LayoutDialog;

#include "song-manager.hpp"
#include "media-gallery.hpp"
#include "audio-bin.hpp"
#include "websocket-server.hpp"
#include "ws-tab.hpp"
#include "mv-tab.hpp"
#include "ppt-tab.hpp"
#include "color-preset-button.hpp"
#include <QWidget>
#include <QStackedWidget>
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
#include <QPlainTextEdit>
#include <QDialog>
#include <QColor>
#include <QMap>
#include <QMouseEvent>
#include <QSpinBox>
#include <QFontComboBox>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

// ── SlideCard: the permanent identity and own data of one slide card ──
// The card's TEXT is derived from its segment (sections / text).  This holds what has to
// survive text edits and reordering.
struct SlideCard {
    QString id;          // service-wide uuid, never reused
    QString key;         // fingerprint of the card's content at the last sync
    QString background;  // own background; blank = inherit Segment::defaultBackground
};

// ── Segment: one entry in the service (see "Terminology" in CLAUDE.md) ──
// Service → Segment[] → slide cards.  "Section" means a song's Verse/Chorus.
struct Segment {
    enum class Type { Lyrics, Paragraphs, Media, Presentation };
    Type    type    = Type::Lyrics;
    QString id;           // unique segment id (uuid) for every segment type
    QString title;        // Required for Paragraphs / Media / Presentation
    QString artist;       // Lyrics only
    // Lyrics: song details copied from the library when the song was added, so "Save to
    // Library" can write the whole song back (the library has no backgrounds).
    QString songKey, songTempo, songCopyright;
    // Lyrics: id of the library song this segment was copied from.  The segment is
    // independent of it; the id is only used to save back to the library.  The library
    // song may no longer exist.
    QString sourceSongId;
    QList<LyricsSection> sections; // embedded song data for portability
    QString text;         // Paragraphs: body text (blank lines = slide breaks)
    QString filePath;     // Media / Presentation (legacy single-file)
    // Presentation: the capture window this item remembers (see CHANGELOG v12.25.0).
    QString windowTitle;  // exact window title when chosen
    QString windowProc;   // owning exe, e.g. "POWERPNT.EXE"
    QString windowCls;    // window class (tells slide show / presenter view / editor apart)
    int     pptComMode = -1; // PowerPoint COM: -1 auto (on for POWERPNT.EXE), 0 off, 1 on
    QStringList mediaPaths; // Media: list of media file paths
    // ── Slide cards (Lyrics & Paragraphs) ──
    // `cards` is the segment's card queue in on-air order.  Slide text is still derived from
    // `sections` / `text`; call syncCards() after ANY change to them so every card keeps its
    // id and background.  A card's background is its own when set, otherwise the segment's
    // defaultBackground.  Blank everywhere = no background.
    QList<SlideCard> cards;
    QString defaultBackground;
    // Paragraphs: this segment's OWN complete layout (same keys as the OBS source settings).
    // It is copied from the default layout when the segment is created, so changing the default
    // or another segment never changes this one.
    QJsonObject paragraphLayout;
    QByteArray layoutBytes() const {
        return paragraphLayout.isEmpty() ? QByteArray()
                                        : QJsonDocument(paragraphLayout).toJson(QJsonDocument::Compact);
    }
    QString backgroundFor(int slideIdx) const {
        if (slideIdx >= 0 && slideIdx < cards.size() && !cards[slideIdx].background.isEmpty())
            return cards[slideIdx].background;
        return defaultBackground;
    }
    QString cardIdAt(int slideIdx) const {
        return (slideIdx >= 0 && slideIdx < cards.size()) ? cards[slideIdx].id : QString();
    }
    int cardIndexById(const QString &cardId) const {
        for (int i = 0; i < cards.size(); ++i) if (cards[i].id == cardId) return i;
        return -1;
    }
    void setCardBackground(int slideIdx, const QString &path) {
        if (slideIdx >= 0 && slideIdx < cards.size()) cards[slideIdx].background = path;
    }
    void clearBackgrounds() {
        defaultBackground.clear();
        for (auto &c : cards) c.background.clear();
        legacySlideBgs.clear();
    }
    void syncCards();
    // Give the segment and all its cards new ids (used when a file is imported).
    void remintIds();
    // Set only while loading an old file: backgrounds keyed by slide index (before card ids).
    QMap<int,QString> legacySlideBgs;
    // Set while loading when the stored form was old (ids had to be minted): save afterwards.
    bool needsSave = false;
    int colorPresetIdx = -1;     // which color preset button (0-based) was last applied; -1 = none
    bool altSentenceColor  = false; // paragraph: alternate even-line color
    int  altSentencePreset = -1;    // paragraph: 0-based index into m_colorBtn for even lines
    QStringList parseSlides() const;

    QJsonObject toJson()                       const;
    static Segment fromJson(const QJsonObject &obj);
    QString typeLabel()                        const;
    QString typeIcon()                         const;
    QColor  typeColor()                        const;
};

Q_DECLARE_METATYPE(Segment)

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
    QList<QString>  m_deletedIds;
    QList<QString>  m_sequenceIds;
};

// ── Background media picker ───────────────────────────────────
class BgPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit BgPickerDialog(const QStringList &mediaPaths,
                            const QString &currentPath,
                            QWidget *parent = nullptr);
    QString selectedPath() const { return m_selected; }
private:
    QString m_selected;
};

// ── Service Order item delegate ───────────────────────────────
class SegmentDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit SegmentDelegate(QObject *parent = nullptr);
    void  paint(QPainter *p, const QStyleOptionViewItem &opt,
                const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx)  const override;
};

// ── Slide card ────────────────────────────────────────────────
class SlideButton : public QFrame {
    Q_OBJECT
public:
    SlideButton(const QString &section, const QString &text,
                int slideIndex, int sectionIndex, int slideNumber,
                QWidget *parent = nullptr);
    void setActive(bool active);
    void setSectionColor(const QColor &c);
    void setFontColor(const QColor &c);
    void applyScale(double scale, const QString &fontFamily = "Arial", int fixedWidth = -1);
    void setBackgroundThumb(const QString &path);
    int     slideIndex()    const { return m_slideIdx; }
    int     sectionIndex()  const { return m_sectionIdx; }
    QString backgroundPath() const { return m_bgPath; }
    static const int BASE_W = 200;
    static const int BASE_H = 120;
signals:
    void clicked(int slideIndex);
    void editRequested(int sectionIndex);
    void bgAssignRequested(int slideIndex, bool forSegment);
    void bgClearRequested(int slideIndex, bool forSegment);
protected:
    void mousePressEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
private:
    QLabel  *m_sectionLbl, *m_textLbl, *m_slideNumLbl;
    int      m_slideIdx, m_sectionIdx;
    bool     m_active = false;
    QColor   m_sectionColor = Qt::white;
    QColor   m_fontColor    = Qt::white;
    QString  m_fontFamily   = "Arial";
    int      m_fontSize     = 12;
    QPixmap  m_bgThumb;
    QString  m_bgPath;
};

// ── Main dock ─────────────────────────────────────────────────
class LyricsDock : public QWidget {
    Q_OBJECT
public:
    explicit LyricsDock(QWidget *parent = nullptr);
    void saveState();
    void loadState();
    void setWsServer(WebSocketServer *srv);
    QJsonArray buildSequenceJson() const;
    QJsonArray buildServiceOrderJson() const;
    void       broadcastSequence();
    void pushSlideFromRemote(const QString &songTitle,
                             int slideIndex,
                             const QString &lyrics,
                             const QString &section);
    void activateSlideFromRemote(int segIdx, int slideIdx);
    void applyScriptureFromRemote(const QString &title, const QString &text);
    void setCurrentBgPath(const QString &p) { m_currentBgPath = p; }
    void stopAllMedia();
    void releasePptCapture();
    void pptNext();
    void pptPrev();
    void pptGoto(int slide);
    void pptGetState();
    void applyParagraphFromRemote(const QString &title, const QString &text);
    void pushParagraphSlideFromRemote(const QString &title, const QString &text);
    void applyColorPresetFromRemote(int idx);
    void audioBinPlayFromRemote(int index);
    void audioBinStopFromRemote();
    void applyParagraphsFromRemote(const QString &title, const QString &text);
    void presentationShowFromRemote(int type);
    QJsonArray colorPresetsJson() const;
    void pushParagraphCurrent();
    void pushScriptureCurrent();
    void restoreCurrentTabContent();
    Q_INVOKABLE void activateNextSlide();
    Q_INVOKABLE void activatePrevSlide();
    void lpNextSlide(const QString &mode);
    void lpPrevSlide(const QString &mode);
    Q_INVOKABLE void activateNextSong();
    Q_INVOKABLE void activatePrevSong();
    Q_INVOKABLE void activateGalleryMedia(int oneBased);

protected:
    void resizeEvent(QResizeEvent *) override;

private slots:
    void onAddSegment();
    void onRemoveSegment();
    void onImportService();
    void onExportService();
    void onSegmentUp();
    void onSegmentDown();
    void onSegmentSelected(int row);
    void onSequenceEditSong();
    void onImportFromWeb();
    void onSlideClicked(int slideIndex);
    void onSlideEditRequested(int sectionIndex);
    void onBlackoutClicked();
    void onNextSlide();
    void onPrevSlide();
    void onZoomIn();
    void onZoomOut();
    void onZoomSlider(int value);
    void onColorPresetApplied(QColor fontColor, QColor outlineColor);
    void onSegmentContextMenu(const QPoint &pos);
    void onSlideBgAssign(int slideIndex, bool forSegment);
    void onSlideBgClear(int slideIndex, bool forSegment);
    void onLiveToggled(bool on);
    void onParagraphsContextMenu(const QPoint &pos);
    void onLiveMediaCardClicked(int segIdx, int fileIdx);
    void onLiveMediaPlayPause(int segIdx, int fileIdx);
    void onLiveMediaRewind(int segIdx, int fileIdx);
    void onLivePptActivate(int segIdx);
    void onLivePptPrev(int segIdx);
    void onLivePptNext(int segIdx);
    void updateEditSlidePanel();
    void onEditSlideApply();

private:
    void buildUi();
    QWidget *buildSongPanel();
    QWidget *buildMediaPanel();
    void loadSongSlides(const Song &song);
    void loadParagraphSlides(const Segment &item);
    void buildAllLiveSlides();
    void loadSegmentContent(int idx);
    void clearSlideGrid();
    void setActiveSlide(int index);
    void pushCurrentSlide();
    void applySlideBackground();
    // Put `newBg` on air with the shared fade settings; blank clears; same path = no-op.
    void showBackground(const QString &newBg);
    // After a background assign/clear: refresh card thumbs and the background on air.
    void refreshAfterBackgroundChange();
    // Re-align every segment's cards with its current text (after lines-per-slide changes).
    void syncAllCards();
    void refreshSlideCardThumbs();
    void updateSlideCounter();
    void applyZoomToAll();
    void saveServiceOrder();
    void loadServiceOrder();
    void rebuildSegmentList();
    void updateSegmentButtons();
    int  computeGlobalSlideOffset(int forRow) const;
    QString sequenceCurrentSongId() const;
    void    updateSegmentListItem(int row);
    void    removeSegment(int row);
    void    showParagraphEditDialog(int segIdx);
    // Everything a paragraph segment sets before its slide is pushed: its own layout and its
    // alternate-sentence colour.
    void    applyParagraphStyle(const Segment &item);
    // Put a segment's saved colour preset into the renderer for its NEXT push, without
    // re-rendering what is on air (selecting a segment must not recolour the on-air slide).
    void    applySegmentPreset(const Segment &seg);
    void    showSegmentLayoutDialog(int segIdx);
    // Make sure a paragraph segment holds a complete layout of its own (copied from the current
    // default for anything it lacks).  Returns true if the segment changed.
    bool    ensureParagraphLayout(Segment &seg);

    // ── On-air tracking by id ──
    void rememberOnAir(const Segment &seg, int slideIdx);
    // Tell the remote what is on air now (segment + card by id).  Only sends when it changed.
    void publishOnAir();
    // Segment index + card index of the on-air card, found by id; false if nothing is on air.
    bool resolveOnAir(int &segIdx, int &cardIdx) const;
    // Next (+1) / previous (-1) slide card across the whole service, from the on-air card.
    bool navigateQueue(int delta);
    int  onAirIndexIn(const Segment &seg) const;      // -1 = none / its card was deleted
    // Position of the on-air slide in the LIVE grid's numbering (every segment's slides counted in
    // service order, exactly as buildAllLiveSlides numbers them); -1 if nothing is on air.
    int  liveGlobalIndexOnAir() const;
    bool isLiveMedia(int segIdx, int fileIdx) const;
    void setLiveMedia(int segIdx, int fileIdx);
    void clearLiveMedia();
    QString liveMediaPath() const;
    bool isLivePpt(int segIdx) const;
    void setLivePpt(int segIdx);
    // Single entry point that points the shared capture source at a Presentation item.
    void    bindPptItem(int segIdx);
    int     segmentIndexById(const QString &id) const;
    void    importSegmentsFromRemote(const QJsonArray &items);
    QJsonObject buildFullServiceExport() const;
    void    applyFullServiceImport(const QJsonObject &root, bool replaceLayout);
    void    applyColorPresetsFromJson(const QJsonArray &arr);
    void    applySourceSettingsFromJson(const QString &jsonStr);

    // ── Service Order ─────────────────────────────────────────
    QList<Segment>  m_segments;
    int                 m_activeSegIdx = -1;
    QListWidget        *m_segmentList      = nullptr;
    QPushButton        *m_addItemBtn       = nullptr;
    QPushButton        *m_removeItemBtn    = nullptr;
    QPushButton        *m_itemUpBtn        = nullptr;
    QPushButton        *m_itemDownBtn      = nullptr;

    // ── Song slides ───────────────────────────────────────────
    QScrollArea        *m_slideArea        = nullptr;
    QWidget            *m_slideContainer   = nullptr;
    QGridLayout        *m_slideGrid        = nullptr;
    QList<SlideButton*> m_slideButtons;

    QPushButton        *m_prevBtn          = nullptr;
    QPushButton        *m_nextBtn          = nullptr;
    QPushButton        *m_blackoutBtn      = nullptr;
    LayoutDialog       *m_lyricsLayoutDialog     = nullptr;
    ColorPresetButton  *m_colorBtn[6]       = {};
    QSpinBox           *m_linesPerSlideSpin = nullptr;
    QLabel             *m_slideCounter      = nullptr;
    QToolButton        *m_zoomInBtn         = nullptr;
    QToolButton        *m_zoomOutBtn        = nullptr;
    QSlider            *m_zoomSlider        = nullptr;
    QLabel             *m_zoomLabel         = nullptr;
    QString             m_slideFontFamily   = "Calibri";
    QSlider            *m_cardWidthSlider   = nullptr;
    QLabel             *m_cardWidthLabel    = nullptr;
    QToolButton        *m_cardWidthOutBtn   = nullptr;
    QToolButton        *m_cardWidthInBtn    = nullptr;
    int                 m_cardWidthScale    = 3;   // cards per row (1-5)
    QString             m_currentBgPath;
    QColor              m_slideFontColor    = Qt::white;
    int                 m_activeColorPresetIdx = -1;

    // ── Center + Right ────────────────────────────────────────
    QStackedWidget     *m_contentStack     = nullptr;
    MediaGallery       *m_sharedGallery    = nullptr;

    // ── Content widgets ───────────────────────────────────────
    WebSocketServer    *m_wsServer         = nullptr;
    WsTab              *m_wsTab            = nullptr;
    AudioBin           *m_audioBin         = nullptr;
    VideoTab           *m_videoTab         = nullptr;
    PptTab             *m_pptTab           = nullptr;
    QDialog            *m_settingsDialog   = nullptr;
    QToolButton        *m_bgDebugBtn      = nullptr;

    // ── Mode tracking ─────────────────────────────────────────
    QPushButton        *m_liveToggleBtn    = nullptr;
    QPushButton        *m_editBtn          = nullptr;
    bool                m_liveMode         = false;
    bool                m_paragraphMode    = false;
    QList<QWidget*>     m_liveSeparators;
    QList<QWidget*>     m_liveExtraCards;
    // Live media card that is playing / paused, remembered by id (segment id + card id)
    QString             m_liveMediaSegId;
    QString             m_liveMediaCardId;
    bool                m_liveMediaPlaying = false;
    bool                m_liveMediaPaused  = false;
    QString             m_activePptSegId;   // segment id of the live presentation card on air
    // The slide card on air, by id: edits that insert/remove cards cannot move it.
    QString             m_onAirSegId;
    QString             m_onAirCardId;
    QByteArray          m_lastOnAirKey;    // last on-air message sent to the remote
    QString             m_pptBoundSegId;  // Presentation item that owns the capture window

    // ── New members ───────────────────────────────────────────
    QSpinBox          *m_globalFadeSpin    = nullptr;
    QWidget           *m_editSlidePanel    = nullptr;
    QPlainTextEdit    *m_editSlideText     = nullptr;
    QPushButton       *m_editSlideApplyBtn = nullptr;
    QLabel            *m_editSlideHdr      = nullptr;
    QWidget           *m_slideCtrlRow      = nullptr;

    // ── Song state ────────────────────────────────────────────
    Song   m_currentSong;
    int    m_activeSlide           = -1;
    int    m_currentSongSlideOffset = 0;
    double m_slideScale  = 1.0;
    QList<LyricsSlide>  m_slides;
    QList<int>          m_slideSectionIndices;
    QList<QString>      m_sequenceSongIds; // derived cache: Lyrics segment ids in order
};
