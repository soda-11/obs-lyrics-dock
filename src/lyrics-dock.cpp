// lyrics-dock.cpp  v9.5.1
#include "lyrics-dock.hpp"
#include "song-edit-dialog.hpp"
#include "media-gallery.hpp"

#include "lyrics-renderer.hpp"
#include "settings-dialog.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QMessageBox>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QShortcut>
#include <QKeySequence>
#include <QStyle>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ══════════════════════════════════════════════════════════════
// SongLibraryPopup
// ══════════════════════════════════════════════════════════════

SongLibraryPopup::SongLibraryPopup(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Song Library");
    setMinimumSize(760, 540);

    auto *mainLayout = new QHBoxLayout(this);

    // Left: list + controls
    auto *leftPanel  = new QWidget;
    leftPanel->setMinimumWidth(320);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("Search songs...");
    m_searchEdit->setClearButtonEnabled(true);

    m_list = new QListWidget;
    m_list->setStyleSheet(
        "QListWidget { background: #1e1e1e; }"
        "QListWidget::item:selected { background: #1e6bb8; }");

    auto *btnRow    = new QHBoxLayout;
    m_addBtn    = new QPushButton("Add to Sequence");
    m_removeBtn = new QPushButton("Remove from Sequence");
    m_deleteBtn = new QPushButton("Delete from Library");
    auto *newBtn    = new QPushButton("New Song");
    auto *closeBtn  = new QPushButton("Close");

    m_addBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_removeBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_deleteBtn->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
    newBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_addBtn->setDefault(true);
    m_addBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    // Colour the delete button red-ish to indicate destructive action
    m_deleteBtn->setStyleSheet(
        "QPushButton { color: #ff6b6b; }"
        "QPushButton:disabled { color: #555; }");

    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addWidget(m_deleteBtn);
    btnRow->addWidget(newBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);

    leftLayout->addWidget(new QLabel("Song Library:"));
    leftLayout->addWidget(m_searchEdit);
    leftLayout->addWidget(m_list, 1);
    leftLayout->addLayout(btnRow);

    // Right: lyrics preview
    auto *rightPanel  = new QWidget;
    rightPanel->setMinimumWidth(300);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto *previewLbl = new QLabel("Lyrics Preview:");
    previewLbl->setStyleSheet("color: #888; font-size: 9px;");

    m_lyricsPreview = new QTextEdit;
    m_lyricsPreview->setReadOnly(true);
    m_lyricsPreview->setStyleSheet(
        "QTextEdit { background: #141414; color: #bbb;"
        " font-size: 10px; border: 1px solid #333; }");
    m_lyricsPreview->setPlaceholderText("Click a song to preview...");

    rightLayout->addWidget(previewLbl);
    rightLayout->addWidget(m_lyricsPreview, 1);

    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(rightPanel);

    refreshList();

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &SongLibraryPopup::onSearch);
    connect(m_list, &QListWidget::currentRowChanged,
            this, [this](int){ onSelectionChanged(); });
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, [this](){ onAdd(); });
    connect(m_addBtn,   &QPushButton::clicked, this, &SongLibraryPopup::onAdd);
    connect(m_removeBtn,&QPushButton::clicked, this, &SongLibraryPopup::onRemove);
    connect(m_deleteBtn,&QPushButton::clicked, this, &SongLibraryPopup::onDeleteSong);
    connect(newBtn,     &QPushButton::clicked, this, &SongLibraryPopup::onAddNewSong);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::accept);
}

void SongLibraryPopup::setSequenceIds(const QList<QString> &ids)
{
    m_sequenceIds = ids;
    refreshList();
}

void SongLibraryPopup::refreshList()
{
    onSearch(m_searchEdit ? m_searchEdit->text() : QString());
}

void SongLibraryPopup::onSearch(const QString &text)
{
    m_list->clear();
    for (const auto &song : SongManager::instance().songs()) {
        if (!text.isEmpty() &&
            !song.title.contains(text, Qt::CaseInsensitive) &&
            !song.artist.contains(text, Qt::CaseInsensitive))
            continue;
        bool inSeq = m_sequenceIds.contains(song.id);
        QString display = (inSeq
            ? QString::fromUtf8("\u2713 ")
            : QString("   "))
            + song.title;
        if (!song.artist.isEmpty())
            display += "  \u2014  " + song.artist;
        auto *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, song.id);
        if (inSeq) item->setForeground(QColor("#4da6ff"));
        m_list->addItem(item);
    }
    onSelectionChanged();
}

