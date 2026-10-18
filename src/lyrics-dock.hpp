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

// ── Service Order item ────────────────────────────────────────
struct ServiceItem {
    enum class Type { Lyrics, Paragraphs, Media, Presentation };
    Type    type    = Type::Lyrics;
    QString id;           // Lyrics: library ID; others: uuid
    QString title;        // Required for Paragraphs / Media / Presentation
    QString artist;       // Lyrics only
    QList<LyricsSection> sections; // embedded song data for portability
    QString text;         // Paragraphs: body text (blank lines = slide breaks)
    QString filePath;     // Media / Presentation (legacy single-file)
    QStringList mediaPaths; // Media: list of media file paths
    QMap<int,QString> slideBgs;  // slide-index → bg path (Paragraphs)
    int colorPresetIdx = -1;     // which color preset button (0-based) was last applied; -1 = none
    QStringList parseSlides() const;

    QJsonObject toJson()                       const;
    static ServiceItem fromJson(const QJsonObject &obj);
    QString typeLabel()                        const;
    QString typeIcon()                         const;
    QColor  typeColor()                        const;
};

Q_DECLARE_METATYPE(ServiceItem)

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
class ServiceItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ServiceItemDelegate(QObject *parent = nullptr);
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
    void bgAssignRequested(int slideIndex, bool forWholeSong);
    void bgClearRequested(int slideIndex, bool forWholeSong);
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
    void activateSlideFromRemote(int serviceIdx, int slideIdx);
    void applyScriptureFromRemote(const QString &title, const QString &text);
    void stopAllMedia();
    void pptNext();
    void pptPrev();
    void pptGoto(int slide);
    void pptGetState();
    void applyCTWFromRemote(const QString &title, const QString &text);
    void pushCTWSlideFromRemote(const QString &title, const QString &text);
    void applyColorPresetFromRemote(int idx);
    void audioBinPlayFromRemote(int index);
    void audioBinStopFromRemote();
    void applyParagraphsFromRemote(const QString &title, const QString &text);
    void presentationShowFromRemote(int type);
    QJsonArray colorPresetsJson() const;
    void pushCTWCurrent();
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
    void onAddServiceItem();
    void onRemoveServiceItem();
    void onImportService();
    void onExportService();
    void onServiceItemUp();
    void onServiceItemDown();
    void onServiceItemSelected(int row);
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
    void onServiceContextMenu(const QPoint &pos);
    void onSlideBgAssign(int slideIndex, bool forWholeSong);
    void onSlideBgClear(int slideIndex, bool forWholeSong);
    void onLiveToggled(bool on);
    void onParagraphsContextMenu(const QPoint &pos);
    void onLiveMediaCardClicked(int svcIdx, int fileIdx);
    void onLiveMediaPlayPause(int svcIdx, int fileIdx);
    void onLiveMediaRewind(int svcIdx, int fileIdx);
    void onLivePptActivate(int svcIdx);
    void onLivePptPrev(int svcIdx);
    void onLivePptNext(int svcIdx);
    void updateEditSlidePanel();
    void onEditSlideApply();

private:
    void buildUi();
    QWidget *buildSongPanel();
    QWidget *buildMediaPanel();
    void loadSongSlides(const Song &song);
    void loadParagraphSlides(const ServiceItem &item);
    void buildAllLiveSlides();
    void loadServiceItemContent(int idx);
    void clearSlideGrid();
    void setActiveSlide(int index);
    void pushCurrentSlide();
    void applySlideBackground();
    void refreshSlideCardThumbs();
    void updateSlideCounter();
    void applyZoomToAll();
    void saveServiceOrder();
    void loadServiceOrder();
    void rebuildServiceList();
    void updateServiceItemButtons();
    int  computeGlobalSlideOffset(int forRow) const;
    QString sequenceCurrentSongId() const;
    void    updateServiceListItem(int row);
    void    removeServiceItem(int row);
    void    showParagraphEditDialog(int serviceIdx);
    void    importServiceItemsFromRemote(const QJsonArray &items);
    QJsonArray buildExportServiceJson() const;

    // ── Service Order ─────────────────────────────────────────
    QList<ServiceItem>  m_serviceItems;
    int                 m_activeServiceIdx = -1;
    QListWidget        *m_serviceList      = nullptr;
    QPushButton        *m_addItemBtn       = nullptr;
    QPushButton        *m_removeItemBtn    = nullptr;
    QPushButton        *m_itemUpBtn        = nullptr;
    QPushButton        *m_itemDownBtn      = nullptr;
    QPushButton        *m_importWebBtn     = nullptr;

    // ── Song slides ───────────────────────────────────────────
    QScrollArea        *m_slideArea        = nullptr;
    QWidget            *m_slideContainer   = nullptr;
    QGridLayout        *m_slideGrid        = nullptr;
    QList<SlideButton*> m_slideButtons;

    QPushButton        *m_prevBtn          = nullptr;
    QPushButton        *m_nextBtn          = nullptr;
    QPushButton        *m_blackoutBtn      = nullptr;
    LayoutDialog       *m_lyricsLayoutDialog = nullptr;
    ColorPresetButton  *m_colorBtn[6]       = {};
    QSpinBox           *m_linesPerSlideSpin = nullptr;
    QLabel             *m_slideCounter      = nullptr;
    QToolButton        *m_zoomInBtn         = nullptr;
    QToolButton        *m_zoomOutBtn        = nullptr;
    QSlider            *m_zoomSlider        = nullptr;
    QLabel             *m_zoomLabel         = nullptr;
    QFontComboBox      *m_slideFontCombo    = nullptr;
    QString             m_slideFontFamily   = "Arial";
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
    int                 m_liveMediaSvcIdx  = -1;
    int                 m_liveMediaFileIdx = -1;
    bool                m_liveMediaPlaying = false;
    bool                m_liveMediaPaused  = false;
    int                 m_activePptSvcIdx  = -1;

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
    QList<QString>      m_sequenceSongIds; // derived cache: Song-type IDs in order
};