void SongLibraryPopup::onSelectionChanged()
{
    auto *item = m_list->currentItem();
    if (!item) {
        m_lyricsPreview->clear();
        m_addBtn->setEnabled(false);
        m_removeBtn->setEnabled(false);
        m_deleteBtn->setEnabled(false);
        return;
    }
    QString id   = item->data(Qt::UserRole).toString();
    bool    inSeq = m_sequenceIds.contains(id);

    m_addBtn->setEnabled(!inSeq);
    m_removeBtn->setEnabled(inSeq);
    m_deleteBtn->setEnabled(true);  // can always delete a song from library

    auto *song = SongManager::instance().findById(id);
    if (!song) { m_lyricsPreview->clear(); return; }

    QString preview;
    for (const auto &sec : song->sections) {
        QString lbl = sec.label.isEmpty()
                      ? sectionTypeLabel(sec.type) : sec.label;
        preview += "[" + lbl + "]\n" + sec.text.trimmed() + "\n\n";
    }
    m_lyricsPreview->setPlainText(preview.trimmed());
}

void SongLibraryPopup::onAdd()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty() || m_sequenceIds.contains(id)) return;

    m_addedIds    << id;
    m_sequenceIds << id;

    // Also un-remove if it was previously removed this session
    m_removedIds.removeAll(id);

    refreshList();
}

void SongLibraryPopup::onRemove()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty() || !m_sequenceIds.contains(id)) return;

    m_removedIds   << id;
    m_sequenceIds.removeAll(id);

    // Also un-add if it was added this session
    m_addedIds.removeAll(id);

    refreshList();
}

void SongLibraryPopup::onDeleteSong()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    auto *song = SongManager::instance().findById(id);
    if (!song) return;

    // Confirm before permanent delete
    auto btn = QMessageBox::warning(
        this,
        "Delete Song from Library",
        QString("Permanently delete \"%1\" from the song library?\n\n"
                "This cannot be undone. The song will also be removed "
                "from the show sequence if present.").arg(song->title),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (btn != QMessageBox::Yes) return;

    // Track the deletion
    m_deletedIds << id;

    // Remove from sequence tracking too
    m_sequenceIds.removeAll(id);
    m_addedIds.removeAll(id);
    m_removedIds.removeAll(id);

    // Delete from the actual library
    SongManager::instance().removeSong(id);

    refreshList();
}

void SongLibraryPopup::onAddNewSong()
{
    SongEditDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    SongManager::instance().addSong(dlg.result());
    refreshList();
    emit newSongAdded();
}

// ══════════════════════════════════════════════════════════════
// SlideButton
// ══════════════════════════════════════════════════════════════

SlideButton::SlideButton(const QString &section, const QString &text,
                         int slideIndex, int sectionIndex,
                         QWidget *parent)
    : QFrame(parent), m_slideIdx(slideIndex), m_sectionIdx(sectionIndex)
{
    setFixedSize(BASE_W, BASE_H);
    setCursor(Qt::PointingHandCursor);
    setFrameShape(QFrame::StyledPanel);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);

    m_sectionLbl = new QLabel(section);
    m_textLbl    = new QLabel(text);
    m_textLbl->setWordWrap(true);
    m_textLbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    layout->addWidget(m_sectionLbl);
    layout->addWidget(m_textLbl, 1);

    setActive(false);
}

void SlideButton::applyScale(double scale)
{
    setFixedSize(qMax(80, (int)(BASE_W * scale)),
                 qMax(50, (int)(BASE_H * scale)));
    QString tc = m_active ? "#fff"    : "#ccc";
    QString sc = m_active ? "#aad4ff" : "#888";
    int fs = qMax(7, (int)(11 * scale));
    int ls = qMax(7, (int)( 9 * scale));
    m_textLbl->setStyleSheet(
        QString("font-size: %1px; color: %2;").arg(fs).arg(tc));
    m_sectionLbl->setStyleSheet(
        QString("font-size: %1px; font-weight: bold; color: %2;")
            .arg(ls).arg(sc));
}

void SlideButton::setActive(bool active)
{
    m_active = active;
    if (active) {
        setStyleSheet(
            "SlideButton { background: #1e6bb8;"
            " border: 2px solid #4da6ff; border-radius: 4px; }");
        m_sectionLbl->setStyleSheet(
            "font-size: 9px; color: #aad4ff; font-weight: bold;");
        m_textLbl->setStyleSheet("font-size: 11px; color: #fff;");
    } else {
        setStyleSheet(
            "SlideButton { background: #2a2a2a;"
            " border: 1px solid #444; border-radius: 4px; }"
            "SlideButton:hover {"
            " border: 1px solid #888; background: #333; }");
        m_sectionLbl->setStyleSheet(
            "font-size: 9px; color: #888; font-weight: bold;");
        m_textLbl->setStyleSheet("font-size: 11px; color: #ccc;");
    }
}

void SlideButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        emit clicked(m_slideIdx);
    QFrame::mousePressEvent(e);
}

void SlideButton::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    auto *editAct = menu.addAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        "Edit this section...");
    if (menu.exec(e->globalPos()) == editAct)
        emit editRequested(m_sectionIdx);
}

void SlideButton::paintEvent(QPaintEvent *e) { QFrame::paintEvent(e); }

// ══════════════════════════════════════════════════════════════
// LyricsDock
// ══════════════════════════════════════════════════════════════

static QString statePath()
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/dock-state.json";
}

LyricsDock::LyricsDock(QWidget *parent) : QWidget(parent)
{
    setObjectName("LyricsDockContent");
    buildUi();
    loadState();

    auto *fwd = new QShortcut(QKeySequence(Qt::Key_Right), this);
    auto *bck = new QShortcut(QKeySequence(Qt::Key_Left),  this);
    connect(fwd, &QShortcut::activated, this, &LyricsDock::onNextSlide);
    connect(bck, &QShortcut::activated, this, &LyricsDock::onPrevSlide);
}


// ── Sequence JSON builder ─────────────────────────────────────
// Builds the sequence as JSON matching the slide tile order in the UI.
// Each song has "slides" array matching exactly what loadSongSlides() builds.
QJsonArray LyricsDock::buildSequenceJson() const
{
    QJsonArray songs;
    int globalIdx = 0; // globally unique across ALL songs in sequence
    for (const auto &id : m_sequenceSongIds) {
        auto *song = SongManager::instance().findById(id);
        if (!song) continue;
        QJsonObject s;
        s["id"]     = song->id;
        s["title"]  = song->title;
        s["artist"] = song->artist;
        QJsonArray slides;
        for (const auto &sec : song->sections) {
            QString lbl = sec.label.isEmpty()
                          ? sectionTypeLabel(sec.type) : sec.label;
            for (const auto &slideText : sec.slides()) {
                QJsonObject sl;
                sl["label"]      = lbl;
                sl["text"]       = slideText;
                sl["slideIndex"] = globalIdx++;
                slides.append(sl);
            }
        }
        s["slides"] = slides;
        songs.append(s);
    }
    return songs;
}

void LyricsDock::broadcastSequence()
{
    if (!m_wsServer) return;
    m_wsServer->broadcastSongs(buildSequenceJson());
}

// Called from WebSocket server (main thread via QueuedConnection).
// Finds the song by title, selects it in the sequence list, and
// highlights the correct slide tile — keeping the dock in sync with
// whatever the remote manager pushed.
void LyricsDock::pushSlideFromRemote(const QString &songTitle,
                                      int slideIndex,
                                      const QString &lyrics,
                                      const QString &section)
{
    // Find the song row in the sequence by title
    for (int row = 0; row < m_sequenceSongIds.size(); ++row) {
        auto *song = SongManager::instance().findById(m_sequenceSongIds[row]);
        if (!song) continue;
        if (song->title != songTitle) continue;

        // Select that row (loads its slides into the panel)
        if (m_sequenceList->currentRow() != row) {
            m_sequenceList->setCurrentRow(row);
            // loadSongSlides is triggered by onSequenceSongSelected
        }

        // The slideIndex from the remote is GLOBAL across all songs.
        // We need to find which local slide index within this song it maps to.
        // Use the loop's row variable — NOT m_sequenceList->currentRow()
        // which may not yet reflect the setCurrentRow() call above.
        int localIdx = slideIndex - computeGlobalSlideOffset(row);

        // Highlight the tile and push to renderer
        setActiveSlide(localIdx);
        lyrics_renderer_push(lyrics, section, songTitle);
        return;
    }

    // Song not found in sequence — push anyway without highlight
    lyrics_renderer_push(lyrics, section, songTitle);
}

void LyricsDock::saveState()
{
    QJsonArray arr;
    for (const auto &id : m_sequenceSongIds)
        arr.append(id);
    QJsonObject obj;
    obj["sequence"] = arr;
    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
    if (m_mediaGallery) m_mediaGallery->saveState();
}

void LyricsDock::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;

    m_sequenceList->clear();
    m_sequenceSongIds.clear();

    for (const auto &v : doc.object()["sequence"].toArray()) {
        QString id   = v.toString();
        auto   *song = SongManager::instance().findById(id);
        if (!song) continue;
        m_sequenceSongIds << id;
        QString display = song->title;
        if (!song->artist.isEmpty())
            display += "  \u2014  " + song->artist;
        auto *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, id);
        m_sequenceList->addItem(item);
    }
}

void LyricsDock::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Title bar
    auto *titleBar = new QWidget;
    titleBar->setStyleSheet(
        "background: #1a1a1a; border-bottom: 1px solid #333;");
    auto *titleL = new QHBoxLayout(titleBar);
    titleL->setContentsMargins(8, 3, 8, 3);
    auto *titleLbl = new QLabel("OBS Lyrics Dock");
    titleLbl->setStyleSheet(
        "color: #ccc; font-weight: bold; font-size: 11px;");
    auto *verLbl = new QLabel("v9.5.1");
    verLbl->setStyleSheet(
        "color: #ff4444; font-size: 9px; font-weight: bold;");
    auto *settingsBtn = new QToolButton;
    settingsBtn->setText(QString::fromUtf8("⚙"));
    settingsBtn->setToolTip("WebSocket & Settings");
    settingsBtn->setStyleSheet(
        "QToolButton{color:#888;border:none;font-size:14px;padding:0 4px;}"
        "QToolButton:hover{color:#ccc;}");
    connect(settingsBtn, &QToolButton::clicked, this, [this](){
        if (!m_settingsDialog) {
            m_settingsDialog = new SettingsDialog(this);
        }
        m_settingsDialog->show();
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
    });
    titleL->addWidget(titleLbl);
    titleL->addStretch();
    titleL->addWidget(verLbl);
    titleL->addWidget(settingsBtn);
    root->addWidget(titleBar);

    auto *splitter = new QSplitter(Qt::Horizontal);

    // ════════════════════════════════════════════════════════
    // LEFT — Show Sequence
    // ════════════════════════════════════════════════════════
    auto *seqGroup  = new QGroupBox("Show Sequence");
    seqGroup->setMaximumWidth(280);
    auto *seqLayout = new QVBoxLayout(seqGroup);
    seqLayout->setSpacing(4);

    m_addToSeqBtn = new QPushButton(
        QString::fromUtf8("\uFF0B  Manage Songs"));
    m_addToSeqBtn->setIcon(
        style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    m_addToSeqBtn->setToolTip(
        "Open song library to add/remove songs from sequence");

    m_sequenceList = new QListWidget;
    m_sequenceList->setDragDropMode(QAbstractItemView::InternalMove);
    m_sequenceList->setDefaultDropAction(Qt::MoveAction);
    m_sequenceList->setStyleSheet(
        "QListWidget { background: #1e1e1e; }"
        "QListWidget::item:selected { background: #1e6bb8; }");

    auto *seqBtns = new QHBoxLayout;
    m_removeFromSeqBtn = new QPushButton;
    m_seqUpBtn         = new QPushButton;
    m_seqDownBtn       = new QPushButton;
    m_seqEditBtn       = new QPushButton;

    m_removeFromSeqBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_removeFromSeqBtn->setToolTip("Remove from sequence");
    m_seqUpBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_seqUpBtn->setToolTip("Move up");
    m_seqDownBtn->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_seqDownBtn->setToolTip("Move down");
    m_seqEditBtn->setIcon(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    m_seqEditBtn->setToolTip("Edit song");

    for (auto *b : { m_removeFromSeqBtn, m_seqUpBtn,
                     m_seqDownBtn, m_seqEditBtn })
        b->setFixedSize(32, 32);

    m_removeFromSeqBtn->setEnabled(false);
    m_seqUpBtn->setEnabled(false);
    m_seqDownBtn->setEnabled(false);
    m_seqEditBtn->setEnabled(false);

    seqBtns->addWidget(m_removeFromSeqBtn);
    seqBtns->addWidget(m_seqUpBtn);
    seqBtns->addWidget(m_seqDownBtn);
    seqBtns->addWidget(m_seqEditBtn);
    seqBtns->addStretch();

    seqLayout->addWidget(m_addToSeqBtn);
    seqLayout->addWidget(m_sequenceList, 1);
    seqLayout->addLayout(seqBtns);

    // ════════════════════════════════════════════════════════
    // RIGHT — Slides
    // ════════════════════════════════════════════════════════
    auto *slidesGroup  = new QGroupBox("Slides");
    auto *slidesPanelL = new QVBoxLayout(slidesGroup);
    slidesPanelL->setContentsMargins(0, 4, 0, 0);
    slidesPanelL->setSpacing(0);

    m_slideArea = new QScrollArea;
    m_slideArea->setWidgetResizable(true);
    m_slideArea->setStyleSheet(
        "QScrollArea { background: #181818; border: none; }");
    m_slideContainer = new QWidget;
    m_slideContainer->setStyleSheet("background: #181818;");
    m_slideGrid = new QGridLayout(m_slideContainer);
    m_slideGrid->setContentsMargins(8, 8, 8, 8);
    m_slideGrid->setSpacing(8);
    m_slideGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_slideArea->setWidget(m_slideContainer);

    // Controls bar — no Template button
    auto *ctrlBar = new QWidget;
    ctrlBar->setStyleSheet(
        "background: #222; border-top: 1px solid #333;");
    auto *ctrlL = new QHBoxLayout(ctrlBar);
    ctrlL->setContentsMargins(6, 4, 6, 4);
    ctrlL->setSpacing(4);

    m_prevBtn = new QPushButton(QString::fromUtf8("\u25C4"));
    m_prevBtn->setFixedWidth(36);
    m_prevBtn->setToolTip("Previous slide  (Left arrow)");

    m_slideCounter = new QLabel(QString::fromUtf8("\u2014"));
    m_slideCounter->setAlignment(Qt::AlignCenter);
    m_slideCounter->setMinimumWidth(52);

    m_nextBtn = new QPushButton(QString::fromUtf8("\u25BA"));
    m_nextBtn->setFixedWidth(36);
    m_nextBtn->setToolTip("Next slide  (Right arrow)");

    m_blackoutBtn = new QPushButton(
        QString::fromUtf8("\u25A0 Clear"));
    m_blackoutBtn->setToolTip("Clear lyrics output");
    m_blackoutBtn->setStyleSheet(
        "QPushButton { background: #4a1a1a; color: #ff6b6b;"
        " border: 1px solid #8b2020; border-radius: 3px; padding: 3px 6px; }"
        "QPushButton:hover { background: #6a2020; }");

    m_zoomOutBtn = new QToolButton;
    m_zoomOutBtn->setText("-");
    m_zoomOutBtn->setToolTip("Zoom out");

    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(1, 8);
    m_zoomSlider->setValue(4);
    m_zoomSlider->setFixedWidth(80);
    m_zoomSlider->setToolTip("Zoom slide cards (25% \u2013 200%)");

    m_zoomInBtn = new QToolButton;
    m_zoomInBtn->setText("+");
    m_zoomInBtn->setToolTip("Zoom in");

    m_zoomLabel = new QLabel("100%");
    m_zoomLabel->setFixedWidth(38);
    m_zoomLabel->setStyleSheet("color: #888; font-size: 9px;");

    ctrlL->addWidget(m_prevBtn);
    ctrlL->addWidget(m_slideCounter);
    ctrlL->addWidget(m_nextBtn);
    ctrlL->addWidget(m_blackoutBtn);
    ctrlL->addStretch();
    ctrlL->addWidget(m_zoomOutBtn);
    ctrlL->addWidget(m_zoomSlider);
    ctrlL->addWidget(m_zoomInBtn);
    ctrlL->addWidget(m_zoomLabel);

    slidesPanelL->addWidget(m_slideArea, 1);
    slidesPanelL->addWidget(ctrlBar);

    // Media Gallery panel (right)
    auto *galleryGroup = new QGroupBox("Media Gallery");
    auto *galleryLayout = new QVBoxLayout(galleryGroup);
    galleryLayout->setContentsMargins(0,0,0,0);
    m_mediaGallery = new MediaGallery;
    galleryLayout->addWidget(m_mediaGallery);

    splitter->addWidget(seqGroup);
    splitter->addWidget(slidesGroup);
    splitter->addWidget(galleryGroup);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({240, 500, 280});
    root->addWidget(splitter, 1);

    // Connections
    connect(m_addToSeqBtn,     &QPushButton::clicked,
            this, &LyricsDock::onAddToSequence);
    connect(m_removeFromSeqBtn,&QPushButton::clicked,
            this, &LyricsDock::onRemoveFromSequence);
    connect(m_seqUpBtn,        &QPushButton::clicked,
            this, &LyricsDock::onMoveSequenceUp);
    connect(m_seqDownBtn,      &QPushButton::clicked,
            this, &LyricsDock::onMoveSequenceDown);
    connect(m_seqEditBtn,      &QPushButton::clicked,
            this, &LyricsDock::onSequenceEditSong);
    connect(m_sequenceList, &QListWidget::currentRowChanged,
            this, &LyricsDock::onSequenceSongSelected);
    connect(m_prevBtn,    &QPushButton::clicked,
            this, &LyricsDock::onPrevSlide);
    connect(m_nextBtn,    &QPushButton::clicked,
            this, &LyricsDock::onNextSlide);
    connect(m_blackoutBtn,&QPushButton::clicked,
            this, &LyricsDock::onBlackoutClicked);
    connect(m_zoomInBtn,  &QToolButton::clicked,
            this, &LyricsDock::onZoomIn);
    connect(m_zoomOutBtn, &QToolButton::clicked,
            this, &LyricsDock::onZoomOut);
    connect(m_zoomSlider, &QSlider::valueChanged,
            this, &LyricsDock::onZoomSlider);
}

// ── Helpers ───────────────────────────────────────────────────

// Compute the global slide offset for the currently selected song.
// This is the sum of all slide counts for songs BEFORE the current
// row in the sequence. Computed fresh each time to avoid stale-cache bugs.
int LyricsDock::computeGlobalSlideOffset(int forRow) const
{
    if (forRow <= 0) return 0; // first song or no selection — offset is 0
    int offset = 0;
    for (int r = 0; r < forRow; ++r) {
        if (r >= m_sequenceSongIds.size()) break;
        auto *s = SongManager::instance().findById(m_sequenceSongIds[r]);
        if (!s) continue;
        for (const auto &sec : s->sections)
            offset += sec.slides().size();
    }
    return offset;
}

QString LyricsDock::sequenceCurrentSongId() const
{
    int row = m_sequenceList->currentRow();
    if (row < 0 || row >= m_sequenceSongIds.size()) return {};
    return m_sequenceSongIds[row];
}

void LyricsDock::updateSequenceItem(int row, const Song &song)
{
    if (row < 0 || row >= m_sequenceList->count()) return;
    QString d = song.title;
    if (!song.artist.isEmpty()) d += "  \u2014  " + song.artist;
    m_sequenceList->item(row)->setText(d);
    m_sequenceList->item(row)->setData(Qt::UserRole, song.id);
}

void LyricsDock::removeSequenceRow(int row)
{
    if (row < 0 || row >= m_sequenceSongIds.size()) return;
    delete m_sequenceList->takeItem(row);
    m_sequenceSongIds.removeAt(row);
    clearSlideGrid();
    saveState();
}

void LyricsDock::applyZoomToAll()
{
    for (auto *b : m_slideButtons)
        b->applyScale(m_slideScale);
    m_slideContainer->adjustSize();
}

// ── Slide grid ────────────────────────────────────────────────

void LyricsDock::clearSlideGrid()
{
    for (auto *b : m_slideButtons) b->deleteLater();
    m_slideButtons.clear();
    m_slides.clear();
    m_slideSectionIndices.clear();
    m_activeSlide = -1;
    updateSlideCounter();
}

void LyricsDock::loadSongSlides(const Song &song)
{
    clearSlideGrid();
    const int cols = 3;
    int row = 0, col = 0, globalIdx = 0;

    for (int si = 0; si < song.sections.size(); ++si) {
        const auto &sec = song.sections[si];
        QString lbl = sec.label.isEmpty()
                      ? sectionTypeLabel(sec.type) : sec.label;
        for (const auto &slideText : sec.slides()) {
            m_slides              << qMakePair(lbl, slideText);
            m_slideSectionIndices << si;
            auto *btn = new SlideButton(
                lbl, slideText, globalIdx, si, m_slideContainer);
            connect(btn, &SlideButton::clicked,
                    this, &LyricsDock::onSlideClicked);
            connect(btn, &SlideButton::editRequested,
                    this, &LyricsDock::onSlideEditRequested);
            m_slideGrid->addWidget(btn, row, col);
            m_slideButtons << btn;
            if (++col >= cols) { col = 0; ++row; }
            ++globalIdx;
        }
    }
    applyZoomToAll();
    updateSlideCounter();
}

void LyricsDock::setActiveSlide(int index)
{
    for (auto *b : m_slideButtons)
        b->setActive(b->slideIndex() == index);
    m_activeSlide = index;
    applyZoomToAll();
    updateSlideCounter();

    // Scroll so the activated card is fully visible (entire card, not just edge)
    for (auto *b : m_slideButtons) {
        if (b->slideIndex() == index) {
            // 4px margin so the card doesn't sit flush against viewport edge
            m_slideArea->ensureWidgetVisible(b, 4, 4);
            break;
        }
    }
}

void LyricsDock::updateSlideCounter()
{
    m_slideCounter->setText(
        m_slides.isEmpty()
            ? QString::fromUtf8("\u2014")
            : m_activeSlide >= 0
                ? QString("%1/%2")
                    .arg(m_activeSlide + 1).arg(m_slides.size())
                : QString::fromUtf8("\u2014/%1")
                    .arg(m_slides.size()));
}

void LyricsDock::pushCurrentSlide()
{
    if (m_activeSlide < 0 || m_activeSlide >= m_slides.size()) return;
    const QString &lyrics  = m_slides[m_activeSlide].second;
    const QString &section = m_slides[m_activeSlide].first;
    const QString &title   = m_currentSong.title;
    lyrics_renderer_push(lyrics, section, title);
    // Broadcast global slide index so browser highlights the correct tile.
    // Use cached m_currentSongSlideOffset (set in onSequenceSongSelected)
    // instead of re-computing — avoids any timing mismatch with currentRow().
    if (m_wsServer) {
        int globalIdx = m_currentSongSlideOffset + m_activeSlide;
        m_wsServer->broadcastSlide(lyrics, section, title, globalIdx);
    }
}

// ── Show Sequence slots ───────────────────────────────────────

void LyricsDock::onAddToSequence()
{
    SongLibraryPopup popup(this);
    popup.setSequenceIds(m_sequenceSongIds);
    popup.exec();

    // Apply library deletions first (also removes from sequence)
    QList<QString> toDelete = popup.deletedFromLibrary();
    for (int i = m_sequenceSongIds.size() - 1; i >= 0; --i) {
        if (toDelete.contains(m_sequenceSongIds[i]))
            removeSequenceRow(i);
    }

    // Apply sequence removals
    QList<QString> toRemove = popup.removedIds();
    for (int i = m_sequenceSongIds.size() - 1; i >= 0; --i) {
        if (toRemove.contains(m_sequenceSongIds[i]))
            removeSequenceRow(i);
    }

    // Then apply additions
    for (const QString &id : popup.addedIds()) {
        if (m_sequenceSongIds.contains(id)) continue;
        auto *song = SongManager::instance().findById(id);
        if (!song) continue;
        m_sequenceSongIds << id;
        QString display = song->title;
        if (!song->artist.isEmpty())
            display += "  \u2014  " + song->artist;
        auto *item = new QListWidgetItem(display);
        item->setData(Qt::UserRole, id);
        m_sequenceList->addItem(item);
    }
    saveState();
    broadcastSequence();

    // Force reload slides for currently selected row
    // (row may not have changed numerically but song may have been removed above)
    onSequenceSongSelected(m_sequenceList->currentRow());
}

void LyricsDock::onRemoveFromSequence()
{
    int row = m_sequenceList->currentRow();
    removeSequenceRow(row);
    // Force update of slides for the new selection
    int newRow = m_sequenceList->currentRow();
    onSequenceSongSelected(newRow);
}

void LyricsDock::onMoveSequenceUp()
{
    int row = m_sequenceList->currentRow();
    if (row <= 0) return;
    auto *item = m_sequenceList->takeItem(row);
    m_sequenceList->insertItem(row - 1, item);
    m_sequenceList->setCurrentRow(row - 1);
    m_sequenceSongIds.swapItemsAt(row, row - 1);
    saveState();
    broadcastSequence();
}

void LyricsDock::onMoveSequenceDown()
{
    int row = m_sequenceList->currentRow();
    if (row < 0 || row >= m_sequenceSongIds.size() - 1) return;
    auto *item = m_sequenceList->takeItem(row);
    m_sequenceList->insertItem(row + 1, item);
    m_sequenceList->setCurrentRow(row + 1);
    m_sequenceSongIds.swapItemsAt(row, row + 1);
    saveState();
    broadcastSequence();
}

void LyricsDock::onSequenceSongSelected(int row)
{
    bool has = (row >= 0 && row < m_sequenceSongIds.size());
    m_removeFromSeqBtn->setEnabled(has);
    m_seqUpBtn->setEnabled(has && row > 0);
    m_seqDownBtn->setEnabled(has && row < m_sequenceSongIds.size() - 1);
    m_seqEditBtn->setEnabled(has);

    if (!has) { clearSlideGrid(); return; }
    auto *song = SongManager::instance().findById(m_sequenceSongIds[row]);
    if (!song) { clearSlideGrid(); return; }

    // Compute global slide offset: sum of all slide counts for songs before this row.
    // Pass row explicitly — do NOT rely on m_sequenceList->currentRow() which may
    // be stale during signal dispatch or when called manually (e.g. after add/remove).
    m_currentSongSlideOffset = computeGlobalSlideOffset(row);

    m_currentSong = *song;
    loadSongSlides(m_currentSong);
}

void LyricsDock::onSequenceEditSong()
{
    int    row = m_sequenceList->currentRow();
    QString id = sequenceCurrentSongId();
    if (id.isEmpty()) return;
    auto *song = SongManager::instance().findById(id);
    if (!song) return;

    SongEditDialog dlg(*song, this);
    if (dlg.exec() != QDialog::Accepted) return;

    Song res = dlg.result();
    if (dlg.savedAsNew()) {
        SongManager::instance().addSong(res);
        m_sequenceSongIds[row] = res.id;
        updateSequenceItem(row, res);
    } else {
        SongManager::instance().updateSong(res);
        updateSequenceItem(row, res);
    }
    if (auto *u = SongManager::instance().findById(res.id)) {
        m_currentSong = *u;
        loadSongSlides(m_currentSong);
    }
    saveState();
    broadcastSequence(); // update browser's global slide indices
}

// ── Slide slots ───────────────────────────────────────────────

void LyricsDock::onSlideClicked(int index)
{
    setActiveSlide(index);
    pushCurrentSlide();
}

void LyricsDock::onSlideEditRequested(int sectionIndex)
{
    if (m_currentSong.id.isEmpty()) return;
    auto *song = SongManager::instance().findById(m_currentSong.id);
    if (!song) return;
    QString oldId = song->id;

    SongEditDialog dlg(*song, this, sectionIndex);
    if (dlg.exec() != QDialog::Accepted) return;

    Song res = dlg.result();
    if (dlg.savedAsNew()) {
        SongManager::instance().addSong(res);
        for (int i = 0; i < m_sequenceSongIds.size(); ++i) {
            if (m_sequenceSongIds[i] == oldId) {
                m_sequenceSongIds[i] = res.id;
                updateSequenceItem(i, res);
            }
        }
        saveState();
    } else {
        SongManager::instance().updateSong(res);
        for (int i = 0; i < m_sequenceSongIds.size(); ++i) {
            if (m_sequenceSongIds[i] == res.id)
                updateSequenceItem(i, res);
        }
    }
    if (auto *u = SongManager::instance().findById(res.id)) {
        m_currentSong = *u;
        loadSongSlides(m_currentSong);
    }
    broadcastSequence(); // update browser's global slide indices
}void LyricsDock::onBlackoutClicked()
{
    for (auto *b : m_slideButtons) b->setActive(false);
    m_activeSlide = -1;
    applyZoomToAll();
    updateSlideCounter();
    lyrics_renderer_clear();
    if (m_wsServer) m_wsServer->broadcastClear();
}

void LyricsDock::onNextSlide()
{
    if (m_slides.isEmpty()) return;
    int next = (m_activeSlide < 0) ? 0 : m_activeSlide + 1;
    if (next >= m_slides.size()) return;
    setActiveSlide(next);
    pushCurrentSlide();
}

void LyricsDock::onPrevSlide()
{
    if (m_slides.isEmpty() || m_activeSlide <= 0) return;
    setActiveSlide(m_activeSlide - 1);
    pushCurrentSlide();
}

void LyricsDock::onZoomIn()
{
    m_zoomSlider->setValue(qMin(8, m_zoomSlider->value() + 1));
}

void LyricsDock::onZoomOut()
{
    m_zoomSlider->setValue(qMax(1, m_zoomSlider->value() - 1));
}

void LyricsDock::onZoomSlider(int value)
{
    m_slideScale = value * 0.25;
    m_zoomLabel->setText(
        QString("%1%").arg((int)(m_slideScale * 100)));
    applyZoomToAll();
}
