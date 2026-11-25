// lyrics-dock.cpp  v12.0
#include "lyrics-dock.hpp"
#include "audio-bin.hpp"
#include "layout-dialog.hpp"
#include "song-edit-dialog.hpp"
#include "media-gallery.hpp"
#include "overlay-tab.hpp"
#include "color-preset-button.hpp"
#include "icon-helpers.hpp"

#include "lyrics-renderer.hpp"

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
#include <QColorDialog>
#include <QScrollBar>
#include <QInputDialog>
#include <QSpinBox>
#include <QTimer>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QFileDialog>
#include <QUuid>
#include <QCryptographicHash>
#include <QApplication>
#include <vector>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QImageReader>
#include <QPlainTextEdit>

static bool isVideoFile(const QString &path) {
    static const QStringList v = {"mp4","mov","avi","mkv","webm","flv","wmv","m4v"};
    return v.contains(QFileInfo(path).suffix().toLower());
}

// ── MediaSlideCard — used in live mode for Media segments ─
class MediaSlideCard : public QFrame {
    Q_OBJECT
public:
    MediaSlideCard(const QString &path, int segIdx, int fileIdx, QWidget *parent = nullptr)
        : QFrame(parent), m_segIdx(segIdx), m_fileIdx(fileIdx), m_path(path)
        , m_isPicture(!isVideoFile(path))
    {
        setFrameShape(QFrame::StyledPanel);
        setBaseStyle(false);

        auto *L = new QVBoxLayout(this);
        L->setContentsMargins(6, 6, 6, 6);
        L->setSpacing(4);

        // Thumbnail
        m_thumb = new QLabel;
        m_thumb->setAlignment(Qt::AlignCenter);
        m_thumb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_thumb->setMinimumHeight(40);
        m_thumb->setMaximumHeight(90);

        if (m_isPicture) {
            // Load at large size; resizeEvent will scale to fill card height
            m_srcPixmap = MediaGallery::generateThumbnail(path, 400, 400, false);
            if (m_srcPixmap.isNull()) {
                m_thumb->setText("[ IMG ]");
                m_thumb->setStyleSheet("color:#888;font-size:11px;");
            } else {
                m_thumb->setPixmap(m_srcPixmap);
            }
        } else {
            QPixmap thumb = MediaGallery::generateThumbnail(path, 140, 80, false);
            if (!thumb.isNull())
                m_thumb->setPixmap(thumb);
            else {
                m_thumb->setText("[ VIDEO ]");
                m_thumb->setStyleSheet("color:#888;font-size:11px;");
            }
        }

        // Filename label
        QFileInfo fi(path);
        m_nameLbl = new QLabel(fi.fileName());
        m_nameLbl->setWordWrap(false);
        m_nameLbl->setAlignment(Qt::AlignCenter);
        m_nameLbl->setStyleSheet("color:#ddd;font-size:11px;font-weight:bold;");
        m_nameLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        L->addWidget(m_thumb, 1);
        L->addWidget(m_nameLbl);

        if (!m_isPicture) {
            // Button row: [⏮ Rewind]  [▶ Play / ⏸ Pause]  — video only
            auto *btnRow = new QHBoxLayout;
            btnRow->setSpacing(4);

            const QString btnBase =
                "QPushButton{background:#1e2a1e;color:#3dd68c;border:1px solid #2a6a2a;"
                "border-radius:4px;font-size:11px;padding:3px 8px;}"
                "QPushButton:hover{background:#2a4a2a;}"
                "QPushButton:pressed{background:#1a3a1a;}";

            m_rewindBtn = new QPushButton(QString::fromUtf8("⏮"));
            m_rewindBtn->setFixedHeight(26);
            m_rewindBtn->setFixedWidth(34);
            m_rewindBtn->setToolTip("Rewind to start");
            m_rewindBtn->setStyleSheet(btnBase);

            m_playBtn = new QPushButton(QString::fromUtf8("▶  Play"));
            m_playBtn->setFixedHeight(26);
            m_playBtn->setStyleSheet(btnBase);

            btnRow->addWidget(m_rewindBtn);
            btnRow->addWidget(m_playBtn, 1);
            L->addLayout(btnRow);

            connect(m_rewindBtn, &QPushButton::clicked, this, [this](){ emit rewindClicked(m_segIdx, m_fileIdx); });
            connect(m_playBtn,   &QPushButton::clicked, this, [this](){ emit playPauseClicked(m_segIdx, m_fileIdx); });
        }
    }

    void setPlaying(bool active, bool paused)
    {
        setBaseStyle(active);
        if (m_isPicture) return;
        if (active && !paused)
            m_playBtn->setText(QString::fromUtf8("⏸  Pause"));
        else if (active && paused)
            m_playBtn->setText(QString::fromUtf8("▶  Resume"));
        else
            m_playBtn->setText(QString::fromUtf8("▶  Play"));
    }

    int segIdx()  const { return m_segIdx; }
    int fileIdx() const { return m_fileIdx; }
    const QString &path() const { return m_path; }

signals:
    void clicked(int segIdx, int fileIdx);
    void playPauseClicked(int segIdx, int fileIdx);
    void rewindClicked(int segIdx, int fileIdx);

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) emit clicked(m_segIdx, m_fileIdx);
        QFrame::mousePressEvent(e);
    }
    void resizeEvent(QResizeEvent *e) override {
        QFrame::resizeEvent(e);
        if (m_isPicture && !m_srcPixmap.isNull() && m_thumb) {
            QSize sz = m_thumb->size();
            if (sz.width() > 0 && sz.height() > 0)
                m_thumb->setPixmap(m_srcPixmap.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

private:
    void setBaseStyle(bool active) {
        setStyleSheet(active
            ? "MediaSlideCard{background:#1e3a1e;border:2px solid #f59e0b;border-radius:6px;}"
            : "MediaSlideCard{background:#242424;border:1px solid #444;border-radius:6px;}"
              "MediaSlideCard:hover{border-color:#888;background:#2e2e2e;}");
    }
    int     m_segIdx, m_fileIdx;
    QString m_path;
    bool    m_isPicture  = false;
    QPixmap m_srcPixmap;               // original full-res thumb for picture rescaling
    QLabel      *m_thumb     = nullptr;
    QLabel      *m_nameLbl   = nullptr;
    QPushButton *m_rewindBtn = nullptr;
    QPushButton *m_playBtn   = nullptr;
};

// ── PptLiveCard — used in live mode for Presentation segments ─
class PptLiveCard : public QFrame {
    Q_OBJECT
public:
    PptLiveCard(const QString &title, int segIdx, QWidget *parent = nullptr)
        : QFrame(parent), m_segIdx(segIdx)
    {
        setCursor(Qt::PointingHandCursor);
        setFrameShape(QFrame::StyledPanel);
        applyStyle(false);

        auto *L = new QVBoxLayout(this);
        L->setContentsMargins(6, 8, 6, 8);
        L->setSpacing(5);

        auto *iconLbl = new QLabel(QString::fromUtf8("📽"));
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setStyleSheet("font-size:26px;");

        auto *titleLbl = new QLabel(title.isEmpty() ? "(Presentation)" : title);
        titleLbl->setWordWrap(true);
        titleLbl->setAlignment(Qt::AlignCenter);
        titleLbl->setStyleSheet("color:#e0c0ff;font-size:11px;font-weight:bold;");

        m_statusLbl = new QLabel("Click to show on OBS");
        m_statusLbl->setAlignment(Qt::AlignCenter);
        m_statusLbl->setStyleSheet("color:#888;font-size:9px;");

        const QString btnSS =
            "QPushButton{background:#2a1a3a;color:#c084fc;border:1px solid #6a3a8a;"
            "border-radius:4px;font-size:11px;padding:4px 8px;}"
            "QPushButton:hover{background:#3a2a4a;}"
            "QPushButton:pressed{background:#1a0a2a;}";

        auto *btnRow = new QHBoxLayout;
        btnRow->setSpacing(6);
        m_prevBtn = new QPushButton(QString::fromUtf8("◀ Prev"));
        m_nextBtn = new QPushButton(QString::fromUtf8("Next ▶"));
        m_prevBtn->setFixedHeight(28);
        m_nextBtn->setFixedHeight(28);
        m_prevBtn->setStyleSheet(btnSS);
        m_nextBtn->setStyleSheet(btnSS);
        btnRow->addWidget(m_prevBtn);
        btnRow->addWidget(m_nextBtn);

        L->addWidget(iconLbl);
        L->addWidget(titleLbl);
        L->addWidget(m_statusLbl);
        L->addLayout(btnRow);

        connect(m_prevBtn, &QPushButton::clicked, this, [this](){ emit prevRequested(m_segIdx); });
        connect(m_nextBtn, &QPushButton::clicked, this, [this](){ emit nextRequested(m_segIdx); });
    }

    void setActive(bool active) {
        applyStyle(active);
        m_statusLbl->setText(active ? "Showing on OBS  (click to stop)" : "Click to show on OBS");
        m_statusLbl->setStyleSheet(active
            ? "color:#a78bfa;font-size:9px;"
            : "color:#888;font-size:9px;");
    }

    int segIdx() const { return m_segIdx; }

signals:
    void activateRequested(int segIdx);
    void prevRequested(int segIdx);
    void nextRequested(int segIdx);

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) emit activateRequested(m_segIdx);
        QFrame::mousePressEvent(e);
    }

private:
    void applyStyle(bool active) {
        setStyleSheet(active
            ? "PptLiveCard{background:#1e1a3a;border:2px solid #a78bfa;border-radius:6px;}"
            : "PptLiveCard{background:#242424;border:1px solid #444;border-radius:6px;}"
              "PptLiveCard:hover{border-color:#c084fc;background:#2e2e2e;}");
    }
    int m_segIdx;
    QLabel      *m_statusLbl = nullptr;
    QPushButton *m_prevBtn = nullptr, *m_nextBtn = nullptr;
};

// ── Section colour palette ("Turn a word into color") ─────────
static const char *kVerseColors[5]  = {"#F3F5E5","#DDE8A1","#CFE35F","#B3CF17","#86EA57"};
static const char *kChorusColors[5] = {"#C9B8DB","#A175CC","#7D37C3","#4F1B83","#CD2DC8"};
static const char *kBridgeColors[5] = {"#EAEEF0","#B3C9D5","#7CACC5","#3B84AB","#747FCD"};
static const char *kOthersColors[5] = {"#929FD3","#4862CB","#233EA9","#0D1F63","#4D1BB1"};

// Parse "Verse 3" → type="verse", num=3.  No trailing digit → num=0 (no-number slot).
static void parseSectionName(const QString &label, QString &outType, int &outNum)
{
    QString lo = label.trimmed().toLower();
    static QRegularExpression re(R"(^(.*?)\s*(\d+)\s*$)");
    auto m = re.match(lo);
    if (m.hasMatch() && !m.captured(1).trimmed().isEmpty()) {
        outType = m.captured(1).trimmed();
        outNum  = m.captured(2).toInt(); // 1-based; 0 reserved for no-number
    } else {
        outType = lo;
        outNum  = 0; // no trailing number
    }
}

// Resolve the section header background colour from the section label.
// Column mapping: num=0 → index 0 (no number), num=1 → index 1, …, num=4 → index 4.
// num >= 5 → white.  othersMap maps unknown type-strings to a 0-based Others slot.
static QColor resolveSectionColor(const QString &sectionLabel,
                                  const QMap<QString,int> &othersMap)
{
    QString type; int num;
    parseSectionName(sectionLabel, type, num);

    const char **pal = nullptr;
    if (type.contains("verse"))       pal = kVerseColors;
    else if (type.contains("chorus")) pal = kChorusColors;
    else if (type.contains("bridge")) pal = kBridgeColors;

    if (pal) {
        if (num >= 0 && num <= 4) return QColor(pal[num]);
        return Qt::white;
    }

    auto it = othersMap.find(type);
    if (it != othersMap.end() && it.value() < 5)
        return QColor(kOthersColors[it.value()]);
    return Qt::white;
}

// ── Shared button-size constants ──────────────────────────────
static constexpr int kBtnH    = 22;
static constexpr int kBtnW    = 68;
static constexpr int kHdrBtnH = 38;

// ══════════════════════════════════════════════════════════════
// SongLibraryPopup
// ══════════════════════════════════════════════════════════════

SongLibraryPopup::SongLibraryPopup(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Song Library");
    setMinimumSize(800, 540);

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(8);

    // Left panel — 30% width: search + song list + action buttons
    auto *leftPanel  = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("Search songs...");
    m_searchEdit->setClearButtonEnabled(true);

    m_list = new QListWidget;
    m_list->setStyleSheet(
        "QListWidget { background: #1e1e1e; }"
        "QListWidget::item:selected { background: #1e6bb8; }");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *btnRow    = new QHBoxLayout;
    m_addBtn    = new QPushButton("Add");
    m_removeBtn = new QPushButton("Remove");
    m_deleteBtn = new QPushButton("Delete");
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

    // Right panel — 70% width: lyrics preview
    auto *rightPanel  = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto *previewLbl = new QLabel("Lyrics Preview:");
    previewLbl->setStyleSheet("color: #888;");

    m_lyricsPreview = new QTextEdit;
    m_lyricsPreview->setReadOnly(true);
    m_lyricsPreview->setStyleSheet(
        "QTextEdit { background: #141414; color: #bbb; border: 1px solid #333; }");
    m_lyricsPreview->setPlaceholderText("Click a song to preview...");

    rightLayout->addWidget(previewLbl);
    rightLayout->addWidget(m_lyricsPreview, 1);

    // 30 / 70 split
    mainLayout->addWidget(leftPanel,  3);
    mainLayout->addWidget(rightPanel, 7);

    refreshList();

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &SongLibraryPopup::onSearch);
    connect(m_list, &QListWidget::currentRowChanged,
            this, [this](int){ onSelectionChanged(); });
    // Double-click toggles: add if not in sequence, remove if already in sequence.
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, [this](){
                auto *item = m_list->currentItem();
                if (!item) return;
                QString id = item->data(Qt::UserRole).toString();
                if (id.isEmpty()) return;
                if (m_sequenceIds.contains(id)) onRemove();
                else                            onAdd();
            });
    connect(m_addBtn,   &QPushButton::clicked, this, &SongLibraryPopup::onAdd);
    connect(m_removeBtn,&QPushButton::clicked, this, &SongLibraryPopup::onRemove);
    connect(m_deleteBtn,&QPushButton::clicked, this, &SongLibraryPopup::onDeleteSong);
    connect(newBtn,     &QPushButton::clicked, this, &SongLibraryPopup::onAddNewSong);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::accept);

    // Right-click context menu on song list
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        auto *item = m_list->itemAt(pos);
        if (!item) return;
        m_list->setCurrentItem(item);

        QString id = item->data(Qt::UserRole).toString();
        bool inSeq = m_sequenceIds.contains(id);

        QMenu menu(m_list);
        auto *addAct    = menu.addAction(
            style()->standardIcon(QStyle::SP_DialogApplyButton), "Add to Sequence");
        auto *removeAct = menu.addAction(
            style()->standardIcon(QStyle::SP_TrashIcon), "Remove from Sequence");
        menu.addSeparator();
        auto *editAct   = menu.addAction(
            makePenIcon(), "Edit Song");

        addAct->setEnabled(!inSeq);
        removeAct->setEnabled(inSeq);

        auto *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
        if (chosen == addAct) {
            onAdd();
        } else if (chosen == removeAct) {
            onRemove();
        } else if (chosen == editAct) {
            auto *song = SongManager::instance().findById(id);
            if (!song) return;
            SongEditDialog dlg(*song, this);
            if (dlg.exec() != QDialog::Accepted) return;
            Song res = dlg.result();
            if (dlg.savedAsNew())
                SongManager::instance().addSong(res);
            else
                SongManager::instance().updateSong(res);
            refreshList();
        }
    });
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
                "This cannot be undone. Segments already in the service keep "
                "their own copy of the lyrics.").arg(song->title),
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
// ── BgPickerDialog ────────────────────────────────────────────
// Shows a thumbnail grid of the media gallery so the user can
// pick a background for a section or whole song.
BgPickerDialog::BgPickerDialog(const QStringList &mediaPaths,
                               const QString &currentPath,
                               QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Choose Background");
    setMinimumWidth(480);
    setMinimumHeight(360);

    auto *vlay = new QVBoxLayout(this);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *container = new QWidget;
    auto *grid = new QGridLayout(container);
    grid->setSpacing(6);
    scroll->setWidget(container);
    vlay->addWidget(scroll, 1);

    // "No background" tile
    auto *noneBtn = new QPushButton("✕  No background");
    noneBtn->setFixedHeight(40);
    noneBtn->setCheckable(true);
    noneBtn->setChecked(currentPath.isEmpty());
    noneBtn->setStyleSheet(
        "QPushButton{background:#333;color:#ccc;border:1px solid #555;border-radius:3px;}"
        "QPushButton:checked{border:2px solid #4da6ff;color:#fff;}");
    grid->addWidget(noneBtn, 0, 0, 1, 3);

    connect(noneBtn, &QPushButton::clicked, this, [this](){
        m_selected = QString();
        accept();
    });

    // Media thumbnail tiles (3 per row)
    static const int TW = 140, TH = 90;
    int row = 1, col = 0;
    for (const QString &path : mediaPaths) {
        QPixmap px;
        bool isVideo = path.endsWith(".mp4", Qt::CaseInsensitive) ||
                       path.endsWith(".mov", Qt::CaseInsensitive) ||
                       path.endsWith(".avi", Qt::CaseInsensitive) ||
                       path.endsWith(".mkv", Qt::CaseInsensitive);
        if (!isVideo) {
            px = QPixmap(path).scaled(TW, TH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                              .copy(0, 0, TW, TH);
        }

        auto *btn = new QPushButton;
        btn->setFixedSize(TW + 4, TH + 22);
        btn->setCheckable(true);
        btn->setChecked(path == currentPath);
        btn->setStyleSheet(
            "QPushButton{background:#222;border:1px solid #555;border-radius:3px;}"
            "QPushButton:checked{border:2px solid #4da6ff;}"
            "QPushButton:hover{border:1px solid #aaa;}");

        auto *inner = new QVBoxLayout(btn);
        inner->setContentsMargins(2, 2, 2, 2);
        inner->setSpacing(2);

        auto *thumbLbl = new QLabel;
        thumbLbl->setFixedSize(TW, TH);
        thumbLbl->setAlignment(Qt::AlignCenter);
        if (!px.isNull())
            thumbLbl->setPixmap(px);
        else
            thumbLbl->setText(isVideo ? "🎬" : "?");
        thumbLbl->setStyleSheet("color:#aaa; font-size:28px; background:transparent;");
        inner->addWidget(thumbLbl);

        auto *nameLbl = new QLabel(QFileInfo(path).fileName());
        nameLbl->setAlignment(Qt::AlignCenter);
        nameLbl->setStyleSheet("color:#ccc; font-size:10px; background:transparent;");
        nameLbl->setWordWrap(false);
        inner->addWidget(nameLbl);

        // Pass path by value into lambda
        connect(btn, &QPushButton::clicked, this, [this, path](){
            m_selected = path;
            accept();
        });

        grid->addWidget(btn, row, col);
        col++;
        if (col >= 3) { col = 0; row++; }
    }

    // Cancel button
    auto *cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    vlay->addLayout(btnRow);
}

// SlideButton
// ══════════════════════════════════════════════════════════════

SlideButton::SlideButton(const QString &section, const QString &text,
                         int slideIndex, int sectionIndex, int slideNumber,
                         QWidget *parent)
    : QFrame(parent), m_slideIdx(slideIndex), m_sectionIdx(sectionIndex)
{
    setFixedSize(BASE_W, BASE_H);   // applyScale() will recompute after text is set
    setCursor(Qt::PointingHandCursor);
    setFrameShape(QFrame::StyledPanel);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 4, 6, 4);
    mainLayout->setSpacing(2);

    // Header row: [Section Name ←] [→ slide #] on one line
    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(4);

    m_sectionLbl = new QLabel(section);
    m_sectionLbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_slideNumLbl = new QLabel(QString::number(slideNumber));
    m_slideNumLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_slideNumLbl->setStyleSheet("color: #00ffff; font-weight: bold;");

    headerRow->addWidget(m_sectionLbl, 1);
    headerRow->addWidget(m_slideNumLbl);

    m_textLbl = new QLabel(text);
    m_textLbl->setWordWrap(true);
    m_textLbl->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    mainLayout->addLayout(headerRow);
    mainLayout->addWidget(m_textLbl, 1);

    setActive(false);
}

void SlideButton::applyScale(double scale, const QString &fontFamily, int fixedWidth)
{
    // Width: use the caller-supplied fixed pixel width when provided,
    // otherwise fall back to the legacy zoom-based formula.
    int w  = (fixedWidth > 0) ? fixedWidth : qMax(80, (int)(BASE_W * scale));
    int fs = qMax(7,  (int)(12 * scale));
    m_fontFamily = fontFamily.isEmpty() ? "Arial" : fontFamily;
    m_fontSize   = fs;
    QString ff    = m_fontFamily;
    QString tc    = m_active ? m_fontColor.name()
                             : QString("rgba(%1,%2,%3,180)")
                                   .arg(m_fontColor.red())
                                   .arg(m_fontColor.green())
                                   .arg(m_fontColor.blue());
    QString secBg = m_sectionColor.name();

    m_textLbl->setStyleSheet(
        QString("font-family: '%1'; font-size: %2px; font-weight: bold; color: %3; background: transparent;")
            .arg(ff).arg(fs).arg(tc));
    m_sectionLbl->setStyleSheet(
        QString("font-family: '%1'; font-size: %2px; font-weight: bold;"
                " background-color: %3; color: black; padding: 0 2px;")
            .arg(ff).arg(fs).arg(secBg));
    m_slideNumLbl->setStyleSheet(
        QString("font-family: '%1'; font-size: %2px; font-weight: bold; color: #00ffff;"
                " background: transparent;")
            .arg(ff).arg(fs));

    QFont font(ff);
    font.setPixelSize(fs);
    font.setBold(true);
    QFontMetrics fm(font);

    int innerW = qMax(1, w - 12); // 6 px left + 6 px right layout margins
    int secH   = fm.height() + 2; // section label: one line
    QRect textBr = fm.boundingRect(QRect(0, 0, innerW, 0),
                                   Qt::TextWordWrap | Qt::AlignLeft,
                                   m_textLbl->text());
    int textH  = qMax(fm.height(), textBr.height()) + 2;

    // Minimum height = 5 lines of body text + section label + padding.
    // This keeps short slides the same size as their neighbours.
    int minFiveLinesH = secH + 5 * (fm.height() + 2) + 10;
    // 4 top + 4 bottom margin + 2 spacing between labels = 10 px overhead
    int h = qMax(minFiveLinesH, secH + textH + 10);

    setFixedSize(w, h);
}

void SlideButton::setActive(bool active)
{
    m_active = active;
    bool hasBg = !m_bgThumb.isNull();

    // Widget background/border — transparent when image fills the card
    if (active) {
        setStyleSheet(hasBg
            ? "SlideButton { background: transparent; border: 2px solid #4da6ff; border-radius: 4px; }"
            : "SlideButton { background: #1e6bb8; border: 2px solid #4da6ff; border-radius: 4px; }");
    } else {
        setStyleSheet(hasBg
            ? "SlideButton { background: transparent; border: 1px solid #444; border-radius: 4px; }"
              "SlideButton:hover { border: 1px solid #888; }"
            : "SlideButton { background: #2a2a2a; border: 1px solid #444; border-radius: 4px; }"
              "SlideButton:hover { border: 1px solid #888; background: #333; }");
    }

    // Label stylesheets — use stored font/size from last applyScale call
    QString ff  = m_fontFamily;
    int     fs  = m_fontSize;
    QString tc  = m_fontColor.name();
    QString tcd = QString("rgba(%1,%2,%3,180)")
                      .arg(m_fontColor.red()).arg(m_fontColor.green()).arg(m_fontColor.blue());

    m_sectionLbl->setStyleSheet(
        QString("font-family: '%1'; font-size: %2px; font-weight: bold;"
                " background-color: %3; color: black; padding: 0 2px;")
            .arg(ff).arg(fs).arg(m_sectionColor.name()));
    m_textLbl->setStyleSheet(
        QString("font-family: '%1'; font-size: %2px; font-weight: bold; color: %3; background: transparent;")
            .arg(ff).arg(fs).arg(active ? tc : tcd));
    m_slideNumLbl->setStyleSheet(
        QString("font-family: '%1'; font-size: %2px; font-weight: bold; color: #00ffff;"
                " background: transparent;")
            .arg(ff).arg(fs));

    update();
}

void SlideButton::setFontColor(const QColor &c)
{
    m_fontColor = c.isValid() ? c : Qt::white;
    setActive(m_active);
}

void SlideButton::setSectionColor(const QColor &c)
{
    m_sectionColor = c.isValid() ? c : Qt::white;
    setActive(m_active); // re-apply stylesheet with new colour
}

void SlideButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        emit clicked(m_slideIdx);
    QFrame::mousePressEvent(e);
}

void SlideButton::setBackgroundThumb(const QString &path)
{
    m_bgPath = path;
    if (path.isEmpty()) {
        m_bgThumb = QPixmap();
    } else {
        // Use Windows Shell thumbnail (works for both images and videos)
        int tw = BASE_W * 2, th = BASE_H * 2;
        QPixmap px = MediaGallery::generateThumbnail(path, tw, th);
        if (!px.isNull()) {
            m_bgThumb = px;
        } else {
            // Fallback: dark placeholder with a play icon (video) or X (image fail)
            m_bgThumb = QPixmap(tw, th);
            m_bgThumb.fill(QColor(20, 20, 20));
            QPainter p(&m_bgThumb);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QColor(100, 100, 100));
            p.setFont(QFont("Arial", th / 8, QFont::Bold));
            p.drawText(m_bgThumb.rect(), Qt::AlignCenter, "?");
        }
    }
    // Refresh widget stylesheet (transparent when image is set)
    setActive(m_active);
    update();
}

void SlideButton::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    auto *editAct    = menu.addAction(makePenIcon(), "Edit...");
    menu.addSeparator();
    auto *bgCardAct  = menu.addAction(QString::fromUtf8("🖼  Apply current background to this slide card"));
    auto *bgSecAct   = menu.addAction(QString::fromUtf8("🖼  Apply current background to this segment (default)"));
    menu.addSeparator();
    auto *clrCardAct = menu.addAction(QString::fromUtf8("✕  Clear this slide card's own background"));
    auto *clrSecAct  = menu.addAction(QString::fromUtf8("✕  Clear this segment's default background"));
    clrCardAct->setEnabled(!m_bgPath.isEmpty());

    QAction *chosen = menu.exec(e->globalPos());
    if (!chosen) return;

    if      (chosen == editAct)    emit editRequested(m_sectionIdx);
    else if (chosen == bgCardAct)  emit bgAssignRequested(m_slideIdx, false);
    else if (chosen == bgSecAct)   emit bgAssignRequested(m_slideIdx, true);
    else if (chosen == clrCardAct) emit bgClearRequested(m_slideIdx, false);
    else if (chosen == clrSecAct)  emit bgClearRequested(m_slideIdx, true);
}

void SlideButton::paintEvent(QPaintEvent *e)
{
    if (!m_bgThumb.isNull()) {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        // Fill entire card with the background image (center-crop)
        QRect r = rect();
        QSize src = m_bgThumb.size();
        double scaleW = (double)r.width()  / src.width();
        double scaleH = (double)r.height() / src.height();
        double scale  = qMax(scaleW, scaleH);
        int dw = (int)(src.width()  * scale);
        int dh = (int)(src.height() * scale);
        int dx = (r.width()  - dw) / 2;
        int dy = (r.height() - dh) / 2;
        p.drawPixmap(dx, dy, dw, dh, m_bgThumb);

        // 80% transparent dark overlay (alpha = 51 ≈ 20% opacity)
        p.fillRect(r, QColor(0, 0, 0, 51));

        // Draw the QFrame border on top
        QFrame::paintEvent(e);
    } else {
        QFrame::paintEvent(e);
    }
}

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
// Build a Song from a Lyrics Segment using embedded sections.
// Falls back to the library lookup if no sections are embedded yet
// (backward compatibility for service.json files saved before this change).
// ── Slide cards: identity that survives edits ────────────────
// A segment's slide TEXT is derived from its sections / text.  Each slide has a SlideCard in
// Segment::cards that holds what must not change when the text changes: a permanent id and
// the card's own background.  syncCards() re-aligns the cards with the current text and keeps
// the id (and background) of every card that still exists.

static QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// Content fingerprint of one slide card (stable across runs).
static QString cardKey(const QString &section, const QString &text, const QString &pinyin,
                       const QString &english, bool chinese)
{
    const QChar sep(0x1f);
    const QString s = section + sep + text + sep + pinyin + sep + english + sep
                    + QString(chinese ? "1" : "0");
    return QString::fromLatin1(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Md5).toHex());
}

// Fingerprints of the slides a segment currently produces, in on-air order.
static QStringList cardKeysFor(const Segment &seg)
{
    QStringList keys;
    if (seg.type == Segment::Type::Lyrics) {
        Song s;
        s.sections = seg.sections;
        for (const auto &sl : s.allSlides())
            keys << cardKey(sl.section, sl.text, sl.pinyin, sl.english, sl.isChinese);
    } else if (seg.type == Segment::Type::Paragraphs) {
        for (const QString &t : seg.parseSlides())
            keys << cardKey(QString(), t, QString(), QString(), false);
    } else if (seg.type == Segment::Type::Media) {
        // One card per file; the file path is its fingerprint.
        for (const QString &p : seg.mediaPaths)
            keys << cardKey(p, QString(), QString(), QString(), false);
    }
    return keys;
}

// Align the stored cards (old) with the slides the text now produces (keys).
//  1. Cards whose content is unchanged are matched by a longest-common-subsequence over the
//     fingerprints, so inserting or deleting a slide does not disturb the others.
//  2. Between two matched cards, leftover old and new cards are paired in order: an EDITED
//     card keeps its id and background.
//  3. Surplus new slides get fresh ids; surplus old cards are dropped.
static QList<SlideCard> reconcileCards(const QList<SlideCard> &old, const QStringList &keys)
{
    const int n = old.size(), m = keys.size();
    std::vector<int> dp((size_t)(n + 1) * (size_t)(m + 1), 0);
    auto at = [&](int i, int j) -> int & { return dp[(size_t)i * (size_t)(m + 1) + (size_t)j]; };
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            at(i, j) = (old[i].key == keys[j]) ? at(i + 1, j + 1) + 1
                                               : qMax(at(i + 1, j), at(i, j + 1));

    QList<QPair<int,int>> matches;
    for (int i = 0, j = 0; i < n && j < m; ) {
        if (old[i].key == keys[j])             { matches << qMakePair(i, j); ++i; ++j; }
        else if (at(i + 1, j) >= at(i, j + 1)) ++i;
        else                                   ++j;
    }
    matches << qMakePair(n, m);   // sentinel closes the last gap

    QList<SlideCard> out;
    int pi = 0, pj = 0;
    for (const auto &mt : matches) {
        const int gapOld = mt.first - pi, gapNew = mt.second - pj;
        for (int k = 0; k < gapNew; ++k) {
            SlideCard c;
            if (k < gapOld) c = old[pi + k];   // edited card keeps id + background
            else            c.id = newId();    // genuinely new slide
            c.key = keys[pj + k];
            out << c;
        }
        if (mt.second < m) {                   // the matched (unchanged) card itself
            SlideCard c = old[mt.first];
            c.key = keys[mt.second];
            out << c;
        }
        pi = mt.first + 1;
        pj = mt.second + 1;
    }
    return out;
}

void Segment::syncCards()
{
    if (type != Type::Lyrics && type != Type::Paragraphs && type != Type::Media) {
        cards.clear();   // Presentation has no slide cards
        return;
    }
    const QStringList keys = cardKeysFor(*this);
    // Nothing to align yet (e.g. the sections are filled in from the library afterwards):
    // leave stored cards and legacy backgrounds untouched for the next sync.
    if (keys.isEmpty() && cards.isEmpty()) return;
    cards = reconcileCards(cards, keys);

    // Backgrounds stored by slide index in files from before slide-card ids ...
    for (auto it = legacySlideBgs.constBegin(); it != legacySlideBgs.constEnd(); ++it)
        if (it.key() >= 0 && it.key() < cards.size() && cards[it.key()].background.isEmpty())
            cards[it.key()].background = it.value();
    legacySlideBgs.clear();
    // ... and section-level backgrounds from before v12.27.0.
    int idx = 0;
    for (auto &sec : sections) {
        const int n = sec.isChinese ? sec.chineseSlides().size() : sec.slides().size();
        if (!sec.legacyBackgroundPath.isEmpty())
            for (int i = 0; i < n && idx + i < cards.size(); ++i)
                if (cards[idx + i].background.isEmpty())
                    cards[idx + i].background = sec.legacyBackgroundPath;
        sec.legacyBackgroundPath.clear();
        idx += n;
    }
}

void Segment::remintIds()
{
    id = newId();
    for (auto &c : cards) c.id = newId();
}

// The library song a segment came from (falls back to the segment id for a segment that
// was never linked to the library).  This is the id the song edit dialog works with.
static QString songIdOf(const Segment &seg)
{
    return seg.sourceSongId.isEmpty() ? seg.id : seg.sourceSongId;
}

// Build a Song from a Lyrics Segment using its embedded sections.
// Falls back to the library only when no sections are embedded (very old service files).
static Song songFromItem(const Segment &item)
{
    if (!item.sections.isEmpty()) {
        Song s;
        s.id        = songIdOf(item);
        s.title     = item.title;
        s.artist    = item.artist;
        s.key       = item.songKey;
        s.tempo     = item.songTempo;
        s.copyright = item.songCopyright;
        s.sections  = item.sections;
        return s;
    }
    // Fallback: look up from library
    const Song *lib = SongManager::instance().findById(songIdOf(item));
    if (lib) return *lib;
    Song empty; empty.id = songIdOf(item); empty.title = item.title; empty.artist = item.artist;
    return empty;
}

// Each song has "slides" array matching exactly what loadSongSlides() builds.
QJsonArray LyricsDock::buildSequenceJson() const
{
    QJsonArray songs;
    int globalIdx = 0; // globally unique across ALL songs in sequence
    for (const auto &id : m_sequenceSongIds) {
        // Find the corresponding segment to use embedded sections
        const Segment *seg = nullptr;
        for (const auto &si : m_segments)
            if (si.type == Segment::Type::Lyrics && si.id == id) { seg = &si; break; }
        if (!seg) continue;
        Song song = songFromItem(*seg);
        if (song.allSlides().isEmpty()) continue;
        QJsonObject s;
        s["id"]     = song.id;
        s["title"]  = song.title;
        s["artist"] = song.artist;
        QJsonArray slides;
        for (const auto &sl : song.allSlides()) {
            QJsonObject slObj;
            slObj["label"]      = sl.section;
            slObj["text"]       = sl.text;
            slObj["slideIndex"] = globalIdx++;
            if (sl.isChinese) {
                slObj["isChinese"] = true;
                slObj["pinyin"]    = sl.pinyin;
                slObj["english"]   = sl.english;
            }
            slides.append(slObj);
        }
        s["slides"] = slides;
        songs.append(s);
    }
    return songs;
}

void LyricsDock::setWsServer(WebSocketServer *srv)
{
    m_wsServer = srv;
    if (m_audioBin) m_audioBin->setWsServer(srv);
    if (!srv) return;
    if (m_sharedGallery)
        srv->broadcastMediaList("shared", m_sharedGallery->itemPaths());
    // Populate WS cache so the first connecting manager gets full service order
    broadcastSequence();
}

void LyricsDock::audioBinPlayFromRemote(int index)
{
    if (m_audioBin) m_audioBin->playFileFromRemote(index);
    if (m_wsServer) m_wsServer->broadcastAudioBinState(index, true);
}

void LyricsDock::audioBinStopFromRemote()
{
    if (m_audioBin) m_audioBin->stopPlayback();
    // broadcastAudioBinState(-1,false) is called inside stopPlayback via m_wsServer
}

// On-disk service files list their segments under "segments" (v12.26.0+).
// Older files (and older remote exports) used "items"; always read both.
static QJsonArray segmentsArray(const QJsonObject &o)
{
    return o.contains("segments") ? o["segments"].toArray() : o["items"].toArray();
}

QJsonObject LyricsDock::buildFullServiceExport() const
{
    QJsonObject root;
    root["version"] = 2;

    root["linesPerSlide"] = m_linesPerSlideSpin ? m_linesPerSlideSpin->value() : 4;
    if (m_globalFadeSpin) root["globalFadeMs"] = m_globalFadeSpin->value();

    root["colorPresets"] = colorPresetsJson();

    // OBS source layout settings
    obs_source_t *src = lyrics_renderer_get_source();
    if (src) {
        obs_data_t *s = obs_source_get_settings(src);
        if (s) {
            root["sourceSettings"] = QString::fromUtf8(obs_data_get_json(s));
            obs_data_release(s);
        }
    }

    // Graphics Bin
    if (m_sharedGallery) {
        QJsonArray gArr;
        for (const QString &p : m_sharedGallery->itemPaths()) gArr.append(p);
        root["gallery"] = gArr;
    }

    // Audio Bin
    if (m_audioBin) {
        QJsonArray aArr;
        for (const QString &p : m_audioBin->files()) aArr.append(p);
        root["audioBin"] = aArr;
    }

    // All segments (Lyrics, Paragraphs, Media — not Presentation)
    QJsonArray items;
    for (const auto &item : m_segments) {
        if (item.type == Segment::Type::Presentation) continue;
        items.append(item.toJson());
    }
    root["segments"] = items;

    return root;
}

void LyricsDock::applyColorPresetsFromJson(const QJsonArray &arr)
{
    for (int i = 0; i < 6 && i < arr.size(); ++i) {
        if (!m_colorBtn[i]) continue;
        auto po = arr[i].toObject();
        uint32_t fc = (uint32_t)po["font"].toInteger(0x00FFFFFF);
        uint32_t oc = (uint32_t)po["outline"].toInteger(0x00000000);
        m_colorBtn[i]->setColors(
            QColor((fc >> 16) & 0xFF, (fc >> 8) & 0xFF, fc & 0xFF),
            QColor((oc >> 16) & 0xFF, (oc >> 8) & 0xFF, oc & 0xFF));
    }
}

void LyricsDock::applySourceSettingsFromJson(const QString &jsonStr)
{
    obs_source_t *src = lyrics_renderer_get_source();
    if (!src) return;
    obs_data_t *d = obs_data_create_from_json(jsonStr.toUtf8().constData());
    if (!d) return;
    obs_source_update(src, d);
    obs_data_release(d);
}

void LyricsDock::applyFullServiceImport(const QJsonObject &root, bool replaceLayout)
{
    int version = root["version"].toInt(1);

    if (replaceLayout) {
        if (root.contains("colorPresets"))
            applyColorPresetsFromJson(root["colorPresets"].toArray());

        if (root.contains("sourceSettings"))
            applySourceSettingsFromJson(root["sourceSettings"].toString());

        if (root["gallery"].isArray()) {
            QStringList paths;
            for (const auto &v : root["gallery"].toArray()) paths << v.toString();
            if (m_sharedGallery) m_sharedGallery->setItemPaths(paths);
        }

        if (root["audioBin"].isArray()) {
            QStringList paths;
            for (const auto &v : root["audioBin"].toArray()) paths << v.toString();
            if (m_audioBin) m_audioBin->setFiles(paths);
        }

        if (root.contains("linesPerSlide")) {
            int lpp = root["linesPerSlide"].toInt(4);
            setGlobalLinesPerSlide(lpp);
            if (m_linesPerSlideSpin) { QSignalBlocker b(m_linesPerSlideSpin); m_linesPerSlideSpin->setValue(lpp); }
        }

        if (root.contains("globalFadeMs") && m_globalFadeSpin) {
            int fms = root["globalFadeMs"].toInt(500);
            QSignalBlocker b(m_globalFadeSpin);
            m_globalFadeSpin->setValue(fms);
            if (m_sharedGallery) m_sharedGallery->setFadeDurationMs(fms);
        }
    }

    // Always replace segments (Lyrics, Paragraphs, Media)
    // Keep existing Presentation items as-is
    for (int i = m_segments.size() - 1; i >= 0; --i) {
        if (m_segments[i].type != Segment::Type::Presentation)
            m_segments.removeAt(i);
    }

    int insertAt = 0;
    m_sequenceSongIds.clear();
    for (const auto &v : segmentsArray(root)) {
        if (!v.isObject()) continue;
        Segment si = Segment::fromJson(v.toObject());
        if (si.type == Segment::Type::Presentation) continue;

        // Library fallback for Lyrics items with no embedded sections
        if (si.type == Segment::Type::Lyrics && si.sections.isEmpty()) {
            const Song *lib = SongManager::instance().findById(si.sourceSongId);
            if (lib) {
                si.sections = lib->sections;
                si.songKey = lib->key; si.songTempo = lib->tempo; si.songCopyright = lib->copyright;
                si.syncCards();
            }
        }

        if (version == 1) {
            // v1 format: strip backgrounds (weren't saved correctly)
            si.clearBackgrounds();
            si.colorPresetIdx = 0;
        }

        // An import always gets NEW segment and slide-card ids: the file's ids may already
        // exist in this service.
        si.remintIds();

        if (si.type == Segment::Type::Lyrics) m_sequenceSongIds << si.id;
        m_segments.insert(insertAt++, si);
    }

    m_activeSegIdx = -1;
    clearSlideGrid();
    rebuildSegmentList();
    saveServiceOrder();
    if (replaceLayout) saveState();
    broadcastSequence();
}

void LyricsDock::importSegmentsFromRemote(const QJsonArray &items)
{
    // Remove all existing Lyrics and Paragraphs items
    for (int i = m_segments.size() - 1; i >= 0; --i) {
        if (m_segments[i].type == Segment::Type::Lyrics ||
            m_segments[i].type == Segment::Type::Paragraphs) {
            m_segments.removeAt(i);
        }
    }

    // Insert imported items at position 0 (before media/presentation)
    int insertAt = 0;
    for (const auto &v : items) {
        if (!v.isObject()) continue;
        Segment si = Segment::fromJson(v.toObject());
        if (si.type != Segment::Type::Lyrics &&
            si.type != Segment::Type::Paragraphs) continue;
        // Library fallback for songs with no embedded sections
        if (si.type == Segment::Type::Lyrics && si.sections.isEmpty()) {
            const Song *lib = SongManager::instance().findById(si.sourceSongId);
            if (lib) {
                si.sections = lib->sections;
                si.songKey = lib->key; si.songTempo = lib->tempo; si.songCopyright = lib->copyright;
                si.syncCards();
            }
        }
        // Strip backgrounds to be safe
        si.clearBackgrounds();
        // Default color preset
        si.colorPresetIdx = 0;
        // An import always gets new segment and slide-card ids
        si.remintIds();
        m_segments.insert(insertAt++, si);
    }

    // Rebuild sequence cache
    m_sequenceSongIds.clear();
    for (const auto &si : m_segments)
        if (si.type == Segment::Type::Lyrics) m_sequenceSongIds << si.id;

    m_activeSegIdx = -1;
    clearSlideGrid();
    rebuildSegmentList();
    saveServiceOrder();
    broadcastSequence();
}

QJsonArray LyricsDock::buildServiceOrderJson() const
{
    QJsonArray arr;
    int songIdx = 0;
    for (int i = 0; i < m_segments.size(); ++i) {
        const auto &item = m_segments[i];
        QJsonObject o;
        o["idx"]  = i;
        o["type"] = item.typeLabel().toLower();
        o["id"]   = item.id;                     // segment id (identity in the queue)
        {   // the segment's card queue: one id per slide card, in on-air order
            QJsonArray cc;
            for (const auto &c : item.cards) cc.append(c.id);
            o["cards"] = cc;
        }
        if (item.type == Segment::Type::Lyrics) {
            o["title"]   = item.title;
            o["artist"]  = item.artist;
            o["songIdx"] = songIdx++;
        } else {
            o["title"]  = item.title;
            o["artist"] = QString{};
            if (item.type == Segment::Type::Paragraphs) {
                o["text"] = item.text;
                // Per-slide background overrides (index -> path): the wire format remote.html knows
                QJsonObject bgs;
                for (int ci = 0; ci < item.cards.size(); ++ci)
                    if (!item.cards[ci].background.isEmpty())
                        bgs[QString::number(ci)] = item.cards[ci].background;
                if (!bgs.isEmpty()) o["slideBgs"] = bgs;
            } else if (item.type == Segment::Type::Media) {
                QJsonArray ma;
                for (const auto &p : item.mediaPaths) ma.append(p);
                o["mediaPaths"] = ma;
            }
        }
        arr.append(o);
    }
    return arr;
}

void LyricsDock::broadcastSequence()
{
    if (!m_wsServer) return;
    m_wsServer->broadcastSongs(buildSequenceJson());
    m_wsServer->broadcastServiceOrder(buildServiceOrderJson());
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
        const Segment *si = nullptr;
        for (const auto &s : m_segments)
            if (s.type == Segment::Type::Lyrics && s.id == m_sequenceSongIds[row])
                { si = &s; break; }
        if (!si || si->title != songTitle) continue;

        // Find the service-item index for this song (may differ from row when
        // non-Lyrics items precede it in the service order).
        int segIdx = -1;
        for (int i = 0; i < m_segments.size(); ++i) {
            if (m_segments[i].type == Segment::Type::Lyrics
                    && m_segments[i].id == m_sequenceSongIds[row]) {
                segIdx = i;
                break;
            }
        }
        if (segIdx < 0) segIdx = row; // fallback: songs-only service order

        // Select the correct row in the service list and load its slides if needed.
        if (m_segmentList && m_segmentList->currentRow() != segIdx)
            m_segmentList->setCurrentRow(segIdx);

        if (m_liveMode) {
            // In live mode, loadSegmentContent calls clearSlideGrid() which destroys
            // the live button layout. Set up song state manually instead.
            if (m_currentSong.id != songIdOf(*si)) {
                Song song = songFromItem(*si);
                m_currentSong = song;
                m_slides      = song.allSlides();
            }
            m_currentSongSlideOffset = computeGlobalSlideOffset(row);
        } else {
            if (m_currentSong.id != songIdOf(*si))
                loadSegmentContent(segIdx);
        }

        // The slideIndex from the remote is GLOBAL across all songs.
        int localIdx = slideIndex - computeGlobalSlideOffset(row);

        // In live mode, slide buttons store GLOBAL indices — use global for highlight/bg lookup.
        // In non-live mode, buttons store LOCAL indices.
        if (m_liveMode) setActiveSlide(slideIndex);
        else            setActiveSlide(localIdx);
        applySlideBackground();
        // Restore local index (consistent with onSlideClicked live mode behaviour).
        if (m_liveMode) m_activeSlide = localIdx;
        rememberOnAir(*si, localIdx);
        applySegmentPreset(*si);   // the preset belongs to this segment, applied with its slide
        if (localIdx >= 0 && localIdx < m_slides.size()) {
            const LyricsSlide &sl = m_slides[localIdx];
            if (sl.isChinese)
                lyrics_renderer_push_chinese(sl.text, sl.pinyin, sl.english,
                                             section, songTitle);
            else
                lyrics_renderer_push(sl.text, section, songTitle);
        } else {
            lyrics_renderer_push(lyrics, section, songTitle);
        }
        return;
    }

    // Song not found in sequence — push anyway without highlight
    lyrics_renderer_push(lyrics, section, songTitle);
}

// New index-driven approach: remote sends (segIdx, slideIdx) and
// the plugin looks up everything from its own service.json data.
void LyricsDock::activateSlideFromRemote(int segIdx, int slideIdx)
{
    if (segIdx < 0 || segIdx >= m_segments.size()) return;
    const Segment &item = m_segments[segIdx];

    // Activating anything other than a presentation takes the screen back from a running one.
    if (item.type != Segment::Type::Presentation && m_pptTab && m_pptTab->activeType() >= 0) {
        m_pptTab->requestShow(-1);
        m_activePptSegId.clear();
    }

    switch (item.type) {
    case Segment::Type::Paragraphs: {
        QStringList slides = item.parseSlides();
        if (slideIdx < 0 || slideIdx >= slides.size()) return;
        // Clear any previously rendered lyrics text before showing paragraph
        lyrics_renderer_clear();
        // Card's own background, else the segment default (blank clears)
        showBackground(item.backgroundFor(slideIdx));
        rememberOnAir(item, slideIdx);
        // Push text through CTW renderer
        applyParagraphStyle(item);
        lyrics_renderer_paragraph_push(item.title, slides[slideIdx]);
        if (m_wsServer) m_wsServer->broadcastScripture(item.title, slides[slideIdx]);
        if (m_wsServer) m_wsServer->broadcastActiveMode("paragraph");
        break;
    }
    case Segment::Type::Lyrics: {
        int row = m_sequenceSongIds.indexOf(item.id);
        if (row < 0) return;
        Song song = songFromItem(item);
        QList<LyricsSlide> all = song.allSlides();
        if (slideIdx < 0 || slideIdx >= all.size()) return;
        const LyricsSlide &sl = all[slideIdx];
        int globalIdx = computeGlobalSlideOffset(row) + slideIdx;
        pushSlideFromRemote(song.title, globalIdx, sl.text, sl.section);
        // Echo back slideUpdate so the remote highlights the correct card
        if (m_wsServer) m_wsServer->broadcastSlide(sl.text, sl.section, song.title, globalIdx);
        if (m_wsServer) m_wsServer->broadcastActiveMode("lyr");
        break;
    }
    case Segment::Type::Media: {
        if (slideIdx < 0 || slideIdx >= item.mediaPaths.size()) return;
        const QString &path = item.mediaPaths[slideIdx];
        rememberOnAir(item, slideIdx);
        m_currentBgPath = path;
        // audio runs independently — video starting no longer stops it
        lyrics_renderer_clear();   // wipe any lyrics from OBS source
        int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
        lyrics_renderer_media_push(path, fadeMs);
        if (m_wsServer) {
            m_wsServer->broadcastClear();        // clears confidence monitor
            m_wsServer->broadcastMediaState(path);
        }
        break;
    }
    case Segment::Type::Presentation:
        m_activeSegIdx = segIdx;
        bindPptItem(segIdx);
        publishOnAir();
        // Highlight the presentation item in the service list
        m_segmentList->setCurrentRow(segIdx);
        break;
    default:
        break;
    }
}

void LyricsDock::applyScriptureFromRemote(const QString &title, const QString &text)
{
    applyParagraphsFromRemote(title, text);
}

void LyricsDock::applyParagraphsFromRemote(const QString &title, const QString &text)
{
    if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
        applyParagraphStyle(m_segments[m_activeSegIdx]);
    else {
        lyrics_renderer_set_paragraph_layout(QByteArray());
        lyrics_renderer_set_alt_sentence_color(false, 0, 0);
    }
    lyrics_renderer_paragraph_push(title, text);
    if (m_wsServer) m_wsServer->broadcastCtwState(title, text);
}

void LyricsDock::stopAllMedia()
{
    if (m_videoTab)      m_videoTab->stopPlayback();
    if (m_sharedGallery) m_sharedGallery->stopPlayback();
    if (m_audioBin)      m_audioBin->stopPlayback();
    lyrics_renderer_mv_clear();
    lyrics_renderer_media_clear();
}

void LyricsDock::releasePptCapture()
{
    if (m_pptTab) m_pptTab->releaseCapture();
}

// ── Remote control helpers ────────────────────────────────────

void LyricsDock::pptNext()  { if (m_pptTab) m_pptTab->nextSlide(); }
void LyricsDock::pptPrev()  { if (m_pptTab) m_pptTab->prevSlide(); }

void LyricsDock::pushParagraphCurrent()
{
    if (m_activeSlide >= 0) pushCurrentSlide();
}

void LyricsDock::pushScriptureCurrent()
{
    if (m_activeSlide >= 0) pushCurrentSlide();
}

void LyricsDock::restoreCurrentTabContent()
{
    if (m_activeSegIdx < 0 || m_activeSegIdx >= m_segments.size()) return;
    const auto &item = m_segments[m_activeSegIdx];
    switch (item.type) {
    case Segment::Type::Paragraphs:
        if (m_activeSlide >= 0) pushCurrentSlide();
        break;
    default:
        if (m_activeSlide >= 0) pushCurrentSlide();
        break;
    }
}

void LyricsDock::presentationShowFromRemote(int type) { if (m_pptTab) m_pptTab->requestShow(type); }
void LyricsDock::pptGoto(int slide) { if (m_pptTab) m_pptTab->gotoSlide(slide); }

void LyricsDock::pptGetState()
{
    if (!m_pptTab || !m_wsServer) return;
    m_wsServer->broadcastPptState(m_pptTab->currentSlide(),
                                   m_pptTab->totalSlides(),
                                   m_pptTab->presTitle());
}

void LyricsDock::applyParagraphFromRemote(const QString &title, const QString &text)
{
    applyParagraphsFromRemote(title, text);
}

void LyricsDock::pushParagraphSlideFromRemote(const QString &title, const QString &text)
{
    lyrics_renderer_scripture_push(title, text);
    if (m_wsServer) m_wsServer->broadcastScripture(title, text);
}

void LyricsDock::saveState()
{
    // Save slide-card appearance prefs
    QJsonObject obj;
    QJsonArray presets;
    for (int i = 0; i < 6; ++i) {
        if (!m_colorBtn[i]) continue;
        QJsonObject p;
        p["font"]    = (qint64)(m_colorBtn[i]->fontColor().rgb()    & 0x00FFFFFF);
        p["outline"] = (qint64)(m_colorBtn[i]->outlineColor().rgb() & 0x00FFFFFF);
        presets.append(p);
    }
    obj["colorPresets"]  = presets;
    obj["linesPerSlide"]  = m_linesPerSlideSpin ? m_linesPerSlideSpin->value() : 4;
    obj["slideFont"]      = m_slideFontFamily;
    obj["cardsPerRow"] = m_cardWidthScale;
    if (m_globalFadeSpin) obj["globalFadeMs"] = m_globalFadeSpin->value();

    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());

    if (m_sharedGallery)     m_sharedGallery->saveState();
    if (m_audioBin)          m_audioBin->saveState();
    if (m_videoTab)          m_videoTab->saveState();

    saveServiceOrder();
}

void LyricsDock::loadState()
{
    QFile f(statePath());
    if (f.open(QIODevice::ReadOnly)) {
        auto doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isNull()) {
            auto pa = doc.object()["colorPresets"].toArray();
            for (int i = 0; i < 6 && i < pa.size(); ++i) {
                if (!m_colorBtn[i]) continue;
                auto po = pa[i].toObject();
                uint32_t fc = (uint32_t)po["font"].toInteger(0x00FFFFFF);
                uint32_t oc = (uint32_t)po["outline"].toInteger(0x00000000);
                m_colorBtn[i]->setColors(
                    QColor((fc >> 16) & 0xFF, (fc >> 8) & 0xFF, fc & 0xFF),
                    QColor((oc >> 16) & 0xFF, (oc >> 8) & 0xFF, oc & 0xFF));
            }
            QString sf = doc.object()["slideFont"].toString("Calibri");
            if (!sf.isEmpty()) m_slideFontFamily = sf;
            int cpr = qBound(1, doc.object()["cardsPerRow"].toInt(3), 5);
            // migrate old percentage-based saves (values > 5 are old % / 10)
            if (doc.object().contains("cardWidthScale") && !doc.object().contains("cardsPerRow")) {
                int old = doc.object()["cardWidthScale"].toInt(30) / 10;
                cpr = qBound(1, 6 - qBound(1, old, 5), 5); // invert: wide% → fewer cols
            }
            m_cardWidthScale = cpr;
            if (m_cardWidthSlider) { QSignalBlocker b(m_cardWidthSlider); m_cardWidthSlider->setValue(cpr); }
            if (m_cardWidthLabel) m_cardWidthLabel->setText(QString("%1").arg(cpr));
            if (doc.object().contains("globalFadeMs") && m_globalFadeSpin) {
                int fms = doc.object()["globalFadeMs"].toInt(500);
                QSignalBlocker b(m_globalFadeSpin);
                m_globalFadeSpin->setValue(fms);
                if (m_sharedGallery) m_sharedGallery->setFadeDurationMs(fms);
                if (m_videoTab) m_videoTab->setFadeDurationMs(fms);
            }
            if (m_videoTab) m_videoTab->setNumCols(cpr);
            int lpp = doc.object()["linesPerSlide"].toInt(4);
            setGlobalLinesPerSlide(lpp);
            if (m_linesPerSlideSpin) { QSignalBlocker b(m_linesPerSlideSpin); m_linesPerSlideSpin->setValue(lpp); }
        }
    }
    loadServiceOrder();
    if (m_audioBin) m_audioBin->loadState();
}

// ── Segment ───────────────────────────────────────────────

QJsonObject Segment::toJson() const
{
    QJsonObject o;
    switch (type) {
    case Type::Lyrics:       o["type"] = "song";         break;
    case Type::Paragraphs:   o["type"] = "paragraphs";   break;
    case Type::Media:        o["type"] = "media";        break;
    case Type::Presentation: o["type"] = "presentation"; break;
    }
    o["id"] = id; o["title"] = title; o["artist"] = artist;
    if (!songKey.isEmpty())       o["songKey"]       = songKey;
    if (!songTempo.isEmpty())     o["songTempo"]     = songTempo;
    if (!songCopyright.isEmpty()) o["songCopyright"] = songCopyright;
    o["text"] = text; o["filePath"] = filePath;
    if (!windowTitle.isEmpty()) o["windowTitle"] = windowTitle;
    if (!windowProc.isEmpty())  o["windowProc"]  = windowProc;
    if (!windowCls.isEmpty())   o["windowCls"]   = windowCls;
    if (pptComMode != -1)       o["pptComMode"]  = pptComMode;
    if (!mediaPaths.isEmpty()) {
        QJsonArray mp;
        for (const auto &p : mediaPaths) mp.append(p);
        o["mediaPaths"] = mp;
    }
    if (!sections.isEmpty()) {
        QJsonArray secs;
        for (const auto &s : sections) secs.append(s.toJson());
        o["sections"] = secs;
    }
    // Always written for Lyrics: its absence is how an old (pre-id) file is recognised.
    if (type == Type::Lyrics || !sourceSongId.isEmpty())
        o["sourceSongId"] = sourceSongId;
    if (!cards.isEmpty()) {
        QJsonArray ca;
        for (const auto &c : cards) {
            QJsonObject co;
            co["id"]  = c.id;
            co["key"] = c.key;
            if (!c.background.isEmpty()) co["bg"] = c.background;
            ca.append(co);
        }
        o["cards"] = ca;
    }
    if (!defaultBackground.isEmpty())
        o["defaultBackground"] = defaultBackground;
    if (!paragraphLayout.isEmpty())
        o["layout"] = paragraphLayout;
    if (colorPresetIdx >= 0)
        o["colorPresetIdx"] = colorPresetIdx;
    if (altSentenceColor)
        o["altSentenceColor"] = true;
    if (altSentencePreset >= 0)
        o["altSentencePreset"] = altSentencePreset;
    return o;
}

Segment Segment::fromJson(const QJsonObject &obj)
{
    Segment item;
    const QString t = obj["type"].toString();
    // Legacy types mapped to new types
    if      (t == "paragraphs")  item.type = Type::Paragraphs;
    else if (t == "paragraph")         item.type = Type::Paragraphs;
    else if (t == "scripture")   item.type = Type::Paragraphs;
    else if (t == "media")       item.type = Type::Media;
    else if (t == "video")       item.type = Type::Media;
    else if (t == "music")       item.type = Type::Media;
    else if (t == "presentation") item.type = Type::Presentation;
    else                          item.type = Type::Lyrics;
    item.id = obj["id"].toString(); item.title = obj["title"].toString();
    item.artist = obj["artist"].toString(); item.text = obj["text"].toString();
    item.songKey       = obj["songKey"].toString();
    item.songTempo     = obj["songTempo"].toString();
    item.songCopyright = obj["songCopyright"].toString();
    item.filePath = obj["filePath"].toString();
    // "windowExe" is the pre-v12.25.0 key (held an exe name, later a window title).
    item.windowTitle = obj.contains("windowTitle") ? obj["windowTitle"].toString()
                                                   : obj["windowExe"].toString();
    item.windowProc  = obj["windowProc"].toString();
    item.windowCls   = obj["windowCls"].toString();
    item.pptComMode  = obj["pptComMode"].toInt(-1);
    for (const auto &v : obj["mediaPaths"].toArray())
        item.mediaPaths << v.toString();
    for (const auto &v : obj["sections"].toArray())
        if (v.isObject()) item.sections << LyricsSection::fromJson(v.toObject());
    // Slide cards: permanent ids + each card's own background.
    for (const auto &v : obj["cards"].toArray()) {
        const QJsonObject co = v.toObject();
        SlideCard c;
        c.id         = co["id"].toString();
        c.key        = co["key"].toString();
        c.background = co["bg"].toString();
        if (c.id.isEmpty()) c.id = newId();
        item.cards << c;
    }
    // Files from before slide-card ids kept backgrounds by slide index.
    const auto bgsObj = obj["slideBgs"].toObject();
    for (auto it = bgsObj.constBegin(); it != bgsObj.constEnd(); ++it)
        item.legacySlideBgs[it.key().toInt()] = it.value().toString();
    item.defaultBackground = obj["defaultBackground"].toString();
    item.paragraphLayout    = obj["layout"].toObject();
    if (obj.contains("colorPresetIdx"))
        item.colorPresetIdx = obj["colorPresetIdx"].toInt(-1);
    item.altSentenceColor  = obj["altSentenceColor"].toBool(false);
    item.altSentencePreset = obj["altSentencePreset"].toInt(-1);

    // Before this version a Lyrics segment's id WAS the library song id.  Now the segment has
    // its own unique id and the library id is kept in sourceSongId.
    if (item.type == Type::Lyrics) {
        if (obj.contains("sourceSongId")) {
            item.sourceSongId = obj["sourceSongId"].toString();
        } else {
            item.sourceSongId = item.id;
            item.id           = newId();
            item.needsSave    = true;
        }
    }
    if (item.id.isEmpty()) { item.id = newId(); item.needsSave = true; }

    const bool hadCards = !item.cards.isEmpty();
    item.syncCards();
    if (!hadCards && !item.cards.isEmpty()) item.needsSave = true;
    return item;
}

QStringList Segment::parseSlides() const
{
    QStringList slides;
    QStringList groups = text.split(QRegularExpression(R"(\n[ \t]*\n[ \t]*\n)"), Qt::SkipEmptyParts);
    for (const QString &g : groups) {
        QString trimmed = g.trimmed();
        if (!trimmed.isEmpty())
            slides << trimmed;
    }
    return slides;
}

QString Segment::typeLabel() const
{
    switch (type) {
    case Type::Lyrics:       return "Lyrics";
    case Type::Paragraphs:   return "Paragraphs";
    case Type::Media:        return "Media";
    case Type::Presentation: return "Presentation";
    }
    return {};
}

QString Segment::typeIcon() const
{
    if (!title.isEmpty())
        return QString(title.at(0));
    // fallback when title is empty
    switch (type) {
    case Type::Lyrics:       return "L";
    case Type::Paragraphs:   return "P";
    case Type::Media:        return "M";
    case Type::Presentation: return "S";
    }
    return {};
}

QColor Segment::typeColor() const
{
    switch (type) {
    case Type::Lyrics:       return QColor("#4a9eff");
    case Type::Paragraphs:   return QColor("#3dd68c");
    case Type::Media:        return QColor("#f59e0b");
    case Type::Presentation: return QColor("#c084fc");
    }
    return QColor("#4a9eff");
}

// ── SegmentDelegate ───────────────────────────────────────

SegmentDelegate::SegmentDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

QSize SegmentDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    int w = option.rect.width();
    return {w > 0 ? w : 200, 52};
}

void SegmentDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                                const QModelIndex &idx) const
{
    if (!p || !idx.isValid()) return;
    if (opt.rect.width() <= 0 || opt.rect.height() <= 0) return;
    p->save();
    const bool sel = opt.state & QStyle::State_Selected;
    const bool hov = opt.state & QStyle::State_MouseOver;
    p->fillRect(opt.rect, sel ? QColor("#3a3a45") : hov ? QColor("#2a2a32") : QColor("#16161a"));

    const auto item = idx.data(Qt::UserRole).value<Segment>();
    // Left colour stripe
    p->fillRect(opt.rect.left(), opt.rect.top(), 3, opt.rect.height(), item.typeColor());
    // Icon background
    QRect iconR(opt.rect.left() + 10, opt.rect.top() + 10, 28, 28);
    QColor ibg = item.typeColor().darker(200);
    ibg.setAlpha(160);
    p->setBrush(ibg); p->setPen(Qt::NoPen);
    p->drawRoundedRect(iconR, 5, 5);
    // Icon — use painter's existing font (avoids DirectWrite font-lookup crash in OBS plugin)
    {
        QFont f = p->font(); f.setPointSize(13); f.setBold(true);
        p->setFont(f);
    }
    p->setPen(item.typeColor());
    p->drawText(iconR, Qt::AlignCenter, item.typeIcon());
    // Title
    p->setPen(QColor("#f0f0f5"));
    {
        QFont f = p->font(); f.setPointSize(11); f.setBold(false);
        p->setFont(f);
    }
    QRect titleR(opt.rect.left() + 46, opt.rect.top() + 7, opt.rect.width() - 50, 19);
    p->drawText(titleR, Qt::AlignLeft | Qt::AlignVCenter,
                item.title.isEmpty() ? "(Untitled)" : item.title);
    // Subtitle
    p->setPen(QColor("#6b6b7b"));
    {
        QFont f = p->font(); f.setPointSize(9); f.setBold(false);
        p->setFont(f);
    }
    QString sub = item.typeLabel();
    if (!item.artist.isEmpty()) sub += " · " + item.artist;
    QRect subR(opt.rect.left() + 46, opt.rect.top() + 27, opt.rect.width() - 50, 16);
    p->drawText(subR, Qt::AlignLeft | Qt::AlignVCenter, sub);
    // Divider
    p->setPen(QColor("#1e1e24"));
    p->drawLine(opt.rect.left() + 3, opt.rect.bottom(), opt.rect.right(), opt.rect.bottom());
    p->restore();
}

// ── buildUi ───────────────────────────────────────────────────

void LyricsDock::buildUi()
{
    // Global dark theme
    setStyleSheet(
        "LyricsDock, LyricsDock QWidget {"
        "  background:#0d0d0f; color:#f0f0f5;"
        "  font-family:'Segoe UI',Arial,sans-serif; font-size:12px; }"
        "QGroupBox { border:1px solid #2c2c35; border-radius:5px;"
        "  margin-top:14px; padding:6px 5px 5px 5px; }"
        "QGroupBox::title { color:#6b6b7b; font-size:10px;"
        "  subcontrol-origin:margin; subcontrol-position:top left;"
        "  padding:0 4px; left:8px; }"
        "QListWidget { background:#16161a; border:1px solid #2c2c35;"
        "  border-radius:4px; outline:none; }"
        "QListWidget::item { border-bottom:1px solid #1a1a22; }"
        "QListWidget::item:selected { background:#3a3a45; color:#f0f0f5; }"
        "QListWidget::item:hover:!selected { background:#2a2a32; }"
        "QPushButton { background:#1e1e24; border:1px solid #2c2c35;"
        "  border-radius:4px; color:#f0f0f5; padding:4px 10px; min-height:22px; }"
        "QPushButton:hover { background:#2a2a32; border-color:#3d3d48; }"
        "QPushButton:pressed { background:#3a3a45; }"
        "QPushButton:disabled { color:#6b6b7b; border-color:#1a1a22; }"
        "QToolButton { background:#1e1e24; border:1px solid #2c2c35;"
        "  border-radius:4px; color:#f0f0f5; min-width:22px; min-height:22px; }"
        "QToolButton:hover { background:#2a2a32; }"
        "QToolButton:pressed { background:#3a3a45; }"
        "QSplitter::handle { background:#2c2c35; width:1px; height:1px; }"
        "QScrollBar:vertical { background:#16161a; width:7px; }"
        "QScrollBar::handle:vertical { background:#3d3d48; border-radius:3px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QComboBox,QFontComboBox { background:#1e1e24; border:1px solid #2c2c35;"
        "  border-radius:4px; color:#f0f0f5; padding:2px 6px; }"
        "QComboBox::drop-down { border:none; }"
        "QComboBox QAbstractItemView { background:#1e1e24; color:#f0f0f5;"
        "  border:1px solid #3d3d48; selection-background-color:#3a3a45; }"
        "QSlider::groove:horizontal { background:#2c2c35; height:4px; border-radius:2px; }"
        "QSlider::handle:horizontal { background:#4a9eff; width:12px; height:12px;"
        "  border-radius:6px; margin:-4px 0; }"
        "QSlider::sub-page:horizontal { background:#4a9eff; border-radius:2px; }"
        "QScrollArea { border:none; background:#0d0d0f; }"
        "QLabel { background:transparent; }"
        "QSpinBox { background:#1e1e24; border:1px solid #2c2c35;"
        "  border-radius:4px; color:#f0f0f5; padding:2px 4px; }"
    );

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header bar ────────────────────────────────────────────
    auto *hdrBar  = new QWidget;
    hdrBar->setFixedHeight(38);
    hdrBar->setStyleSheet("background:#1a1a22;border-bottom:1px solid #2c2c35;");
    auto *hdrL = new QHBoxLayout(hdrBar);
    hdrL->setContentsMargins(10, 0, 6, 0);
    hdrL->setSpacing(6);
    auto *hdrLbl = new QLabel("SSO v2.0");
    hdrLbl->setStyleSheet("color:#f0f0f5;font-weight:bold;font-size:12px;");
    auto *liveIndicator = new QLabel(QString::fromUtf8("\u25CF LIVE"));
    liveIndicator->setStyleSheet("color:#3dd68c;font-size:9px;font-weight:bold;");
    auto *gearBtn = new QToolButton;
    gearBtn->setText(QString::fromUtf8("\u2699"));
    gearBtn->setToolTip("Settings (WebSocket, Port)");
    gearBtn->setStyleSheet(
        "QToolButton{background:transparent;border:none;color:#8888aa;"
        "font-size:16px;padding:0 4px;}"
        "QToolButton:hover{color:#f0f0f5;}");

    m_bgDebugBtn = new QToolButton;
    m_bgDebugBtn->setText("BG");
    m_bgDebugBtn->setCheckable(true);
    m_bgDebugBtn->setChecked(false);
    m_bgDebugBtn->setToolTip(
        "Debug BG Layers \u2014 splits the 4 background slots into quadrants on\n"
        "the OBS canvas (854\u00d7480 each) so layering and fades are visible.\n"
        "Does NOT affect the dock preview.");
    m_bgDebugBtn->setStyleSheet(
        "QToolButton{background:#1e1e24;border:1px solid #2c2c35;"
        "border-radius:3px;color:#8888aa;font-size:9px;font-weight:bold;"
        "padding:1px 5px;min-height:18px;}"
        "QToolButton:hover{color:#f0f0f5;border-color:#3d3d48;}"
        "QToolButton:checked{background:#7a4400;border-color:#e87d00;color:#ffb347;}");
    connect(m_bgDebugBtn, &QToolButton::toggled, this, [](bool on){
        lyrics_renderer_set_bg_debug(on);
    });

    hdrL->addWidget(hdrLbl);
    hdrL->addSpacing(4);
    hdrL->addWidget(liveIndicator);
    hdrL->addStretch();
    hdrL->addWidget(m_bgDebugBtn);
    hdrL->addWidget(gearBtn);
    root->addWidget(hdrBar);

    // Create WsTab eagerly so its capture timer starts immediately (preview frames).
    m_wsTab = new WsTab(this);
    m_wsTab->hide();

    // Gear -> settings dialog (WsTab moves into it on first open)
    connect(gearBtn, &QToolButton::clicked, this, [this](){
        if (!m_settingsDialog) {
            m_settingsDialog = new QDialog(window());
            m_settingsDialog->setWindowTitle("Settings");
            m_settingsDialog->setStyleSheet(
                "QDialog{background:#0d0d0f;color:#f0f0f5;}"
                "QLabel{background:transparent;}");
            m_settingsDialog->resize(500, 380);
            auto *dl = new QVBoxLayout(m_settingsDialog);
            dl->setContentsMargins(10,10,10,10);
            m_wsTab->setParent(m_settingsDialog);
            m_wsTab->show();
            dl->addWidget(m_wsTab);
        }
        m_settingsDialog->show();
        m_settingsDialog->raise();
        m_settingsDialog->activateWindow();
    });

    // ════ Main splitter: LEFT | CENTER | RIGHT ──────────────
    auto *mainSplit = new QSplitter(Qt::Horizontal);

    // ════ LEFT — Service Order ═══════════════════════════════
    auto *leftPanel  = new QWidget;
    leftPanel->setMinimumWidth(200);
    leftPanel->setMaximumWidth(340);
    auto *leftL = new QVBoxLayout(leftPanel);
    leftL->setContentsMargins(6, 6, 6, 4);
    leftL->setSpacing(4);

    // ── EDIT / LIVE mode buttons — side by side ───────────────
    m_editBtn = new QPushButton("EDIT");
    m_editBtn->setCheckable(true);
    m_editBtn->setChecked(true);
    m_editBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_editBtn->setMinimumHeight(28);
    m_editBtn->setStyleSheet(
        "QPushButton{background:#1a2a3a;color:#6a9acf;border:1px solid #2a5070;"
        "border-radius:3px;font-size:10px;font-weight:bold;}"
        "QPushButton:hover{background:#22385a;}"
        "QPushButton:checked{background:#0d2a4a;color:#4da6ff;border:1px solid #2a70c0;}");
    m_editBtn->setToolTip("Edit mode: select a segment to view its slides");

    m_liveToggleBtn = new QPushButton("LIVE");
    m_liveToggleBtn->setCheckable(true);
    m_liveToggleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_liveToggleBtn->setMinimumHeight(28);
    m_liveToggleBtn->setStyleSheet(
        "QPushButton{background:#1e2a1e;color:#4a8a4a;border:1px solid #2a5a2a;"
        "border-radius:3px;font-size:10px;font-weight:bold;}"
        "QPushButton:hover{background:#253025;}"
        "QPushButton:checked{background:#1a4a1a;color:#3dd68c;border:1px solid #2a8a2a;}");
    m_liveToggleBtn->setToolTip("Live mode: all segments displayed in sequence");

    auto *modeBtnRow = new QHBoxLayout;
    modeBtnRow->setSpacing(4);
    modeBtnRow->setContentsMargins(0, 0, 0, 0);
    modeBtnRow->addWidget(m_editBtn);
    modeBtnRow->addWidget(m_liveToggleBtn);
    leftL->addLayout(modeBtnRow);

    auto *orderHdr = new QLabel("SERVICE ORDER");
    orderHdr->setStyleSheet("color:#6b6b7b;font-size:9px;font-weight:bold;letter-spacing:1px;");
    leftL->addWidget(orderHdr);
    m_segmentList = new QListWidget(this);
    m_segmentList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_segmentList->setItemDelegate(new SegmentDelegate(m_segmentList));
    leftL->addWidget(m_segmentList, 1);

    auto *segBtnRow = new QHBoxLayout;
    segBtnRow->setSpacing(3);
    m_addItemBtn    = new QPushButton("+");
    m_removeItemBtn = new QPushButton(QString::fromUtf8("\u2212"));
    m_itemUpBtn     = new QPushButton(QString::fromUtf8("\u25B2"));
    m_itemDownBtn   = new QPushButton(QString::fromUtf8("\u25BC"));
    for (auto *b : {m_addItemBtn, m_removeItemBtn, m_itemUpBtn, m_itemDownBtn})
        b->setFixedSize(28, 28);
    m_addItemBtn->setToolTip("Add segment");
    m_removeItemBtn->setToolTip("Remove selected item");
    m_itemUpBtn->setToolTip("Move up");
    m_itemDownBtn->setToolTip("Move down");
    m_removeItemBtn->setEnabled(false);
    m_itemUpBtn->setEnabled(false);
    m_itemDownBtn->setEnabled(false);
    segBtnRow->addWidget(m_addItemBtn);
    segBtnRow->addWidget(m_removeItemBtn);
    segBtnRow->addWidget(m_itemUpBtn);
    segBtnRow->addWidget(m_itemDownBtn);
    segBtnRow->addStretch();

    // ── Global Fade row ───────────────────────────────────────────
    auto *fadeRowW = new QWidget;
    fadeRowW->setStyleSheet("background:#1a1a22;border-top:1px solid #222;");
    auto *fadeRowL = new QHBoxLayout(fadeRowW);
    fadeRowL->setContentsMargins(6, 3, 6, 3);
    fadeRowL->setSpacing(6);
    auto *fadeLbl2 = new QLabel("Fade:");
    fadeLbl2->setStyleSheet("color:#888;font-size:9px;");
    m_globalFadeSpin = new QSpinBox;
    m_globalFadeSpin->setRange(0, 3000);
    m_globalFadeSpin->setSingleStep(50);
    m_globalFadeSpin->setValue(500);
    m_globalFadeSpin->setSuffix(" ms");
    m_globalFadeSpin->setFixedWidth(80);
    m_globalFadeSpin->setFixedHeight(22);
    fadeRowL->addWidget(fadeLbl2);
    fadeRowL->addWidget(m_globalFadeSpin);
    fadeRowL->addStretch();
    leftL->addWidget(fadeRowW);

    // ── Emergency Stop bar ────────────────────────────────────────
    auto *stopBarW = new QWidget;
    stopBarW->setStyleSheet("background:#1a1a22;border-top:1px solid #222;");
    auto *stopBarL = new QHBoxLayout(stopBarW);
    stopBarL->setContentsMargins(6, 3, 6, 3);
    auto *stopAllBtn = new QPushButton(QString::fromUtf8("⏹  Stop All Media"));
    stopAllBtn->setFixedHeight(26);
    stopAllBtn->setStyleSheet(
        "QPushButton{background:#3a1010;color:#ff6060;border:1px solid #6a2020;"
        "border-radius:3px;font-size:10px;font-weight:700;}"
        "QPushButton:hover{background:#5a1818;}");
    stopBarL->addWidget(stopAllBtn);
    connect(stopAllBtn, &QPushButton::clicked, this, &LyricsDock::stopAllMedia);
    leftL->addWidget(stopBarW);

    leftL->addLayout(segBtnRow);

    // ── Import / Export row ───────────────────────────────────────
    auto *importExportRowW = new QWidget;
    importExportRowW->setStyleSheet("background:#141420;border-top:1px solid #222;");
    auto *importExportRowL = new QHBoxLayout(importExportRowW);
    importExportRowL->setContentsMargins(6, 3, 6, 3);
    importExportRowL->setSpacing(4);
    auto *importBtn = new QPushButton(QString::fromUtf8("⬆ Import"));
    auto *exportBtn = new QPushButton(QString::fromUtf8("⬇ Export"));
    importBtn->setFixedHeight(24);
    exportBtn->setFixedHeight(24);
    importBtn->setStyleSheet(
        "QPushButton{background:#102010;color:#88cc88;border:1px solid #336633;"
        "border-radius:3px;font-size:10px;font-weight:600;}"
        "QPushButton:hover{background:#1a3a1a;}");
    exportBtn->setStyleSheet(
        "QPushButton{background:#101030;color:#8899ee;border:1px solid #334488;"
        "border-radius:3px;font-size:10px;font-weight:600;}"
        "QPushButton:hover{background:#1a1a4a;}");
    importBtn->setToolTip("Import lyrics & paragraphs from service JSON file");
    exportBtn->setToolTip("Export lyrics & paragraphs to service JSON file");
    importExportRowL->addWidget(importBtn);
    importExportRowL->addWidget(exportBtn);
    connect(importBtn, &QPushButton::clicked, this, &LyricsDock::onImportService);
    connect(exportBtn, &QPushButton::clicked, this, &LyricsDock::onExportService);
    leftL->addWidget(importExportRowW);

    // ════ CENTER — Content Area ══════════════════════════
    // Wrapper widget: LIVE toggle button + stacked content
    auto *centerWidget = new QWidget;
    auto *centerL = new QVBoxLayout(centerWidget);
    centerL->setContentsMargins(0, 0, 0, 0);
    centerL->setSpacing(0);

    m_contentStack = new QStackedWidget;

    // Page 0: Slide panel — shared for Lyrics, Paragraphs, and LIVE mode
    m_contentStack->addWidget(buildSongPanel());

    // Page 1: Media
    m_videoTab = new VideoTab;
    m_contentStack->addWidget(buildMediaPanel());

    // Page 2: Presentation
    m_pptTab = new PptTab;
    m_contentStack->addWidget(m_pptTab);

    // Page 3: Empty placeholder
    auto *emptyPage = new QWidget;
    auto *emptyL = new QVBoxLayout(emptyPage);
    auto *emptyLbl = new QLabel("Select a segment\nto see its content.");
    emptyLbl->setAlignment(Qt::AlignCenter);
    emptyLbl->setStyleSheet("color:#6b6b7b;font-size:13px;");
    emptyL->addStretch();
    emptyL->addWidget(emptyLbl);
    emptyL->addStretch();
    m_contentStack->addWidget(emptyPage);
    m_contentStack->setCurrentIndex(3);

    centerL->addWidget(m_contentStack, 1);
    centerL->addWidget(m_slideCtrlRow);

    // ════ RIGHT — Graphics Bin ═══════════════════════════════
    auto *rightPanel = new QWidget;
    rightPanel->setMinimumWidth(200);
    rightPanel->setMaximumWidth(380);
    auto *rightL = new QVBoxLayout(rightPanel);
    rightL->setContentsMargins(6, 6, 6, 4);
    rightL->setSpacing(4);

    // ── Edit Slide block ──────────────────────────────────────────
    auto *editSlideHdrLbl = new QLabel("EDIT SLIDE");
    editSlideHdrLbl->setStyleSheet("color:#6b6b7b;font-size:9px;font-weight:bold;letter-spacing:1px;");
    rightL->addWidget(editSlideHdrLbl);

    m_editSlidePanel = new QWidget(rightPanel);
    m_editSlidePanel->setStyleSheet("background:#16161a;border:1px solid #222;border-radius:4px;");
    auto *editSlidePanelL = new QVBoxLayout(m_editSlidePanel);
    editSlidePanelL->setContentsMargins(4, 2, 4, 2);
    editSlidePanelL->setSpacing(2);
    m_editSlideHdr = new QLabel("No slide selected");
    m_editSlideHdr->setMaximumHeight(16);
    m_editSlideHdr->setStyleSheet("color:#6b6b7b;font-size:9px;margin:0;padding:0;");
    m_editSlideText = new QPlainTextEdit;
    m_editSlideText->setMinimumHeight(60);
    m_editSlideText->setStyleSheet(
        "QPlainTextEdit{background:#1a1a24;color:#eee;border:1px solid #2a2a36;"
        "border-radius:3px;font-size:14px;padding:4px;}");
    m_editSlideText->setPlaceholderText("Select a slide to edit...");
    m_editSlideText->setEnabled(false);
    m_editSlideApplyBtn = new QPushButton("Apply");
    m_editSlideApplyBtn->setEnabled(false);
    m_editSlideApplyBtn->setFixedHeight(22);
    m_editSlideApplyBtn->setStyleSheet(
        "QPushButton{background:#1a3a5c;color:#4da6ff;border:1px solid #2a5a8c;"
        "border-radius:3px;font-size:10px;font-weight:700;padding:0 8px;}"
        "QPushButton:hover{background:#254e7a;}"
        "QPushButton:disabled{background:#1a1a22;color:#444;border-color:#333;}");
    auto *editBtnRow = new QHBoxLayout;
    editBtnRow->addStretch();
    editBtnRow->addWidget(m_editSlideApplyBtn);
    editSlidePanelL->addWidget(m_editSlideHdr);
    editSlidePanelL->addWidget(m_editSlideText);
    editSlidePanelL->addLayout(editBtnRow);
    rightL->addWidget(m_editSlidePanel, 1);

    // ── Divider before media bin ──────────────────────────────
    auto *editDivider = new QFrame(rightPanel);
    editDivider->setFrameShape(QFrame::HLine);
    editDivider->setStyleSheet("color:#333;");
    rightL->addWidget(editDivider);

    connect(m_editSlideApplyBtn, &QPushButton::clicked, this, &LyricsDock::onEditSlideApply);

    // ── Graphics Bin (top half) ───────────────────────────────
    auto *binHdr = new QLabel("GRAPHICS BIN");
    binHdr->setStyleSheet("color:#6b6b7b;font-size:9px;font-weight:bold;letter-spacing:1px;");
    rightL->addWidget(binHdr);
    m_sharedGallery = new MediaGallery(rightPanel, "shared-gallery.json");
    rightL->addWidget(m_sharedGallery, 1);


    // ── Divider ───────────────────────────────────────────────
    auto *divider = new QFrame(rightPanel);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("color:#333;");
    rightL->addWidget(divider);

    // ── Audio Bin (bottom half) ───────────────────────────────
    auto *audioBinHdr = new QLabel("AUDIO BIN");
    audioBinHdr->setStyleSheet(
        "color:#6b6b7b;font-size:9px;font-weight:bold;letter-spacing:1px;");
    rightL->addWidget(audioBinHdr);
    m_audioBin = new AudioBin(rightPanel);
    rightL->addWidget(m_audioBin, 1);

    // Audio Bin takes priority: mute video/media audio while Audio Bin is playing.
    // When Audio Bin stops, media audio resumes automatically.
    connect(m_audioBin, &AudioBin::playbackStarted, this, [](){ lyrics_renderer_mute_media_audio(true);  });
    connect(m_audioBin, &AudioBin::playbackStopped, this, [](){ lyrics_renderer_mute_media_audio(false); });

    // Assemble splitter
    mainSplit->addWidget(leftPanel);
    mainSplit->addWidget(centerWidget);
    mainSplit->addWidget(rightPanel);
    mainSplit->setStretchFactor(0, 0);
    mainSplit->setStretchFactor(1, 1);
    mainSplit->setStretchFactor(2, 0);
    mainSplit->setSizes({260, 500, 260});
    root->addWidget(mainSplit, 1);

    // ── Service list connections ──────────────────────────────
    connect(m_segmentList, &QListWidget::currentRowChanged,
            this, &LyricsDock::onSegmentSelected);
    connect(m_segmentList, &QListWidget::customContextMenuRequested,
            this, &LyricsDock::onSegmentContextMenu);
    connect(m_addItemBtn,    &QPushButton::clicked, this, &LyricsDock::onAddSegment);
    connect(m_removeItemBtn, &QPushButton::clicked, this, &LyricsDock::onRemoveSegment);
    connect(m_itemUpBtn,     &QPushButton::clicked, this, &LyricsDock::onSegmentUp);
    connect(m_itemDownBtn,   &QPushButton::clicked, this, &LyricsDock::onSegmentDown);

    // ── Cross-panel media coordination ───────────────────────
    connect(m_sharedGallery, &MediaGallery::mediaActivated, this, [this](){
        m_videoTab->stopPlayback();
        // Sync bg path cache — gallery pushes to OBS directly; without this,
        // applySlideBackground() sees a stale m_currentBgPath and skips re-pushing
        // the slide's assigned background when a slide card is next clicked.
        if (m_sharedGallery) m_currentBgPath = m_sharedGallery->activeItemPath();
    });
    connect(m_videoTab, &VideoTab::playbackStarted, this, [this](){
        if (m_pptTab && m_pptTab->activeType() >= 0) m_pptTab->requestShow(-1);
        lyrics_renderer_clear();
        lyrics_renderer_scripture_clear();
        if (m_sharedGallery) m_sharedGallery->clearActiveCard();
        // VideoTab pushed media to OBS background; invalidate cache so the next
        // paragraph/lyrics slide re-pushes its assigned background correctly.
        m_currentBgPath.clear();
    });
    // A media file started from the Media tab: record it as on air (by id) so the remote follows.
    connect(m_videoTab, &VideoTab::mediaActivated, this, [this](int fileIdx){
        if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size()
                && m_segments[m_activeSegIdx].type == Segment::Type::Media) {
            m_segments[m_activeSegIdx].syncCards();
            rememberOnAir(m_segments[m_activeSegIdx], fileIdx);
        }
    });
    connect(m_sharedGallery, &MediaGallery::stopAllRequested, this, [this](){
        m_videoTab->stopPlayback();
        m_currentBgPath.clear(); // gallery cleared the OBS bg; reset cache to match
    });
    // Save mediaPaths back to the active segment whenever VideoTab's list changes
    connect(m_videoTab, &VideoTab::mediaListChanged, this, [this](const QStringList &paths){
        if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size()
                && m_segments[m_activeSegIdx].type == Segment::Type::Media) {
            m_segments[m_activeSegIdx].mediaPaths = paths;
            m_segments[m_activeSegIdx].syncCards();   // every media file keeps its own card id
            saveServiceOrder();
            // Broadcast updated service order so remote refreshes media slide cards
            if (m_wsServer)
                m_wsServer->broadcastServiceOrder(buildServiceOrderJson());
        }
    });

    // ── LIVE toggle ───────────────────────────────────────────
    connect(m_liveToggleBtn, &QPushButton::clicked, this, [this](){
        if (!m_liveMode) onLiveToggled(true);
        else m_liveToggleBtn->setChecked(true);  // stay in live mode if already there
    });
    connect(m_editBtn, &QPushButton::clicked, this, [this](){
        if (m_liveMode) onLiveToggled(false);
        else m_editBtn->setChecked(true);  // stay in edit mode if already there
    });

    // ── PptTab connections ────────────────────────────────────
    connect(m_pptTab, &PptTab::slideStateChanged, this, [this](int cur, int tot, const QString &title){
        if (m_wsServer) m_wsServer->broadcastPptState(cur, tot, title);
    });
    connect(m_pptTab, &PptTab::showSlidesRequested, this, [this](int type){
        if (type >= 0) {
            lyrics_renderer_clear();
            lyrics_renderer_scripture_clear();
            lyrics_renderer_paragraph_clear();
            lyrics_renderer_media_clear();
            lyrics_renderer_mv_clear(0);
            if (m_sharedGallery) m_sharedGallery->suspendPlayback();
            m_currentBgPath.clear(); // Bug A: reset cache so slide bg re-pushes after returning from presentation
        }
        if (m_wsServer) m_wsServer->broadcastPresentationActive(type);
        publishOnAir();   // a presentation showing / stopping changes what is on air
    });
    // Window / COM choices belong to the item the capture is bound to
    // (m_pptBoundSegId), not to whichever row happens to be selected.
    connect(m_pptTab, &PptTab::windowChosen, this,
            [this](const QString &title, const QString &exe, const QString &cls){
        int i = segmentIndexById(m_pptBoundSegId);
        if (i < 0) return;
        Segment &item = m_segments[i];
        if (item.type != Segment::Type::Presentation) return;
        if (item.windowTitle == title && item.windowProc == exe && item.windowCls == cls) return;
        item.windowTitle = title;
        item.windowProc  = exe;
        item.windowCls   = cls;
        saveServiceOrder();
    });
    connect(m_pptTab, &PptTab::comModeChanged, this, [this](int mode){
        int i = segmentIndexById(m_pptBoundSegId);
        if (i < 0) return;
        Segment &item = m_segments[i];
        if (item.type != Segment::Type::Presentation) return;
        if (item.pptComMode == mode) return;
        item.pptComMode = mode;
        saveServiceOrder();
    });

    // ── Shared gallery media list broadcast ────────────────────
    connect(m_sharedGallery, &MediaGallery::mediaListChanged, this, [this](){
        if (m_wsServer && m_sharedGallery)
            m_wsServer->broadcastMediaList("shared", m_sharedGallery->itemPaths());
    });

    // ── Global fade spin ──────────────────────────────────────
    connect(m_globalFadeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){
        if (m_sharedGallery) m_sharedGallery->setFadeDurationMs(v);
        if (m_videoTab) m_videoTab->setFadeDurationMs(v);
    });
}

// ── buildSongPanel ────────────────────────────────────────────

QWidget *LyricsDock::buildSongPanel()
{
    auto *panel = new QWidget;
    auto *panelL = new QVBoxLayout(panel);
    panelL->setContentsMargins(0, 0, 0, 0);
    panelL->setSpacing(0);

    // Header row with color presets, lines-per-slide, layout, blank
    auto *hdr = new QWidget;
    hdr->setFixedHeight(kHdrBtnH + 4);
    hdr->setStyleSheet("background:#1a1a22;border-bottom:1px solid #2c2c35;");
    auto *hdrL = new QHBoxLayout(hdr);
    hdrL->setContentsMargins(8, 1, 6, 1);
    hdrL->setSpacing(4);

    auto *slidesTitleLbl = new QLabel("Slides");
    QFont hf = slidesTitleLbl->font(); hf.setBold(true); hf.setPointSize(9);
    slidesTitleLbl->setFont(hf);
    slidesTitleLbl->setStyleSheet("color:#ccc;");

    for (int i = 0; i < 6; ++i) {
        m_colorBtn[i] = new ColorPresetButton(i + 1, hdr);
        m_colorBtn[i]->setFixedSize(kBtnW, kHdrBtnH);
    }
    auto *pyLbl = new QLabel("Py:");
    pyLbl->setStyleSheet("color:#aaa;font-size:9px;");
    pyLbl->setToolTip("Buttons 4 & 5 set pinyin text color");

    auto *lppLbl = new QLabel("Lines:");
    lppLbl->setStyleSheet("color:#888;font-size:9px;");
    m_linesPerSlideSpin = new QSpinBox;
    m_linesPerSlideSpin->setRange(1, 6);
    m_linesPerSlideSpin->setValue(getGlobalLinesPerSlide());
    m_linesPerSlideSpin->setFixedWidth(80);
    m_linesPerSlideSpin->setFixedHeight(kHdrBtnH);
    m_linesPerSlideSpin->setToolTip("Max lines per slide (blank line always forces new slide)");

    hdrL->addWidget(slidesTitleLbl);
    hdrL->addSpacing(6);
    for (int i = 0; i < 4; ++i) hdrL->addWidget(m_colorBtn[i]);
    hdrL->addSpacing(4);
    hdrL->addWidget(pyLbl);
    hdrL->addWidget(m_colorBtn[4]);
    hdrL->addWidget(m_colorBtn[5]);
    hdrL->addSpacing(4);
    hdrL->addWidget(lppLbl);
    hdrL->addWidget(m_linesPerSlideSpin);
    hdrL->addStretch();

    auto *lyricsLayoutBtn = new QPushButton("Layout");
    lyricsLayoutBtn->setFixedSize(kBtnW, kHdrBtnH);
    lyricsLayoutBtn->setStyleSheet(
        "QPushButton{background:#1a2a3a;color:#6aacff;"
        "border:1px solid #2a5080;border-radius:3px;font-size:12px;padding:0 2px;}"
        "QPushButton:hover{background:#22385a;}");
    lyricsLayoutBtn->setToolTip(
        "Paragraph segment selected: edit THIS segment's layout.\n"
        "Lyrics: edit the lyrics layout.\n"
        "The DEFAULT paragraph layout (copied into new paragraph segments) is edited in the "
        "OBS source's Properties.");
    connect(lyricsLayoutBtn, &QPushButton::clicked, this, [this](){
        if (m_paragraphMode) {
            // A paragraph segment owns its layout: this button edits the selected segment's.
            showSegmentLayoutDialog(m_activeSegIdx);
        } else {
            if (!m_lyricsLayoutDialog) {
                m_lyricsLayoutDialog = new LayoutDialog("lyrics", window());
                connect(m_lyricsLayoutDialog, &QObject::destroyed, this,
                        [this](){ m_lyricsLayoutDialog = nullptr; });
            }
            m_lyricsLayoutDialog->show();
            m_lyricsLayoutDialog->raise();
            m_lyricsLayoutDialog->activateWindow();
        }
    });
    hdrL->addWidget(lyricsLayoutBtn);

    m_blackoutBtn = new QPushButton(QString::fromUtf8("■ Blank"));
    m_blackoutBtn->setFixedSize(kBtnW, kHdrBtnH);
    m_blackoutBtn->setToolTip("Blank out lyrics text");
    m_blackoutBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;font-size:12px;padding:0 2px;}"
        "QPushButton:hover{background:#6a2020;}");
    hdrL->addWidget(m_blackoutBtn);
    panelL->addWidget(hdr);

    // Slide scroll area
    m_slideArea = new QScrollArea;
    m_slideArea->setWidgetResizable(true);
    m_slideArea->setStyleSheet("QScrollArea{background:#181818;border:none;}");
    m_slideContainer = new QWidget;
    m_slideContainer->setStyleSheet("background:#181818;");
    m_slideGrid = new QGridLayout(m_slideContainer);
    m_slideGrid->setContentsMargins(8, 8, 8, 8);
    m_slideGrid->setSpacing(8);
    m_slideGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_slideArea->setWidget(m_slideContainer);
    panelL->addWidget(m_slideArea, 1);

    // Controls bar
    m_slideCtrlRow = new QWidget;
    auto *ctrlBar = m_slideCtrlRow;
    ctrlBar->setStyleSheet("background:#1a1a22;border-top:1px solid #2c2c35;");
    auto *ctrlL = new QHBoxLayout(ctrlBar);
    ctrlL->setContentsMargins(6, 4, 6, 4);
    ctrlL->setSpacing(4);

    m_prevBtn = new QPushButton(QString::fromUtf8("◄"));
    m_prevBtn->setFixedWidth(36);
    m_prevBtn->setToolTip("Previous slide (Left arrow)");
    m_slideCounter = new QLabel(QString::fromUtf8("—"));
    m_slideCounter->setAlignment(Qt::AlignCenter);
    m_slideCounter->setMinimumWidth(52);
    m_nextBtn = new QPushButton(QString::fromUtf8("►"));
    m_nextBtn->setFixedWidth(36);
    m_nextBtn->setToolTip("Next slide (Right arrow)");
    m_zoomOutBtn = new QToolButton; m_zoomOutBtn->setText("-"); m_zoomOutBtn->setToolTip("Zoom out");
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(1, 8); m_zoomSlider->setValue(4); m_zoomSlider->setMaximumWidth(80);
    m_zoomInBtn  = new QToolButton; m_zoomInBtn->setText("+");  m_zoomInBtn->setToolTip("Zoom in");
    m_zoomLabel  = new QLabel("100%"); m_zoomLabel->setFixedWidth(38);
    m_zoomLabel->setStyleSheet("color:#888;font-size:9px;");

    auto *wLbl = new QLabel("Cols:"); wLbl->setStyleSheet("color:#888;font-size:9px;");
    m_cardWidthOutBtn = new QToolButton; m_cardWidthOutBtn->setText("-");
    m_cardWidthSlider = new QSlider(Qt::Horizontal);
    m_cardWidthSlider->setRange(1, 5); m_cardWidthSlider->setValue(3); m_cardWidthSlider->setMaximumWidth(80);
    m_cardWidthInBtn  = new QToolButton; m_cardWidthInBtn->setText("+");
    m_cardWidthLabel  = new QLabel("3"); m_cardWidthLabel->setFixedWidth(24);
    m_cardWidthLabel->setStyleSheet("color:#888;font-size:9px;");

    ctrlL->addWidget(m_prevBtn);
    ctrlL->addWidget(m_slideCounter);
    ctrlL->addWidget(m_nextBtn);
    ctrlL->addStretch();
    ctrlL->addWidget(m_zoomOutBtn); ctrlL->addWidget(m_zoomSlider); ctrlL->addWidget(m_zoomInBtn); ctrlL->addWidget(m_zoomLabel);
    ctrlL->addSpacing(4);
    ctrlL->addWidget(wLbl); ctrlL->addWidget(m_cardWidthOutBtn); ctrlL->addWidget(m_cardWidthSlider);
    ctrlL->addWidget(m_cardWidthInBtn); ctrlL->addWidget(m_cardWidthLabel);
    // ctrlBar (m_slideCtrlRow) is NOT added here — it is added to centerL in buildUi()

    // Connections for song panel controls
    connect(m_prevBtn,    &QPushButton::clicked, this, &LyricsDock::onPrevSlide);
    connect(m_nextBtn,    &QPushButton::clicked, this, &LyricsDock::onNextSlide);
    connect(m_blackoutBtn,&QPushButton::clicked, this, &LyricsDock::onBlackoutClicked);
    for (int i = 0; i < 4; ++i) {
        connect(m_colorBtn[i], &ColorPresetButton::colorsApplied,
                this, [this, i](QColor fc, QColor oc) {
                    for (int j = 0; j < 6; ++j) if (m_colorBtn[j]) m_colorBtn[j]->setActive(j == i);
                    m_activeColorPresetIdx = i;
                    onColorPresetApplied(fc, oc);
                    if (m_wsServer) m_wsServer->broadcastColorPresets(colorPresetsJson(), i);
                });
    }
    for (int i = 4; i < 6; ++i) {
        connect(m_colorBtn[i], &ColorPresetButton::colorsApplied,
                this, [this, i](QColor fc, QColor oc) {
                    for (int j = 0; j < 6; ++j) if (m_colorBtn[j]) m_colorBtn[j]->setActive(j == i);
                    uint32_t f = ((uint32_t)fc.blue()<<16)|((uint32_t)fc.green()<<8)|(uint32_t)fc.red();
                    uint32_t o = ((uint32_t)oc.blue()<<16)|((uint32_t)oc.green()<<8)|(uint32_t)oc.red();
                    lyrics_renderer_set_pinyin_color(f, o);
                    saveState();
                    if (m_wsServer) m_wsServer->broadcastColorPresets(colorPresetsJson(), i);
                });
    }
    connect(m_zoomInBtn,  &QToolButton::clicked, this, &LyricsDock::onZoomIn);
    connect(m_zoomOutBtn, &QToolButton::clicked, this, &LyricsDock::onZoomOut);
    connect(m_zoomSlider, &QSlider::valueChanged, this, &LyricsDock::onZoomSlider);
    connect(m_cardWidthSlider, &QSlider::valueChanged, this, [this](int v) {
        m_cardWidthScale = v;
        m_cardWidthLabel->setText(QString::number(v));
        int prevActive = m_activeSlide;
        if (m_liveMode) {
            buildAllLiveSlides();
        } else if (m_paragraphMode && m_activeSegIdx >= 0) {
            loadParagraphSlides(m_segments[m_activeSegIdx]);
        } else if (!m_currentSong.id.isEmpty()) {
            loadSongSlides(m_currentSong);
        }
        if (prevActive >= 0 && prevActive < m_slides.size()) setActiveSlide(prevActive);
        if (m_videoTab) m_videoTab->setNumCols(v);
        saveState();
    });
    connect(m_cardWidthOutBtn, &QToolButton::clicked, this, [this](){
        m_cardWidthSlider->setValue(qMax(1, m_cardWidthSlider->value() - 1));
    });
    connect(m_cardWidthInBtn, &QToolButton::clicked, this, [this](){
        m_cardWidthSlider->setValue(qMin(5, m_cardWidthSlider->value() + 1));
    });
    connect(m_linesPerSlideSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){
        setGlobalLinesPerSlide(v);
        syncAllCards();   // slide counts changed: re-align every segment's cards
        // The rebuild below resets the highlight: find the on-air card again by id.
        const int onAir = (!m_paragraphMode && !m_liveMode
                           && m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
                              ? onAirIndexIn(m_segments[m_activeSegIdx]) : -1;
        if (!m_paragraphMode && !m_currentSong.id.isEmpty()) loadSongSlides(m_currentSong);
        if (onAir >= 0 && onAir < m_slides.size()) setActiveSlide(onAir);   // highlight only
        saveState();
    });

    // Right-click on slide area → paragraph edit context menu
    m_slideArea->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_slideArea, &QScrollArea::customContextMenuRequested,
            this, &LyricsDock::onParagraphsContextMenu);

    return panel;
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
        const Segment *si = nullptr;
        for (const auto &s : m_segments)
            if (s.type == Segment::Type::Lyrics && s.id == m_sequenceSongIds[r])
                { si = &s; break; }
        if (si) offset += songFromItem(*si).allSlides().size();
    }
    return offset;
}

QString LyricsDock::sequenceCurrentSongId() const
{
    if (m_activeSegIdx < 0 || m_activeSegIdx >= m_segments.size()) return {};
    const auto &item = m_segments[m_activeSegIdx];
    return item.type == Segment::Type::Lyrics ? item.id : QString{};
}

// updateSegmentListItem \u2014 refresh one row in the service list widget
void LyricsDock::updateSegmentListItem(int row)
{
    if (row < 0 || row >= m_segments.size() || !m_segmentList) return;
    auto *lwi = m_segmentList->item(row);
    if (!lwi) return;
    lwi->setData(Qt::UserRole, QVariant::fromValue(m_segments[row]));
    m_segmentList->update(m_segmentList->indexFromItem(lwi));
}

// removeSegment \u2014 delete one service order row
void LyricsDock::removeSegment(int row)
{
    if (row < 0 || row >= m_segments.size()) return;
    m_segments.removeAt(row);
    delete m_segmentList->takeItem(row);
    // Rebuild sequenceSongIds cache
    m_sequenceSongIds.clear();
    for (const auto &si : m_segments)
        if (si.type == Segment::Type::Lyrics) m_sequenceSongIds << si.id;
    clearSlideGrid();
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::applyZoomToAll()
{
    if (m_slideButtons.isEmpty() && m_liveExtraCards.isEmpty()) return;

    // Card width: divide viewport evenly across m_cardWidthScale columns.
    int vpw    = m_slideArea->viewport()->width() - 16; // subtract L+R margins
    if (vpw < 80) vpw = 580;                            // fallback before shown
    int cols   = m_cardWidthScale;
    int fixedW = qMax(80, (vpw - 8 * (cols - 1)) / cols);

    // Pass 1 — apply styles and let each card compute its own preferred height.
    for (auto *b : m_slideButtons)
        b->applyScale(m_slideScale, m_slideFontFamily, fixedW);

    // Pass 2 — unify height so all lyrics/paragraphs cards are the same size.
    // MediaSlideCard is excluded from maxH to prevent squarish images inflating all cards.
    int maxH = 0;
    for (auto *b : m_slideButtons)
        maxH = qMax(maxH, b->height());
    if (maxH > 0) {
        for (auto *b : m_slideButtons)
            b->setFixedHeight(maxH);
    }
    // Media cards get the same height as lyrics cards (capped independently of their thumbnail).
    int mediaH = maxH > 0 ? maxH : 120;
    for (auto *w : m_liveExtraCards)
        w->setFixedSize(fixedW, mediaH);

    m_slideContainer->adjustSize();
}

// ── Slide grid ────────────────────────────────────────────────

void LyricsDock::clearSlideGrid()
{
    for (auto *b : m_slideButtons) b->deleteLater();
    m_slideButtons.clear();
    for (auto *w : m_liveSeparators) w->deleteLater();
    m_liveSeparators.clear();
    for (auto *w : m_liveExtraCards) w->deleteLater();
    m_liveExtraCards.clear();
    m_slides.clear();
    m_slideSectionIndices.clear();
    m_activeSlide = -1;
    clearLiveMedia();
    m_liveMediaPlaying = false;
    m_liveMediaPaused  = false;
    m_activePptSegId.clear();
    updateSlideCounter();
}

void LyricsDock::loadSongSlides(const Song &song)
{
    clearSlideGrid();
    const int colVpw   = qMax(80, m_slideArea->viewport()->width() - 16);
    const int cols     = m_cardWidthScale;
    const int colCardW = qMax(80, (colVpw - 8 * (cols - 1)) / cols);
    int row = 0, col = 0, globalIdx = 0;

    // A card's thumb shows its own background, else the segment default.
    const Segment *cardSeg =
        (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
            ? &m_segments[m_activeSegIdx] : nullptr;

    // Pre-pass: assign Others slots to section types not matching verse/chorus/bridge.
    // Slot is assigned per unique type string, in order of first appearance.
    QMap<QString, int> othersMap;
    int othersNextSlot = 0;
    for (const auto &sec : song.sections) {
        QString lbl = sec.label.isEmpty() ? sectionTypeLabel(sec.type) : sec.label;
        QString type; int num;
        parseSectionName(lbl, type, num);
        bool known = type.contains("verse") || type.contains("chorus")
                  || type.contains("bridge");
        if (!known && !othersMap.contains(type) && othersNextSlot < 5)
            othersMap[type] = othersNextSlot++;
    }

    for (int si = 0; si < song.sections.size(); ++si) {
        const auto &sec = song.sections[si];

        // Build per-section slides list (Chinese or English)
        QList<LyricsSlide> secSlides;
        if (sec.isChinese) {
            secSlides = sec.chineseSlides();
        } else {
            QString lbl = sec.label.isEmpty()
                          ? sectionTypeLabel(sec.type) : sec.label;
            for (const auto &slideText : sec.slides()) {
                LyricsSlide sl;
                sl.section = lbl;
                sl.text    = slideText;
                secSlides << sl;
            }
        }

        for (auto sl : secSlides) {
            m_slides              << sl;
            m_slideSectionIndices << si;
            // Show "中" marker in section label for Chinese slides
            QString displaySection = sl.section;
            if (sl.isChinese)
                displaySection = QString::fromUtf8("中  ") + sl.section;
            // Card body: for Chinese slides show chars + translation (preferred)
            // or chars + pinyin as fallback; for English slides show text as-is.
            QString cardText = sl.text;
            if (sl.isChinese) {
                if (!sl.english.isEmpty())
                    cardText = sl.text + "\n\n" + sl.english;
                else if (!sl.pinyin.isEmpty())
                    cardText = sl.text + "\n\n" + sl.pinyin;
            }
            auto *btn = new SlideButton(
                displaySection, cardText, globalIdx, si,
                globalIdx + 1,          // 1-based slide number shown on card
                m_slideContainer);
            btn->setSectionColor(resolveSectionColor(sl.section, othersMap));
            btn->setFontColor(m_slideFontColor);
            connect(btn, &SlideButton::clicked,
                    this, &LyricsDock::onSlideClicked);
            connect(btn, &SlideButton::editRequested,
                    this, &LyricsDock::onSlideEditRequested);
            connect(btn, &SlideButton::bgAssignRequested,
                    this, &LyricsDock::onSlideBgAssign);
            connect(btn, &SlideButton::bgClearRequested,
                    this, &LyricsDock::onSlideBgClear);
            if (cardSeg && !cardSeg->backgroundFor(globalIdx).isEmpty())
                btn->setBackgroundThumb(cardSeg->backgroundFor(globalIdx));
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
    // Remember WHICH card is on air by its id, so edits that insert or remove cards cannot
    // move it (live mode records it where the slide is pushed).
    if (index < 0) { m_onAirSegId.clear(); m_onAirCardId.clear(); publishOnAir(); }
    else if (!m_liveMode && m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
        rememberOnAir(m_segments[m_activeSegIdx], index);
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
    updateEditSlidePanel();
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

void LyricsDock::applySlideBackground()
{
    QString newBg;

    if (m_liveMode) {
        // In live mode m_slides[] is not populated — read bg from the slide button itself.
        // Each button already has setBackgroundThumb called during buildAllLiveSlides.
        for (auto *btn : m_slideButtons) {
            if (btn->slideIndex() == m_activeSlide) {
                newBg = btn->backgroundPath();
                break;
            }
        }
    } else {
        if (m_activeSlide < 0 || m_activeSlide >= m_slides.size()) return;
        // The card's own background, else the segment's default (blank when neither).
        if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
            newBg = m_segments[m_activeSegIdx].backgroundFor(m_activeSlide);
    }

    showBackground(newBg);
}

// Shared by every slide-card activation.  The fade itself (photo/video cross-fade rules,
// duration from the global fade setting) lives in the renderer's applyBgPath(); this only
// decides WHEN to call it: never for the background already on air, clear for blank.
void LyricsDock::showBackground(const QString &newBg)
{
    const int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
    if (newBg.isEmpty()) {
        // A slide with no background of its own must not inherit the previous one.  Clear when
        // the plugin believes one is up, OR when the renderer says one is (several panels reset
        // the cache while their photo/video stays on air, which used to leave that media behind
        // the next slide).
        if (!m_currentBgPath.isEmpty() || lyrics_renderer_media_background_active()) {
            lyrics_renderer_media_clear(fadeMs);
            m_currentBgPath.clear();
            if (m_wsServer) m_wsServer->broadcastMediaState(QString());   // remote: nothing on air
        }
        return;
    }
    if (newBg == m_currentBgPath) return;
    lyrics_renderer_media_push(newBg, fadeMs);
    m_currentBgPath = newBg;
    if (m_wsServer) m_wsServer->broadcastMediaState(newBg);               // remote: this is on air
}

void LyricsDock::refreshSlideCardThumbs()
{
    const Segment *seg = (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
                         ? &m_segments[m_activeSegIdx] : nullptr;
    for (auto *btn : m_slideButtons)
        btn->setBackgroundThumb(seg ? seg->backgroundFor(btn->slideIndex()) : QString());
}

void LyricsDock::onSlideBgAssign(int slideIndex, bool forSegment)
{
    // "Current background" = whatever is selected in the media gallery.
    // If nothing is selected, applying = clearing (empty path).
    const QString chosen = m_sharedGallery ? m_sharedGallery->activeItemPath() : QString();

    if (m_activeSegIdx < 0 || m_activeSegIdx >= m_segments.size()) return;
    auto &seg = m_segments[m_activeSegIdx];
    if (seg.type != Segment::Type::Lyrics && seg.type != Segment::Type::Paragraphs) return;
    seg.syncCards();   // the card queue must match the text before a card is touched

    // Segment: the default every slide card inherits.  Card: that one card's own background.
    if (forSegment)          seg.defaultBackground = chosen;
    else                      seg.setCardBackground(slideIndex, chosen);

    saveServiceOrder();
    refreshAfterBackgroundChange();
}

void LyricsDock::onSlideBgClear(int slideIndex, bool forSegment)
{
    if (m_activeSegIdx < 0 || m_activeSegIdx >= m_segments.size()) return;
    auto &seg = m_segments[m_activeSegIdx];
    if (seg.type != Segment::Type::Lyrics && seg.type != Segment::Type::Paragraphs) return;
    seg.syncCards();   // the card queue must match the text before a card is touched

    // Segment: remove the default (cards with their own background keep it).
    // Card: remove that card's own background, so it inherits the segment default again.
    if (forSegment) seg.defaultBackground.clear();
    else            seg.setCardBackground(slideIndex, QString());

    saveServiceOrder();
    refreshAfterBackgroundChange();
}

void LyricsDock::refreshAfterBackgroundChange()
{
    if (m_activeSegIdx < 0 || m_activeSegIdx >= m_segments.size()) return;
    const Segment &seg = m_segments[m_activeSegIdx];
    if (m_paragraphMode) {
        const int savedActive = m_activeSlide;
        loadParagraphSlides(seg);
        if (savedActive >= 0 && savedActive < m_slideButtons.size())
            setActiveSlide(savedActive);
        // The card on air may now inherit or override a different background.
        if (savedActive >= 0) showBackground(seg.backgroundFor(savedActive));
        return;
    }
    // Lyrics: rebuild slide data only, not the buttons (destroying them causes a visual glitch).
    m_slides = m_currentSong.allSlides();
    refreshSlideCardThumbs();
    applySlideBackground();
}

void LyricsDock::pushCurrentSlide()
{
    if (m_activeSlide < 0 || m_activeSlide >= m_slides.size()) return;
    const LyricsSlide &sl  = m_slides[m_activeSlide];
    const QString     &title = m_currentSong.title;

    // This segment's colour preset goes into the renderer now, with the slide it colours.
    if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
        applySegmentPreset(m_segments[m_activeSegIdx]);

    applySlideBackground();  // transition background only when it actually changes

    if (sl.isChinese) {
        lyrics_renderer_push_chinese(sl.text, sl.pinyin, sl.english,
                                     sl.section, title);
    } else {
        lyrics_renderer_push(sl.text, sl.section, title);
    }

    // Broadcast global slide index so browser highlights the correct tile.
    // Use cached m_currentSongSlideOffset (set in onSequenceSongSelected)
    // instead of re-computing — avoids any timing mismatch with currentRow().
    if (m_wsServer) {
        int globalIdx = m_currentSongSlideOffset + m_activeSlide;
        m_wsServer->broadcastSlide(sl.text, sl.section, title, globalIdx);
    }
}

// ── Service Order slots ─────────────────────────────────────

void LyricsDock::onAddSegment()
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu{background:#1e1e24;color:#f0f0f5;border:1px solid #3a3a48;}"
        "QMenu::item:selected{background:#2a4a6a;}");
    struct { const char *label; Segment::Type type; } types[] = {
        {"Lyrics",       Segment::Type::Lyrics},
        {"Paragraphs",   Segment::Type::Paragraphs},
        {"Media",        Segment::Type::Media},
        {"Presentation", Segment::Type::Presentation},
    };
    for (const auto &t : types)
        menu.addAction(t.label);
    auto *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    Segment item;
    for (const auto &t : types)
        if (chosen->text() == t.label) { item.type = t.type; break; }
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Lyrics: open song library popup
    if (item.type == Segment::Type::Lyrics) {
        // First time lyrics are added after an update: upgrade the song library file once
        // (no-op afterwards).
        SongManager::instance().ensureCurrent();
        SongLibraryPopup popup(this);
        // The popup works in library song ids: tell it which songs the service already has.
        QList<QString> inService;
        for (const auto &s : m_segments)
            if (s.type == Segment::Type::Lyrics) inService << songIdOf(s);
        popup.setSequenceIds(inService);
        if (popup.exec() != QDialog::Accepted) return;
        // Deleting a song from the song library does NOT remove it from the service:
        // the segment is an independent copy.
        for (int i = m_segments.size() - 1; i >= 0; --i)
            if (m_segments[i].type == Segment::Type::Lyrics
                    && popup.removedIds().contains(songIdOf(m_segments[i])))
                removeSegment(i);
        for (const QString &id : popup.addedIds()) {
            bool already = false;
            for (const auto &s : m_segments)
                if (s.type == Segment::Type::Lyrics && songIdOf(s) == id) { already = true; break; }
            if (already) continue;
            auto *song = SongManager::instance().findById(id);
            if (!song) continue;
            Segment si;
            si.type         = Segment::Type::Lyrics;
            si.id           = newId();
            si.sourceSongId = id;
            si.title    = song->title;
            si.artist   = song->artist;
            si.songKey = song->key; si.songTempo = song->tempo; si.songCopyright = song->copyright;
            si.sections = song->sections;  // embed full lyrics into service
            si.syncCards();                // give every slide card its own id
            int insertRow = m_segmentList->currentRow() + 1;
            if (insertRow <= 0 || insertRow > m_segments.size())
                insertRow = m_segments.size();
            m_segments.insert(insertRow, si);
        }
        rebuildSegmentList();
        m_sequenceSongIds.clear();
        for (const auto &si : m_segments)
            if (si.type == Segment::Type::Lyrics) m_sequenceSongIds << si.id;
        saveServiceOrder();
        broadcastSequence();
        return;
    }

    // Non-Lyrics: ask for a title
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Add %1").arg(chosen->text()));
    dlg.setFixedSize(360, 130);
    dlg.setStyleSheet("QDialog{background:#1e1e24;color:#f0f0f5;}"
                      "QLineEdit{background:#16161a;color:#f0f0f5;border:1px solid #3a3a48;"
                      "border-radius:3px;padding:4px 6px;}"
                      "QPushButton{background:#1a3a5c;color:#4da6ff;border:1px solid #2a5a8c;"
                      "border-radius:4px;padding:4px 14px;}"
                      "QPushButton:hover{background:#254e7a;}");
    auto *dlgL = new QVBoxLayout(&dlg);
    dlgL->setContentsMargins(12, 10, 12, 10); dlgL->setSpacing(8);
    auto *lbl = new QLabel(QString("Title for this %1:").arg(chosen->text()));
    lbl->setStyleSheet("color:#aaa;font-size:11px;");
    auto *titleEdit = new QLineEdit;
    titleEdit->setPlaceholderText(QString("e.g. %1").arg(chosen->text()));
    auto *btnRow = new QHBoxLayout;
    auto *okBtn  = new QPushButton("Add");
    auto *cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet("QPushButton{background:#2a1a1a;color:#aaa;border:1px solid #3a2a2a;"
                             "border-radius:4px;padding:4px 14px;}");
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    dlgL->addWidget(lbl);
    dlgL->addWidget(titleEdit);
    dlgL->addLayout(btnRow);
    connect(okBtn,    &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn,&QPushButton::clicked, &dlg, &QDialog::reject);
    connect(titleEdit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
    if (dlg.exec() != QDialog::Accepted) return;
    item.title = titleEdit->text().trimmed();
    if (item.title.isEmpty())
        item.title = chosen->text();

    // A new paragraph segment gets its own copy of the current DEFAULT layout.
    ensureParagraphLayout(item);

    int insertRow = m_segmentList->currentRow() + 1;
    if (insertRow <= 0 || insertRow > m_segments.size())
        insertRow = m_segments.size();
    m_segments.insert(insertRow, item);
    rebuildSegmentList();
    saveServiceOrder();
    if (m_wsServer) m_wsServer->broadcastServiceOrder(buildServiceOrderJson());
    m_segmentList->setCurrentRow(insertRow);
}

void LyricsDock::onRemoveSegment()
{
    int row = m_segmentList->currentRow();
    if (row < 0) return;
    removeSegment(row);
    updateSegmentButtons();
}

void LyricsDock::onSegmentUp()
{
    int row = m_segmentList->currentRow();
    if (row <= 0) return;
    m_segments.swapItemsAt(row, row - 1);
    // Also swap in widget
    auto *taken = m_segmentList->takeItem(row);
    m_segmentList->insertItem(row - 1, taken);
    m_segmentList->setCurrentRow(row - 1);
    // Rebuild sequenceSongIds
    m_sequenceSongIds.clear();
    for (const auto &si : m_segments)
        if (si.type == Segment::Type::Lyrics) m_sequenceSongIds << si.id;
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::onSegmentDown()
{
    int row = m_segmentList->currentRow();
    if (row < 0 || row >= m_segments.size() - 1) return;
    m_segments.swapItemsAt(row, row + 1);
    auto *taken = m_segmentList->takeItem(row);
    m_segmentList->insertItem(row + 1, taken);
    m_segmentList->setCurrentRow(row + 1);
    m_sequenceSongIds.clear();
    for (const auto &si : m_segments)
        if (si.type == Segment::Type::Lyrics) m_sequenceSongIds << si.id;
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::onSegmentSelected(int row)
{
    m_activeSegIdx = row;
    updateSegmentButtons();
    if (row < 0 || row >= m_segments.size()) {
        if (!m_liveMode && m_contentStack)
            m_contentStack->setCurrentIndex(3);
        return;
    }
    if (m_liveMode) {
        // In live mode: scroll so the section separator is at the TOP of the viewport.
        if (row < m_liveSeparators.size() && m_slideArea) {
            int y = m_liveSeparators[row]->y();
            m_slideArea->verticalScrollBar()->setValue(qMax(0, y - 4));
        }
        return;
    }
    loadSegmentContent(row);
}

void LyricsDock::loadSegmentContent(int idx)
{
    if (idx < 0 || idx >= m_segments.size()) {
        if (m_contentStack) m_contentStack->setCurrentIndex(3);
        return;
    }
    // Make sure the segment's card queue matches its text before the cards are shown
    // (idempotent: nothing changes when it is already in step).
    m_segments[idx].syncCards();
    if (ensureParagraphLayout(m_segments[idx])) saveServiceOrder();   // paragraph segments own their layout
    const auto &item = m_segments[idx];
    // Show the segment's saved colour preset in the panel (card label colour + highlighted
    // button).  The RENDERER is deliberately not touched here: selecting a segment must not
    // recolour the slide that is on air.  The preset reaches the renderer when one of the
    // segment's slides is pushed (applySegmentPreset).
    if (item.colorPresetIdx >= 0 && item.colorPresetIdx < 6 && m_colorBtn[item.colorPresetIdx]) {
        m_slideFontColor = m_colorBtn[item.colorPresetIdx]->fontColor();
        // Highlight the matching button
        for (int j = 0; j < 6; ++j)
            if (m_colorBtn[j]) m_colorBtn[j]->setActive(j == item.colorPresetIdx);
    }

    switch (item.type) {
    case Segment::Type::Lyrics: {
        m_paragraphMode = false;
        m_contentStack->setCurrentIndex(0);
        int seqRow = m_sequenceSongIds.indexOf(item.id);
        if (seqRow >= 0)
            m_currentSongSlideOffset = computeGlobalSlideOffset(seqRow);
        Song song = songFromItem(item);
        if (!song.allSlides().isEmpty()) {
            m_currentSong = song;
            loadSongSlides(m_currentSong);
        } else {
            clearSlideGrid();
        }
        break;
    }
    case Segment::Type::Paragraphs:
        m_paragraphMode = true;
        m_contentStack->setCurrentIndex(0);
        loadParagraphSlides(item);
        break;
    case Segment::Type::Media:
        m_paragraphMode = false;
        m_contentStack->setCurrentIndex(1);
        if (m_videoTab) m_videoTab->setMediaList(item.mediaPaths);
        break;
    case Segment::Type::Presentation:
        m_paragraphMode = false;
        m_contentStack->setCurrentIndex(2);
        bindPptItem(idx);
        m_currentBgPath.clear();
        break;
    }
}

void LyricsDock::updateSegmentButtons()
{
    int row = m_segmentList ? m_segmentList->currentRow() : -1;
    bool has = (row >= 0 && row < m_segments.size());
    if (m_removeItemBtn) m_removeItemBtn->setEnabled(has);
    if (m_itemUpBtn)   m_itemUpBtn->setEnabled(has && row > 0);
    if (m_itemDownBtn) m_itemDownBtn->setEnabled(has && row < m_segments.size() - 1);
}

void LyricsDock::rebuildSegmentList()
{
    if (!m_segmentList) return;
    QSignalBlocker blk(m_segmentList);
    m_segmentList->clear();
    for (const auto &item : m_segments) {
        auto *lwi = new QListWidgetItem;
        lwi->setData(Qt::UserRole, QVariant::fromValue(item));
        lwi->setSizeHint({1, 52});
        m_segmentList->addItem(lwi);
    }
}

// ── loadParagraphSlides ───────────────────────────────────────

void LyricsDock::loadParagraphSlides(const Segment &item)
{
    clearSlideGrid();
    QStringList slides = item.parseSlides();
    if (slides.isEmpty()) {
        if (m_slideCounter) m_slideCounter->setText(QString::fromUtf8("—"));
        if (m_prevBtn) m_prevBtn->setEnabled(false);
        if (m_nextBtn) m_nextBtn->setEnabled(false);
        return;
    }
    int vpw   = m_slideArea ? qMax(80, m_slideArea->viewport()->width() - 16) : 580;
    int cols  = m_cardWidthScale;
    int cardW = qMax(80, (vpw - 8 * (cols - 1)) / cols);
    int row = 0, col = 0;
    for (int i = 0; i < slides.size(); ++i) {
        auto *btn = new SlideButton(item.title.left(12),
                                    slides[i], i, 0, i + 1, m_slideContainer);
        btn->setSectionColor(item.typeColor());
        btn->setFontColor(m_slideFontColor);
        btn->applyScale(m_slideScale, m_slideFontFamily, cardW);
        if (!item.backgroundFor(i).isEmpty())
            btn->setBackgroundThumb(item.backgroundFor(i));
        connect(btn, &SlideButton::clicked, this, &LyricsDock::onSlideClicked);
        connect(btn, &SlideButton::bgAssignRequested, this, &LyricsDock::onSlideBgAssign);
        connect(btn, &SlideButton::bgClearRequested,  this, &LyricsDock::onSlideBgClear);
        m_slideGrid->addWidget(btn, row, col);
        m_slideButtons << btn;
        if (++col >= cols) { col = 0; ++row; }
    }
    applyZoomToAll();
    if (m_slideCounter)
        m_slideCounter->setText(QString("0 / %1").arg(slides.size()));
    if (m_prevBtn) m_prevBtn->setEnabled(false);
    if (m_nextBtn) m_nextBtn->setEnabled(slides.size() > 1);
}

// ── buildAllLiveSlides ────────────────────────────────────────

void LyricsDock::buildAllLiveSlides()
{
    clearSlideGrid();
    m_paragraphMode = false;

    int vpw   = m_slideArea ? qMax(80, m_slideArea->viewport()->width() - 16) : 580;
    int cols  = m_cardWidthScale;
    int cardW = qMax(80, (vpw - 8 * (cols - 1)) / cols);

    int row = 0, col = 0;
    int globalIdx = 0;

    for (int segIdx = 0; segIdx < m_segments.size(); ++segIdx) {
        const auto &seg = m_segments[segIdx];

        // Flush current row before separator so it spans the full width
        if (col > 0) { col = 0; ++row; }

        // ── Separator row with segment title ──────────────────
        auto *sep = new QWidget(m_slideContainer);
        sep->setFixedHeight(28);
        const QColor tc = seg.typeColor();
        sep->setStyleSheet(
            QString("background: rgba(%1,%2,%3,22);")
            .arg(tc.red()).arg(tc.green()).arg(tc.blue()));

        auto *sepL = new QHBoxLayout(sep);
        sepL->setContentsMargins(0, 0, 6, 0);
        sepL->setSpacing(6);

        // Left colour stripe (mirrors column-1 delegate)
        auto *stripe = new QWidget(sep);
        stripe->setFixedWidth(3);
        stripe->setStyleSheet(QString("background: %1;").arg(tc.name()));
        sepL->addWidget(stripe);

        // First-character badge
        QString firstChar = seg.title.isEmpty()
            ? seg.typeLabel().left(1)
            : QString(seg.title.at(0));
        auto *badge = new QLabel(firstChar, sep);
        badge->setFixedSize(20, 20);
        badge->setAlignment(Qt::AlignCenter);
        QColor badgeBg = tc.darker(220); badgeBg.setAlpha(200);
        badge->setStyleSheet(
            QString("color: %1; background: rgba(%2,%3,%4,200);"
                    " border-radius: 4px; font-weight: bold; font-size: 11px;")
            .arg(tc.name())
            .arg(badgeBg.red()).arg(badgeBg.green()).arg(badgeBg.blue()));
        sepL->addWidget(badge);

        // Section title
        auto *lbl = new QLabel(
            seg.title.isEmpty() ? seg.typeLabel() : seg.title, sep);
        lbl->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;")
            .arg(tc.name()));
        sepL->addWidget(lbl, 1);
        m_slideGrid->addWidget(sep, row, 0, 1, cols);
        m_liveSeparators << sep;
        ++row;

        // Use this section's saved color preset for its slide buttons
        QColor segFontColor = m_slideFontColor;
        if (seg.colorPresetIdx >= 0 && seg.colorPresetIdx < 6
            && m_colorBtn[seg.colorPresetIdx])
            segFontColor = m_colorBtn[seg.colorPresetIdx]->fontColor();

        // ── Slides for this segment ───────────────────────────
        if (seg.type == Segment::Type::Lyrics) {
            Song song = songFromItem(seg);
            {
                const auto allSlides = song.allSlides();
                for (int i = 0; i < allSlides.size(); ++i) {
                    const auto &sl = allSlides[i];
                    auto *btn = new SlideButton(sl.section, sl.text,
                                               globalIdx, segIdx, i + 1, m_slideContainer);
                    btn->setFontColor(segFontColor);
                    btn->applyScale(m_slideScale, m_slideFontFamily, cardW);
                    // Background: the card's own, else the segment default (blank when none)
                    const QString bgPath = seg.backgroundFor(i);
                    if (!bgPath.isEmpty()) btn->setBackgroundThumb(bgPath);
                    connect(btn, &SlideButton::clicked, this, &LyricsDock::onSlideClicked);
                    m_slideGrid->addWidget(btn, row, col);
                    m_slideButtons << btn;
                    if (++col >= cols) { col = 0; ++row; }
                    ++globalIdx;
                }
            }
        } else if (seg.type == Segment::Type::Paragraphs) {
            QStringList slides = seg.parseSlides();
            for (int i = 0; i < slides.size(); ++i) {
                auto *btn = new SlideButton(seg.title.left(12), slides[i],
                                           globalIdx, segIdx, i + 1, m_slideContainer);
                btn->setSectionColor(seg.typeColor());
                btn->setFontColor(segFontColor);
                btn->applyScale(m_slideScale, m_slideFontFamily, cardW);
                if (!seg.backgroundFor(i).isEmpty())
                    btn->setBackgroundThumb(seg.backgroundFor(i));
                connect(btn, &SlideButton::clicked, this, &LyricsDock::onSlideClicked);
                m_slideGrid->addWidget(btn, row, col);
                m_slideButtons << btn;
                if (++col >= cols) { col = 0; ++row; }
                ++globalIdx;
            }
        } else if (seg.type == Segment::Type::Media) {
            for (int fi = 0; fi < seg.mediaPaths.size(); ++fi) {
                auto *card = new MediaSlideCard(seg.mediaPaths[fi], segIdx, fi, m_slideContainer);
                card->setFixedWidth(cardW);
                connect(card, &MediaSlideCard::clicked,          this, &LyricsDock::onLiveMediaCardClicked);
                connect(card, &MediaSlideCard::playPauseClicked, this, &LyricsDock::onLiveMediaPlayPause);
                connect(card, &MediaSlideCard::rewindClicked,    this, &LyricsDock::onLiveMediaRewind);
                m_slideGrid->addWidget(card, row, col);
                m_liveExtraCards << card;
                if (++col >= cols) { col = 0; ++row; }
            }
            if (seg.mediaPaths.isEmpty()) {
                // Placeholder card so media segments are visible in live mode
                auto *btn = new SlideButton("[Media]",
                                            seg.title.isEmpty() ? "(no media)" : seg.title,
                                            globalIdx, segIdx, 1, m_slideContainer);
                btn->setSectionColor(seg.typeColor());
                btn->applyScale(m_slideScale, m_slideFontFamily, cardW);
                m_slideGrid->addWidget(btn, row, col);
                m_slideButtons << btn;
                if (++col >= cols) { col = 0; ++row; }
                ++globalIdx;
            }
        } else if (seg.type == Segment::Type::Presentation) {
            auto *card = new PptLiveCard(seg.title, segIdx, m_slideContainer);
            card->setFixedWidth(cardW);
            connect(card, &PptLiveCard::activateRequested, this, &LyricsDock::onLivePptActivate);
            connect(card, &PptLiveCard::prevRequested,     this, &LyricsDock::onLivePptPrev);
            connect(card, &PptLiveCard::nextRequested,     this, &LyricsDock::onLivePptNext);
            m_slideGrid->addWidget(card, row, col);
            m_liveExtraCards << card;
            if (++col >= cols) { col = 0; ++row; }
        }
    }

    applyZoomToAll();
    if (m_slideCounter) m_slideCounter->setText(
        QString::fromUtf8("LIVE — %1 slides").arg(m_slideButtons.size()));
    if (m_prevBtn) m_prevBtn->setEnabled(false);
    if (m_nextBtn) m_nextBtn->setEnabled(m_slideButtons.size() > 1);
}

// ── onLiveToggled ─────────────────────────────────────────────

void LyricsDock::onLiveToggled(bool on)
{
    m_liveMode = on;

    // Keep button checked-states mutually exclusive
    if (m_liveToggleBtn) { QSignalBlocker b(m_liveToggleBtn); m_liveToggleBtn->setChecked(on); }
    if (m_editBtn)       { QSignalBlocker b(m_editBtn);       m_editBtn->setChecked(!on); }

    // Media bin disabled in live mode (audio bin is unaffected)
    if (m_sharedGallery) m_sharedGallery->setEnabled(!on);

    if (on) {
        m_currentBgPath.clear(); // reset so every section's background pushes fresh
        m_contentStack->setCurrentIndex(0);
        buildAllLiveSlides();
    } else {
        m_currentBgPath.clear();
        int row = m_segmentList ? m_segmentList->currentRow() : -1;
        if (row >= 0) loadSegmentContent(row);
        else m_contentStack->setCurrentIndex(3);
    }
}

// ── Live media card slots ─────────────────────────────────────

// Push a media file to OBS and update card visuals. Called only for a fresh start.
void LyricsDock::onLiveMediaCardClicked(int segIdx, int fileIdx)
{
    // Same card — handle resume / pause / play based on current state
    if (isLiveMedia(segIdx, fileIdx)) {
        if (m_liveMediaPaused) {
            // Resume: use play_pause API — never re-push (that crashes OBS)
            lyrics_renderer_mv_play_pause(false);
            m_liveMediaPlaying = true;
            m_liveMediaPaused  = false;
            for (auto *w : m_liveExtraCards) {
                if (auto *card = qobject_cast<MediaSlideCard*>(w))
                    if (card->segIdx() == segIdx && card->fileIdx() == fileIdx)
                        card->setPlaying(true, false);
            }
            return;
        }
        if (m_liveMediaPlaying) {
            // Already playing — treat click as pause
            onLiveMediaPlayPause(segIdx, fileIdx);
            return;
        }
        // Fully stopped on the same card — fall through to fresh start below
    } else {
        // Switching to a different card — stop current playback cleanly
        if (m_liveMediaPlaying || m_liveMediaPaused) {
            lyrics_renderer_mv_clear();
            lyrics_renderer_media_clear();
            for (auto *w : m_liveExtraCards) {
                if (auto *card = qobject_cast<MediaSlideCard*>(w))
                    if (isLiveMedia(card->segIdx(), card->fileIdx()))
                        card->setPlaying(false, false);
            }
        }
        setLiveMedia(segIdx, fileIdx);
        m_liveMediaPlaying = false;
        m_liveMediaPaused  = false;
    }

    // Stop any active presentation (mutual exclusion)
    if (m_pptTab && m_pptTab->activeType() >= 0) {
        m_pptTab->requestShow(-1);
        m_activePptSegId.clear();
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(false);
        }
    }

    // Fresh start
    if (segIdx < 0 || segIdx >= m_segments.size()) return;
    const QString &path = m_segments[segIdx].mediaPaths.value(fileIdx);
    if (path.isEmpty()) return;

    // Clear any displayed lyrics/CTW text so only the media is visible
    lyrics_renderer_clear();
    lyrics_renderer_paragraph_clear();
    m_currentBgPath.clear(); // force background re-push on next lyrics slide

    if (isVideoFile(path)) {
        lyrics_renderer_mv_video_push(path, 0);
    } else
        lyrics_renderer_media_push(path, 0);

    m_liveMediaPlaying = true;
    m_liveMediaPaused  = false;

    for (auto *w : m_liveExtraCards) {
        if (auto *card = qobject_cast<MediaSlideCard*>(w))
            card->setPlaying(card->segIdx() == segIdx && card->fileIdx() == fileIdx, false);
    }
    setActiveSlide(-1);                          // no lyrics/paragraph slide is on air now ...
    rememberOnAir(m_segments[segIdx], fileIdx);  // ... this media card is (the remote follows it)
}

void LyricsDock::onLiveMediaPlayPause(int segIdx, int fileIdx)
{
    if (!isLiveMedia(segIdx, fileIdx)) {
        onLiveMediaCardClicked(segIdx, fileIdx);
        return;
    }

    if (m_liveMediaPaused) {
        // Resume
        if (segIdx >= 0 && segIdx < m_segments.size()) {
            const QString &p = m_segments[segIdx].mediaPaths.value(fileIdx);
            if (isVideoFile(p)) {
                // Video: resume via play_pause API (never re-push)
                lyrics_renderer_mv_play_pause(false);
            } else {
                // Image: re-push (it was cleared on pause — safe for images)
                lyrics_renderer_media_push(p, 0);
            }
        }
        m_liveMediaPlaying = true;
        m_liveMediaPaused  = false;
        for (auto *w : m_liveExtraCards) {
            if (auto *card = qobject_cast<MediaSlideCard*>(w))
                if (card->segIdx() == segIdx && card->fileIdx() == fileIdx)
                    card->setPlaying(true, false);
        }
        return;
    }

    if (!m_liveMediaPlaying) {
        // Fully stopped — fresh start
        onLiveMediaCardClicked(segIdx, fileIdx);
        return;
    }

    // Currently playing — pause it
    if (segIdx >= 0 && segIdx < m_segments.size()) {
        const QString &p = m_segments[segIdx].mediaPaths.value(fileIdx);
        if (isVideoFile(p))
            lyrics_renderer_mv_play_pause(true);
        else
            lyrics_renderer_media_clear();
    }
    m_liveMediaPlaying = false;
    m_liveMediaPaused  = true;

    for (auto *w : m_liveExtraCards) {
        if (auto *card = qobject_cast<MediaSlideCard*>(w))
            if (card->segIdx() == segIdx && card->fileIdx() == fileIdx)
                card->setPlaying(true, true);
    }
}

void LyricsDock::onLiveMediaRewind(int segIdx, int fileIdx)
{
    if (segIdx < 0 || segIdx >= m_segments.size()) return;
    const auto &item = m_segments[segIdx];
    if (fileIdx < 0 || fileIdx >= item.mediaPaths.size()) return;

    if (isLiveMedia(segIdx, fileIdx)) {
        // Seek to start; if paused stay paused (user can then press play)
        if (isVideoFile(item.mediaPaths[fileIdx])) {
            lyrics_renderer_mv_set_time(0);
            if (m_liveMediaPaused) {
                // Keep paused at 0 — card already shows "Resume"
            }
        } else {
            // For images just re-push to refresh
            lyrics_renderer_media_push(item.mediaPaths[fileIdx], 0);
            m_liveMediaPlaying = true;
            m_liveMediaPaused  = false;
            for (auto *w : m_liveExtraCards) {
                if (auto *card = qobject_cast<MediaSlideCard*>(w))
                    if (card->segIdx() == segIdx && card->fileIdx() == fileIdx)
                        card->setPlaying(true, false);
            }
        }
    } else {
        // Rewind on an inactive card — start it from the beginning
        onLiveMediaCardClicked(segIdx, fileIdx);
    }
}

// ── Live presentation card slots ──────────────────────────────

// ── On-air tracking by id ─────────────────────────────────────
// What is on air is remembered by id, not by position, so inserting / removing / reordering
// cards or segments cannot silently move it.

void LyricsDock::rememberOnAir(const Segment &seg, int slideIdx)
{
    m_onAirSegId  = seg.id;
    m_onAirCardId = seg.cardIdAt(slideIdx);
    publishOnAir();
}

// What the Live Preview remote shows and navigates from.  A presentation that is showing wins;
// otherwise it is the on-air slide / media card.  Sent only when it actually changed.
void LyricsDock::publishOnAir()
{
    if (!m_wsServer) return;
    int segIdx = -1, slideIdx = -1;
    QString segId, cardId, kind;

    if (m_pptTab && m_pptTab->activeType() >= 0) {
        segIdx = segmentIndexById(m_pptBoundSegId);
        if (segIdx >= 0) { segId = m_pptBoundSegId; slideIdx = 0; kind = "presentation"; }
    }
    if (segIdx < 0 && !m_onAirCardId.isEmpty()) {
        segIdx = segmentIndexById(m_onAirSegId);
        if (segIdx >= 0) {
            const Segment &s = m_segments[segIdx];
            slideIdx = s.cardIndexById(m_onAirCardId);
            if (slideIdx >= 0) { segId = s.id; cardId = m_onAirCardId; kind = s.typeLabel().toLower(); }
            else               { segIdx = -1; }
        }
    }

    QJsonObject o;
    o["type"]     = "onAir";
    o["segIdx"]   = segIdx;
    o["segId"]    = segId;
    o["slideIdx"] = slideIdx;
    o["cardId"]   = cardId;
    o["kind"]     = kind;
    const QByteArray key = QJsonDocument(o).toJson(QJsonDocument::Compact);
    if (key == m_lastOnAirKey) return;
    m_lastOnAirKey = key;
    m_wsServer->broadcastOnAir(o);
}

bool LyricsDock::resolveOnAir(int &segIdx, int &cardIdx) const
{
    if (m_onAirCardId.isEmpty()) return false;
    for (int i = 0; i < m_segments.size(); ++i) {
        if (m_segments[i].id != m_onAirSegId) continue;
        const int c = m_segments[i].cardIndexById(m_onAirCardId);
        if (c < 0) return false;
        segIdx = i;
        cardIdx = c;
        return true;
    }
    return false;
}

// The queue is: segments in service order, each with its slide cards in on-air order (Lyrics,
// Paragraphs and Media contribute one stop per card; a Presentation is one stop).  Next/Previous
// move to the neighbouring stop, crossing into the next / previous segment at its end.  A
// presentation on air never reaches here (it keeps Next/Previous for itself).
bool LyricsDock::navigateQueue(int delta)
{
    auto stops = [](const Segment &s) -> int {
        switch (s.type) {
        case Segment::Type::Lyrics:
        case Segment::Type::Paragraphs:
        case Segment::Type::Media:        return (int)s.cards.size();
        case Segment::Type::Presentation: return 1;
        }
        return 0;
    };

    int segIdx = -1, cardIdx = -1;
    if (!resolveOnAir(segIdx, cardIdx)) {
        // Nothing on air yet: Next starts at the first stop, Previous at the last one.
        segIdx  = (delta > 0) ? -1 : (int)m_segments.size();
        cardIdx = 0;
    }

    int ts = -1, tc = -1;
    if (delta > 0) {
        if (segIdx >= 0 && segIdx < m_segments.size() && cardIdx + 1 < stops(m_segments[segIdx])) {
            ts = segIdx; tc = cardIdx + 1;
        } else {
            for (int s = segIdx + 1; s < m_segments.size(); ++s)
                if (stops(m_segments[s]) > 0) { ts = s; tc = 0; break; }
        }
    } else {
        if (segIdx >= 0 && segIdx < m_segments.size() && cardIdx - 1 >= 0) {
            ts = segIdx; tc = cardIdx - 1;
        } else {
            for (int s = segIdx - 1; s >= 0; --s)
                if (stops(m_segments[s]) > 0) { ts = s; tc = stops(m_segments[s]) - 1; break; }
        }
    }
    if (ts < 0) return false;            // already at the end (or the start) of the service
    activateSlideFromRemote(ts, tc);
    return true;
}

// Where the on-air slide card sits NOW inside `seg` (-1 = nothing on air here, or its card was
// deleted).  Call it before the slide grid is rebuilt: the rebuild resets m_activeSlide.
int LyricsDock::onAirIndexIn(const Segment &seg) const
{
    if (m_activeSlide < 0) return -1;                      // nothing on air in this view
    if (m_onAirSegId == seg.id && !m_onAirCardId.isEmpty())
        return seg.cardIndexById(m_onAirCardId);           // follow the card by its id
    return m_activeSlide;                                  // no id recorded: keep the position
}

int LyricsDock::liveGlobalIndexOnAir() const
{
    if (m_onAirCardId.isEmpty()) return -1;
    int global = 0;
    for (const auto &seg : m_segments) {
        if (seg.type == Segment::Type::Lyrics || seg.type == Segment::Type::Paragraphs) {
            if (seg.id == m_onAirSegId) {
                const int i = seg.cardIndexById(m_onAirCardId);
                return i >= 0 ? global + i : -1;
            }
            global += seg.cards.size();          // one live slide button per slide card
        } else if (seg.type == Segment::Type::Media && seg.mediaPaths.isEmpty()) {
            ++global;                            // the placeholder card of an empty media segment
        }
    }
    return -1;
}

bool LyricsDock::isLiveMedia(int segIdx, int fileIdx) const
{
    if (segIdx < 0 || segIdx >= m_segments.size() || m_liveMediaCardId.isEmpty()) return false;
    const Segment &seg = m_segments[segIdx];
    return seg.id == m_liveMediaSegId && seg.cardIdAt(fileIdx) == m_liveMediaCardId;
}

void LyricsDock::setLiveMedia(int segIdx, int fileIdx)
{
    if (segIdx < 0 || segIdx >= m_segments.size()) { clearLiveMedia(); return; }
    m_segments[segIdx].syncCards();   // the media card queue must be in step before it is used
    m_liveMediaSegId  = m_segments[segIdx].id;
    m_liveMediaCardId = m_segments[segIdx].cardIdAt(fileIdx);
}

void LyricsDock::clearLiveMedia()
{
    m_liveMediaSegId.clear();
    m_liveMediaCardId.clear();
}

QString LyricsDock::liveMediaPath() const
{
    if (m_liveMediaCardId.isEmpty()) return QString();
    for (const auto &seg : m_segments) {
        if (seg.id != m_liveMediaSegId) continue;
        const int i = seg.cardIndexById(m_liveMediaCardId);
        return i >= 0 ? seg.mediaPaths.value(i) : QString();
    }
    return QString();
}

bool LyricsDock::isLivePpt(int segIdx) const
{
    return segIdx >= 0 && segIdx < m_segments.size()
        && !m_activePptSegId.isEmpty() && m_segments[segIdx].id == m_activePptSegId;
}

void LyricsDock::setLivePpt(int segIdx)
{
    if (segIdx >= 0 && segIdx < m_segments.size()) m_activePptSegId = m_segments[segIdx].id;
}

bool LyricsDock::ensureParagraphLayout(Segment &seg)
{
    if (seg.type != Segment::Type::Paragraphs) return false;
    const QJsonObject def = QJsonDocument::fromJson(lyrics_renderer_paragraph_layout_snapshot()).object();
    bool changed = false;
    for (auto it = def.constBegin(); it != def.constEnd(); ++it) {
        if (!seg.paragraphLayout.contains(it.key())) {
            seg.paragraphLayout.insert(it.key(), it.value());
            changed = true;
        }
    }
    return changed;
}

void LyricsDock::syncAllCards()
{
    for (auto &seg : m_segments) seg.syncCards();
}

int LyricsDock::segmentIndexById(const QString &id) const
{
    if (id.isEmpty()) return -1;
    for (int i = 0; i < m_segments.size(); ++i)
        if (m_segments[i].id == id) return i;
    return -1;
}

// Every path that shows a Presentation item (edit-mode select, remote activate,
// live cards) goes through here so the shared capture always follows the item's
// own saved window and COM setting — never whatever the previous item left behind.
void LyricsDock::bindPptItem(int segIdx)
{
    if (!m_pptTab || segIdx < 0 || segIdx >= m_segments.size()) return;
    const Segment &it = m_segments[segIdx];
    if (it.type != Segment::Type::Presentation) return;
    m_pptBoundSegId = it.id;
    PptBinding b;
    b.title   = it.windowTitle;
    b.exe     = it.windowProc;
    b.cls     = it.windowCls;
    b.comMode = it.pptComMode;
    m_pptTab->bindItem(b);
}

void LyricsDock::onLivePptActivate(int segIdx)
{
    if (!m_pptTab) return;

    // Stop any live media first (mutual exclusion with media cards)
    if (m_liveMediaPlaying || m_liveMediaPaused) {
        lyrics_renderer_mv_clear();
        lyrics_renderer_media_clear();
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<MediaSlideCard*>(w))
                c->setPlaying(false, false);
        }
        m_liveMediaPlaying = false;
        m_liveMediaPaused  = false;
        clearLiveMedia();
    }

    // Toggle: clicking an already-active card stops it
    if (isLivePpt(segIdx) && m_pptTab->activeType() >= 0) {
        m_pptTab->requestShow(-1); // stop
        m_activePptSegId.clear();
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(false);
        }
        return;
    }

    // Presentation takes over the screen — background is no longer ours to track
    m_currentBgPath.clear();

    // Start / switch to this card: always rebind so P2 never shows P1's window
    bindPptItem(segIdx);
    setLivePpt(segIdx);

    for (auto *w : m_liveExtraCards) {
        if (auto *c = qobject_cast<PptLiveCard*>(w))
            c->setActive(c->segIdx() == segIdx);
    }
}

void LyricsDock::onLivePptPrev(int segIdx)
{
    if (!m_pptTab) return;
    // Activate without toggle-off if not already showing for this card
    if (!isLivePpt(segIdx) || !m_pptTab->isActive()) {
        // Stop any live media (mutual exclusion)
        if (m_liveMediaPlaying || m_liveMediaPaused) {
            lyrics_renderer_mv_clear();
            lyrics_renderer_media_clear();
            for (auto *w : m_liveExtraCards) {
                if (auto *c = qobject_cast<MediaSlideCard*>(w))
                    c->setPlaying(false, false);
            }
            m_liveMediaPlaying = m_liveMediaPaused = false;
            clearLiveMedia();
        }
        bindPptItem(segIdx);
        setLivePpt(segIdx);
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(c->segIdx() == segIdx);
        }
    }
    pptPrev();
}

void LyricsDock::onLivePptNext(int segIdx)
{
    if (!m_pptTab) return;
    // Activate without toggle-off if not already showing for this card
    if (!isLivePpt(segIdx) || !m_pptTab->isActive()) {
        // Stop any live media (mutual exclusion)
        if (m_liveMediaPlaying || m_liveMediaPaused) {
            lyrics_renderer_mv_clear();
            lyrics_renderer_media_clear();
            for (auto *w : m_liveExtraCards) {
                if (auto *c = qobject_cast<MediaSlideCard*>(w))
                    c->setPlaying(false, false);
            }
            m_liveMediaPlaying = m_liveMediaPaused = false;
            clearLiveMedia();
        }
        bindPptItem(segIdx);
        setLivePpt(segIdx);
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(c->segIdx() == segIdx);
        }
    }
    pptNext();
}

// ── onParagraphsContextMenu ───────────────────────────────────

void LyricsDock::onParagraphsContextMenu(const QPoint &pos)
{
    if (!m_paragraphMode) return;
    int row = m_activeSegIdx;
    if (row < 0 || row >= m_segments.size()) return;
    if (m_segments[row].type != Segment::Type::Paragraphs) return;
    QMenu menu(this);
    auto *editAct   = menu.addAction("Edit slides...");
    auto *layoutAct = menu.addAction("Layout for this segment...");
    auto *chosen = menu.exec(m_slideArea ? m_slideArea->mapToGlobal(pos) : QCursor::pos());
    if (chosen == editAct)        showParagraphEditDialog(row);
    else if (chosen == layoutAct) showSegmentLayoutDialog(row);
}

// ── showParagraphEditDialog ───────────────────────────────────

void LyricsDock::showParagraphEditDialog(int segIdx)
{
    if (segIdx < 0 || segIdx >= m_segments.size()) return;
    auto &item = m_segments[segIdx];

    QDialog dlg(this);
    dlg.setWindowTitle("Edit Paragraphs — " + item.title);
    dlg.resize(480, 400);
    dlg.setStyleSheet(
        "QDialog{background:#1e1e24;color:#f0f0f5;}"
        "QLabel{color:#aaa;font-size:10px;}"
        "QLineEdit,QPlainTextEdit{background:#16161a;color:#eee;border:1px solid #3a3a48;"
        "border-radius:3px;padding:4px;}"
        "QPushButton{background:#1a3a5c;color:#4da6ff;border:1px solid #2a5a8c;"
        "border-radius:4px;padding:4px 14px;}"
        "QPushButton:hover{background:#254e7a;}");
    auto *dlgL = new QVBoxLayout(&dlg);
    dlgL->setContentsMargins(12, 10, 12, 10); dlgL->setSpacing(8);

    auto *titleLbl = new QLabel("Title:");
    auto *titleEdit = new QLineEdit(item.title);
    auto *bodyLbl  = new QLabel("Text (blank line = new slide):");
    auto *bodyEdit = new QPlainTextEdit(item.text);
    bodyEdit->setPlaceholderText(
        "First slide text\n\nSecond slide text\n\nThird slide...");

    // Alternate sentence color row
    auto *altRow    = new QHBoxLayout;
    auto *altCheck  = new QCheckBox("Alternate sentence color");
    altCheck->setChecked(item.altSentenceColor);
    auto *presetLbl   = new QLabel("Color Preset:");
    auto *presetCombo = new QComboBox;
    presetCombo->setStyleSheet(
        "QComboBox{background:#16161a;color:#eee;border:1px solid #3a3a48;"
        "border-radius:3px;padding:2px 6px;min-width:60px;}"
        "QComboBox::drop-down{border:none;}"
        "QComboBox QAbstractItemView{background:#1e1e24;color:#eee;selection-background-color:#254e7a;}");
    for (int i = 1; i <= 6; ++i)
        presetCombo->addItem(QString::number(i), i - 1);
    int initPreset = (item.altSentencePreset >= 0 && item.altSentencePreset < 6)
                     ? item.altSentencePreset : 0;
    presetCombo->setCurrentIndex(initPreset);
    presetCombo->setEnabled(item.altSentenceColor);
    connect(altCheck, &QCheckBox::toggled, presetCombo, &QComboBox::setEnabled);
    altRow->addWidget(altCheck);
    altRow->addSpacing(10);
    altRow->addWidget(presetLbl);
    altRow->addWidget(presetCombo);
    altRow->addStretch();
    auto *layoutBtn = new QPushButton("Segment layout...");
    layoutBtn->setToolTip("This segment's own layout (font, size, margins ...). Same as the Layout button "
                          "at the top of the slide panel. The DEFAULT for NEW paragraph segments is "
                          "edited in the OBS source's Properties.");
    altRow->addWidget(layoutBtn);
    connect(layoutBtn, &QPushButton::clicked, &dlg, [this, segIdx](){ showSegmentLayoutDialog(segIdx); });

    auto *btnRow = new QHBoxLayout;
    auto *applyBtn  = new QPushButton("Apply");
    auto *cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet("QPushButton{background:#2a1a1a;color:#aaa;border:1px solid #3a2a2a;"
                             "border-radius:4px;padding:4px 14px;}");
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(applyBtn);

    dlgL->addWidget(titleLbl);
    dlgL->addWidget(titleEdit);
    dlgL->addWidget(bodyLbl);
    dlgL->addWidget(bodyEdit, 1);
    dlgL->addLayout(altRow);
    dlgL->addLayout(btnRow);

    connect(applyBtn,  &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    item.title             = titleEdit->text().trimmed();
    if (item.title.isEmpty()) item.title = "Paragraphs";
    item.text              = bodyEdit->toPlainText();
    item.altSentenceColor  = altCheck->isChecked();
    item.altSentencePreset = presetCombo->currentData().toInt();
    item.syncCards();   // keep each slide card's id and background across the text edit
    updateSegmentListItem(segIdx);
    saveServiceOrder();

    if (m_activeSegIdx == segIdx) {
        // Find the on-air card by id BEFORE the grid is rebuilt (the rebuild resets the active
        // slide); its position may have moved if slides were added or removed above it.
        const int onAir = onAirIndexIn(item);
        loadParagraphSlides(item);
        if (onAir >= 0 && onAir < m_slideButtons.size())
            setActiveSlide(onAir);
        // Re-push it so text / alternate-colour changes show immediately
        if (onAir >= 0) {
            QStringList slides = item.parseSlides();
            if (onAir < slides.size()) {
                applyParagraphStyle(item);
                lyrics_renderer_paragraph_push(item.title, slides[onAir]);
            }
        }
    }
}

// ── showSegmentLayoutDialog ───────────────────────────────────
// Per-segment paragraph layout.  Only the settings the user changes are stored on the
// segment; everything else keeps following the global paragraph layout.
void LyricsDock::showSegmentLayoutDialog(int segIdx)
{
    if (segIdx < 0 || segIdx >= m_segments.size()) return;
    Segment &seg = m_segments[segIdx];
    if (seg.type != Segment::Type::Paragraphs) return;
    if (ensureParagraphLayout(seg)) saveServiceOrder();   // show the segment's own layout

    QWidget *par = QApplication::activeModalWidget() ? QApplication::activeModalWidget()
                                                     : static_cast<QWidget *>(this);
    LayoutDialog dlg("paragraph", par);
    dlg.setSegmentMode(seg.title, seg.layoutBytes());
    if (dlg.exec() != QDialog::Accepted) return;

    seg.paragraphLayout = QJsonDocument::fromJson(dlg.segmentLayoutJson()).object();
    saveServiceOrder();

    // If a slide of this segment is on air, show the new layout straight away.
    if (m_activeSegIdx == segIdx && !m_liveMode && m_activeSlide >= 0)
        applyParagraphStyle(seg);
}

// ── buildMediaPanel ───────────────────────────────────────────

QWidget *LyricsDock::buildMediaPanel()
{
    return m_videoTab;
}

void LyricsDock::saveServiceOrder()
{
    QJsonObject root;
    root["version"]     = 2;   // 2 = segments carry ids and slide cards
    root["activeIndex"] = m_activeSegIdx;
    QJsonArray arr;
    for (const auto &item : m_segments)
        arr.append(item.toJson());
    root["segments"] = arr;

    QString path = QStandardPaths::writableLocation(
                       QStandardPaths::AppDataLocation) + "/service.json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());

    // Keep the remote's queue (the list it shows and navigates) up to date after every change.
    if (m_wsServer) m_wsServer->broadcastServiceOrder(buildServiceOrderJson());
}

void LyricsDock::loadServiceOrder()
{
    QString path = QStandardPaths::writableLocation(
                       QStandardPaths::AppDataLocation) + "/service.json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        // Migrate from legacy state: build Song-only service from old sequence
        QString legacyPath = QStandardPaths::writableLocation(
                                 QStandardPaths::AppDataLocation)
                             + "/lyrics-dock-state.json";
        QFile lf(legacyPath);
        if (lf.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(lf.readAll());
            if (!doc.isNull()) {
                for (const auto &v : doc.object()["sequence"].toArray()) {
                    QString id = v.toString();
                    auto *song = SongManager::instance().findById(id);
                    if (!song) continue;
                    Segment si;
                    si.type         = Segment::Type::Lyrics;
                    si.id           = newId();
                    si.sourceSongId = id;
                    si.title  = song->title;
                    si.artist = song->artist;
                    si.songKey = song->key; si.songTempo = song->tempo; si.songCopyright = song->copyright;
                    si.sections = song->sections;
                    si.syncCards();
                    m_segments << si;
                    m_sequenceSongIds << si.id;
                }
            }
        }
        rebuildSegmentList();
        return;
    }

    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (doc.isNull()) { rebuildSegmentList(); return; }

    const auto obj = doc.object();
    m_segments.clear();
    m_sequenceSongIds.clear();

    bool migrated = false;   // the file was in the pre-id form; it is rewritten below
    for (const auto &v : segmentsArray(obj)) {
        if (!v.isObject()) continue;
        Segment item = Segment::fromJson(v.toObject());
        if (item.type == Segment::Type::Lyrics) {
            // If no sections embedded yet (old service.json), copy from library
            if (item.sections.isEmpty()) {
                if (auto *song = SongManager::instance().findById(item.sourceSongId)) {
                    item.sections = song->sections;
                    item.title    = song->title;
                    item.artist   = song->artist;
                    item.songKey = song->key; item.songTempo = song->tempo; item.songCopyright = song->copyright;
                    item.syncCards();
                    migrated = true;
                }
            }
            m_sequenceSongIds << item.id;
        }
        migrated = migrated || item.needsSave;
        m_segments << item;
    }

    rebuildSegmentList();

    int savedIdx = obj["activeIndex"].toInt(-1);
    if (savedIdx >= 0 && savedIdx < m_segments.size()) {
        m_segmentList->setCurrentRow(savedIdx);
        m_activeSegIdx = savedIdx;
        // For Presentation items: restore the active UI state (enables buttons)
        // without touching OBS sources — sources are set up when the user interacts
        // or on the first real requestShow call.
        if (m_pptTab && m_segments[savedIdx].type == Segment::Type::Presentation) {
            // Remember which item owns the capture and its saved window so the first
            // real show resolves that window instead of auto-detecting.
            const Segment &it = m_segments[savedIdx];
            m_pptBoundSegId = it.id;
            PptBinding b;
            b.title = it.windowTitle; b.exe = it.windowProc;
            b.cls = it.windowCls;     b.comMode = it.pptComMode;
            m_pptTab->setBinding(b);
            m_pptTab->restoreActiveUi();
        }
    }

    if (migrated) {
        // Old file (no segment / slide-card ids yet): keep the original once, then save the
        // new form straight away so the ids minted above are the ones every later run sees.
        const QString bak = QFileInfo(path).absolutePath() + "/service.v1.bak.json";
        if (!QFile::exists(bak)) QFile::copy(path, bak);
        saveServiceOrder();
    }
}

void LyricsDock::onSequenceEditSong()
{
    int row = m_activeSegIdx;
    if (row < 0 || row >= m_segments.size()) return;
    if (m_segments[row].type != Segment::Type::Lyrics) return;

    // Build a Song from embedded sections (with library fallback)
    Song current = songFromItem(m_segments[row]);

    SongEditDialog dlg(current, this);
    dlg.setSegmentMode(true);
    if (dlg.exec() != QDialog::Accepted) return;

    Song res = dlg.result();
    // The segment is an independent copy: the library changes only when the user chose
    // "Save to Library" (replace) or "Save as New Song".
    if (dlg.savedAsNew())                   SongManager::instance().addSong(res);
    else if (!dlg.appliedToSegmentOnly())   SongManager::instance().upsertSong(res);

    // Update the segment's own copy.  "Save as New Song" only changes which library song the
    // segment points at; the segment keeps its own id.
    if (dlg.savedAsNew()) m_segments[row].sourceSongId = res.id;
    m_segments[row].title         = res.title;
    m_segments[row].artist        = res.artist;
    m_segments[row].songKey       = res.key;
    m_segments[row].songTempo     = res.tempo;
    m_segments[row].songCopyright = res.copyright;
    m_segments[row].sections      = res.sections;
    m_segments[row].syncCards();   // keep every slide card's id and background
    updateSegmentListItem(row);

    int prevActive = onAirIndexIn(m_segments[row]);   // the on-air card, found by id
    m_currentSong = res;
    loadSongSlides(m_currentSong);
    if (prevActive >= 0 && prevActive < m_slides.size()) {
        setActiveSlide(prevActive);
        pushCurrentSlide();
    }
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::onSegmentContextMenu(const QPoint &pos)
{
    auto *item = m_segmentList->itemAt(pos);
    if (!item) return;
    int row = m_segmentList->row(item);
    m_segmentList->setCurrentRow(row);
    if (row < 0 || row >= m_segments.size()) return;

    QMenu menu(m_segmentList);
    auto *removeAct = menu.addAction(
        style()->standardIcon(QStyle::SP_TrashIcon), "Remove");
    QAction *editAct    = nullptr;
    QAction *renameAct  = nullptr;
    const auto &seg = m_segments[row];
    if (seg.type == Segment::Type::Lyrics)
        editAct = menu.addAction(makePenIcon(), "Edit Song");
    else
        renameAct = menu.addAction(makePenIcon(), "Rename");

    auto *chosen = menu.exec(m_segmentList->viewport()->mapToGlobal(pos));
    if (chosen == removeAct) {
        onRemoveSegment();
    } else if (editAct && chosen == editAct) {
        onSequenceEditSong();
    } else if (renameAct && chosen == renameAct) {
        bool ok = false;
        QString newTitle = QInputDialog::getText(
            this, "Rename Segment",
            "New name:", QLineEdit::Normal,
            m_segments[row].title, &ok);
        if (ok && !newTitle.trimmed().isEmpty()) {
            m_segments[row].title = newTitle.trimmed();
            updateSegmentListItem(row);
            saveServiceOrder();
            broadcastSequence();
            // Refresh panel if this segment is currently loaded
            if (row == m_activeSegIdx && !m_liveMode)
                loadSegmentContent(row);
        }
    }
}

// ── Slide slots ───────────────────────────────────────────────

void LyricsDock::onSlideClicked(int index)
{
    // Stop any active presentation when a lyrics/paragraph slide is clicked
    // (not in live mode — live mode handles PPT separately via PptLiveCard)
    if (!m_liveMode && m_pptTab && m_pptTab->activeType() >= 0)
        m_pptTab->requestShow(-1);

    if (m_paragraphMode && !m_liveMode) {
        // Paragraphs: push slide text via CTW renderer
        setActiveSlide(index);
        if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size()) {
            const auto &item = m_segments[m_activeSegIdx];
            QStringList slides = item.parseSlides();
            if (index >= 0 && index < slides.size()) {
                // Card's own background, else the segment default (blank clears)
                showBackground(item.backgroundFor(index));
                applyParagraphStyle(item);
                lyrics_renderer_paragraph_push(item.title, slides[index]);
                if (m_wsServer) m_wsServer->broadcastScripture(item.title, slides[index]);
                if (m_wsServer) m_wsServer->broadcastActiveMode("paragraph");
            }
        }
        return;
    }

    if (m_liveMode) {
        // Clicking a lyrics/paragraph slide while media is playing stops the media first.
        if (m_liveMediaPlaying || m_liveMediaPaused) {
            lyrics_renderer_mv_clear();
            // For video: must clear now to stop playback.
            // For image: keep it — the slide's bg push will replace it (no black flash).
            bool wasVideo = false;
            {
                const QString p = liveMediaPath();   // looked up by id
                wasVideo = isVideoFile(p);
                if (!wasVideo && !p.isEmpty() && m_currentBgPath.isEmpty())
                    m_currentBgPath = p; // let bg block compare against this image
            }
            if (wasVideo)
                lyrics_renderer_media_clear();
            for (auto *w : m_liveExtraCards)
                if (auto *c = qobject_cast<MediaSlideCard*>(w))
                    c->setPlaying(false, false);
            m_liveMediaPlaying = false;
            m_liveMediaPaused  = false;
            clearLiveMedia();
        }

        // Clicking a lyrics/paragraph slide stops any active presentation
        if (m_pptTab && m_pptTab->activeType() >= 0) {
            m_pptTab->requestShow(-1);
            m_activePptSegId.clear();
            for (auto *w : m_liveExtraCards) {
                if (auto *c = qobject_cast<PptLiveCard*>(w))
                    c->setActive(false);
            }
        }

        // In live mode, index is a flat global index across all items
        setActiveSlide(index);  // highlight button
        // Find which segment + slide this index corresponds to
        int globalIdx = 0;
        for (int segIdx = 0; segIdx < m_segments.size(); ++segIdx) {
            const auto &seg = m_segments[segIdx];
            if (seg.type == Segment::Type::Lyrics) {
                Song song = songFromItem(seg);
                const auto allSlides = song.allSlides();
                if (!allSlides.isEmpty() && index < globalIdx + allSlides.size()) {
                    int localIdx = index - globalIdx;
                    m_currentSong = song;
                    m_currentSongSlideOffset = globalIdx;
                    m_slides = allSlides;

                    // Apply section color preset to OBS before pushing
                    if (seg.colorPresetIdx >= 0 && seg.colorPresetIdx < 6
                            && m_colorBtn[seg.colorPresetIdx]) {
                        QColor fc = m_colorBtn[seg.colorPresetIdx]->fontColor();
                        QColor oc = m_colorBtn[seg.colorPresetIdx]->outlineColor();
                        uint32_t fcObs = ((uint32_t)fc.blue()  << 16) | ((uint32_t)fc.green() <<  8) | (uint32_t)fc.red();
                        uint32_t ocObs = ((uint32_t)oc.blue()  << 16) | ((uint32_t)oc.green() <<  8) | (uint32_t)oc.red();
                        lyrics_renderer_set_text_color(fcObs, ocObs);
                        lyrics_renderer_set_chinese_color(fcObs, ocObs);
                        m_slideFontColor = fc;
                    }

                    // Apply background using global index (button slideIndex is global)
                    {
                        QString newBg;
                        for (auto *btn : m_slideButtons)
                            if (btn->slideIndex() == index) { newBg = btn->backgroundPath(); break; }
                        showBackground(newBg);   // same rules as every other slide change
                    }

                    // Push slide content using local index into m_slides[]
                    const auto &sl = allSlides[localIdx];
                    if (sl.isChinese)
                        lyrics_renderer_push_chinese(sl.text, sl.pinyin, sl.english, sl.section, m_currentSong.title);
                    else
                        lyrics_renderer_push(sl.text, sl.section, m_currentSong.title);
                    m_activeSlide = localIdx;
                    rememberOnAir(seg, localIdx);

                    // The remote numbers lyric slides across SONGS only (see buildSequenceJson), while
                    // `index` counts every segment's slides in the live grid.  Send the remote's numbering
                    // so its highlight follows when paragraph segments precede the song.
                    {
                        const int seqRow = m_sequenceSongIds.indexOf(seg.id);
                        const int remoteIdx = (seqRow >= 0 ? computeGlobalSlideOffset(seqRow) : globalIdx) + localIdx;
                        if (m_wsServer) m_wsServer->broadcastSlide(sl.text, sl.section, m_currentSong.title, remoteIdx);
                    }
                    if (m_wsServer) m_wsServer->broadcastActiveMode("lyr");
                    return;
                }
                globalIdx += allSlides.size();
            } else if (seg.type == Segment::Type::Paragraphs) {
                QStringList slides = seg.parseSlides();
                if (index < globalIdx + slides.size()) {
                    int localIdx = index - globalIdx;

                    // Apply section color preset to OBS before pushing
                    if (seg.colorPresetIdx >= 0 && seg.colorPresetIdx < 6
                            && m_colorBtn[seg.colorPresetIdx]) {
                        QColor fc = m_colorBtn[seg.colorPresetIdx]->fontColor();
                        QColor oc = m_colorBtn[seg.colorPresetIdx]->outlineColor();
                        uint32_t fcObs = ((uint32_t)fc.blue()  << 16) | ((uint32_t)fc.green() <<  8) | (uint32_t)fc.red();
                        uint32_t ocObs = ((uint32_t)oc.blue()  << 16) | ((uint32_t)oc.green() <<  8) | (uint32_t)oc.red();
                        lyrics_renderer_set_paragraph_text_color(fcObs, ocObs);
                        m_slideFontColor = fc;
                    }

                    // Apply background using global index
                    {
                        QString newBg;
                        for (auto *btn : m_slideButtons)
                            if (btn->slideIndex() == index) { newBg = btn->backgroundPath(); break; }
                        showBackground(newBg);   // same rules as every other slide change
                    }

                    applyParagraphStyle(seg);
                    rememberOnAir(seg, localIdx);
                    lyrics_renderer_paragraph_push(seg.title, slides[localIdx]);
                    if (m_wsServer) m_wsServer->broadcastScripture(seg.title, slides[localIdx]);
                    if (m_wsServer) m_wsServer->broadcastActiveMode("paragraph");
                    return;
                }
                globalIdx += slides.size();
            } else if (seg.type == Segment::Type::Media) {
                if (seg.mediaPaths.isEmpty()) {
                    // Placeholder SlideButton was created — consumes one globalIdx slot
                    if (index == globalIdx) {
                        if (m_segmentList) m_segmentList->setCurrentRow(segIdx);
                        onLiveToggled(false);
                        return;
                    }
                    ++globalIdx;
                }
                // Media with actual paths: MediaSlideCards used, no globalIdx slot consumed
            } else if (seg.type == Segment::Type::Presentation) {
                // PptLiveCard used — no globalIdx slot consumed
            }
        }
        return;
    }

    // Default: Lyrics mode
    setActiveSlide(index);
    pushCurrentSlide();
    if (m_wsServer) m_wsServer->broadcastActiveMode("lyr");
}

void LyricsDock::onSlideEditRequested(int sectionIndex)
{
    if (m_paragraphMode) {
        showParagraphEditDialog(m_activeSegIdx);
        return;
    }
    if (m_currentSong.id.isEmpty()) return;

    SongEditDialog dlg(m_currentSong, this, sectionIndex);
    dlg.setSegmentMode(true);
    if (dlg.exec() != QDialog::Accepted) return;

    Song res = dlg.result();
    // The segment is an independent copy: the library changes only when the user chose
    // "Save to Library" (replace) or "Save as New Song".
    if (dlg.savedAsNew())                   SongManager::instance().addSong(res);
    else if (!dlg.appliedToSegmentOnly())   SongManager::instance().upsertSong(res);

    // Update the active segment only (other segments keep their own copy)
    for (int i = 0; i < m_segments.size(); ++i) {
        if (i != m_activeSegIdx) continue;
        if (m_segments[i].type != Segment::Type::Lyrics) continue;
        if (dlg.savedAsNew()) m_segments[i].sourceSongId = res.id;
        m_segments[i].title         = res.title;
        m_segments[i].artist        = res.artist;
        m_segments[i].songKey       = res.key;
        m_segments[i].songTempo     = res.tempo;
        m_segments[i].songCopyright = res.copyright;
        m_segments[i].sections      = res.sections;
        m_segments[i].syncCards();   // keep every slide card's id and background
        updateSegmentListItem(i);
    }

    int prevActive = (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
                         ? onAirIndexIn(m_segments[m_activeSegIdx]) : -1;   // found by id
    m_currentSong = res;
    loadSongSlides(m_currentSong);
    if (prevActive >= 0 && prevActive < m_slides.size()) {
        setActiveSlide(prevActive);
        pushCurrentSlide();
    }
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::updateEditSlidePanel()
{
    if (!m_editSlideText || !m_editSlideApplyBtn || !m_editSlideHdr) return;

    bool hasParagraph = m_paragraphMode && !m_liveMode
                        && m_activeSegIdx >= 0
                        && m_activeSegIdx < m_segments.size()
                        && m_activeSlide >= 0;
    bool hasLyrics = !m_paragraphMode && !m_liveMode
                     && !m_currentSong.id.isEmpty()
                     && m_activeSlide >= 0
                     && m_activeSlide < m_slides.size();

    if (hasParagraph) {
        const auto &item = m_segments[m_activeSegIdx];
        QStringList slides = item.parseSlides();
        if (m_activeSlide < slides.size()) {
            m_editSlideHdr->setText(
                QString("Slide %1 / %2  —  %3")
                    .arg(m_activeSlide + 1).arg(slides.size()).arg(item.title));
            m_editSlideText->setPlainText(slides[m_activeSlide]);
            m_editSlideText->setEnabled(true);
            m_editSlideApplyBtn->setEnabled(true);
            return;
        }
    }
    if (hasLyrics) {
        const LyricsSlide &sl = m_slides[m_activeSlide];
        m_editSlideHdr->setText(
            QString("Slide %1 / %2  —  %3  [%4]")
                .arg(m_activeSlide + 1).arg(m_slides.size())
                .arg(m_currentSong.title).arg(sl.section));
        m_editSlideText->setPlainText(sl.text);
        m_editSlideText->setEnabled(true);
        m_editSlideApplyBtn->setEnabled(true);
        return;
    }
    // No active slide
    m_editSlideHdr->setText("No slide selected");
    m_editSlideText->setPlainText("");
    m_editSlideText->setEnabled(false);
    m_editSlideApplyBtn->setEnabled(false);
}

void LyricsDock::onEditSlideApply()
{
    if (!m_editSlideText) return;
    QString newText = m_editSlideText->toPlainText();

    if (m_paragraphMode && m_activeSegIdx >= 0
            && m_activeSegIdx < m_segments.size()) {
        auto &item = m_segments[m_activeSegIdx];
        QStringList slides = item.parseSlides();
        if (m_activeSlide >= 0 && m_activeSlide < slides.size()) {
            slides[m_activeSlide] = newText.trimmed();
            item.text = slides.join("\n\n\n");
            item.syncCards();   // the edited card keeps its id and background
            saveServiceOrder();
            int saved = onAirIndexIn(item);   // by id
            loadParagraphSlides(item);
            if (saved >= 0 && saved < m_slideButtons.size())
                setActiveSlide(saved);
            // Push what that card NOW contains, not the raw text that was typed.  The edit may
            // have split it into several slides (two blank lines start a new slide): the on-air
            // card is then only the FIRST part, exactly what its slide card shows.  If the
            // edit deleted the card (saved < 0) nothing is pushed.
            const QStringList nowSlides = item.parseSlides();
            if (saved >= 0 && saved < nowSlides.size()) {
                applyParagraphStyle(item);
                lyrics_renderer_paragraph_push(item.title, nowSlides[saved]);
                if (m_wsServer) m_wsServer->broadcastScripture(item.title, nowSlides[saved]);
            }
        }
        return;
    }

    if (!m_paragraphMode && !m_currentSong.id.isEmpty()
            && m_activeSlide >= 0 && m_activeSlide < m_slides.size()) {
        int sectionIdx = (m_activeSlide < m_slideSectionIndices.size())
                         ? m_slideSectionIndices[m_activeSlide] : -1;
        if (sectionIdx < 0 || sectionIdx >= m_currentSong.sections.size()) return;
        LyricsSection &sec = m_currentSong.sections[sectionIdx];
        if (sec.isChinese) return; // edit only English slides

        // Find local slide index within this section
        int firstOfSection = 0;
        for (int i = 0; i < sectionIdx; ++i) {
            const auto &s = m_currentSong.sections[i];
            firstOfSection += s.isChinese ? s.chineseSlides().size() : s.slides().size();
        }
        int localIdx = m_activeSlide - firstOfSection;

        QStringList secSlides = sec.slides();
        if (localIdx < 0 || localIdx >= secSlides.size()) return;
        secSlides[localIdx] = newText.trimmed();
        sec.text = secSlides.join("\n\n");

        // Edit the active segment's own copy; the song library is not touched.
        if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size()
                && m_segments[m_activeSegIdx].type == Segment::Type::Lyrics) {
            m_segments[m_activeSegIdx].sections = m_currentSong.sections;
            m_segments[m_activeSegIdx].syncCards();   // the edited card keeps its id and background
        }
        saveServiceOrder();

        int saved = (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size())
                        ? onAirIndexIn(m_segments[m_activeSegIdx]) : m_activeSlide;   // by id
        loadSongSlides(m_currentSong);
        if (saved >= 0 && saved < m_slides.size()) {
            setActiveSlide(saved);
            pushCurrentSlide();
        }
    }
}

void LyricsDock::onExportService()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Export Service",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
            + "/service_export.json",
        "JSON Files (*.json)");
    if (path.isEmpty()) return;

    QJsonObject root = buildFullServiceExport();
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.close();
        QMessageBox::information(this, "Export Complete",
            QString("Exported %1 segment(s) to:\n%2")
                .arg(segmentsArray(root).size()).arg(path));
    } else {
        QMessageBox::warning(this, "Export Failed", "Could not write file:\n" + path);
    }
}

void LyricsDock::onImportService()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Import Service",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        "JSON Files (*.json)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import Failed", "Could not open file:\n" + path);
        return;
    }
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, "Import Failed", "Invalid JSON file.");
        return;
    }

    const QJsonObject root = doc.object();

    // Count usable segments
    int itemCount = 0;
    for (const auto &v : segmentsArray(root)) {
        if (!v.isObject()) continue;
        const QString t = v.toObject()["type"].toString();
        if (t == "song" || t == "paragraphs" || t == "paragraph" || t == "scripture" || t == "media")
            ++itemCount;
    }
    if (itemCount == 0) {
        QMessageBox::information(this, "Import", "No usable items found in file.");
        return;
    }

    bool hasLayout   = root.contains("sourceSettings");
    bool hasGallery  = root["gallery"].isArray();
    bool hasAudio    = root["audioBin"].isArray();
    bool hasPresets  = root.contains("colorPresets");

    // Build summary string
    QStringList summary;
    summary << QString("%1 segment(s)").arg(itemCount);
    if (hasGallery)  summary << QString("%1 graphics image(s)").arg(root["gallery"].toArray().size());
    if (hasAudio)    summary << QString("%1 audio file(s)").arg(root["audioBin"].toArray().size());
    if (hasPresets)  summary << "color presets";
    if (hasLayout)   summary << "layout settings";

    // Replace All note: explain if layout is missing
    QString replaceNote = hasLayout
        ? "Replace All: clears the current service and restores everything from the file, including font and color layout settings."
        : "Replace All: clears the current service and restores everything from the file.\n\nNote: this file has no layout settings — your current font, size and color settings will be kept.";

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Import Service");
    msgBox.setText("This file contains: " + summary.join(", ") + ".\n\nHow would you like to import it?");
    msgBox.setInformativeText(
        replaceNote + "\n\n"
        "Keep My Layout: replaces the segments only; your current font, colors, gallery and audio are unchanged.");

    QPushButton *replaceAllBtn  = msgBox.addButton("Replace All",    QMessageBox::AcceptRole);
    QPushButton *keepLayoutBtn  = msgBox.addButton("Keep My Layout", QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Cancel);
    msgBox.exec();

    QAbstractButton *clicked = msgBox.clickedButton();
    if (!clicked || clicked == msgBox.button(QMessageBox::Cancel)) return;

    bool replaceLayout = (clicked == replaceAllBtn);
    applyFullServiceImport(root, replaceLayout);

    QMessageBox::information(this, "Import Complete",
        QString("Imported %1 item(s).").arg(itemCount));
}

void LyricsDock::onImportFromWeb()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Import Service from Web Editor",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        "Service JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import Failed",
            QString("Could not open: %1").arg(path));
        return;
    }
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    // Try service.json format first (array of segments)
    if (doc.isObject() && (doc.object().contains("segments") || doc.object().contains("items"))) {
        // Full service.json: replace entire service order
        const auto obj = doc.object();
        m_segments.clear();
        m_sequenceSongIds.clear();
        for (const auto &v : segmentsArray(obj)) {
            if (!v.isObject()) continue;
            Segment item = Segment::fromJson(v.toObject());
            item.remintIds();   // an import always gets new segment and slide-card ids
            if (item.type == Segment::Type::Lyrics) {
                auto *existing = SongManager::instance().findById(item.sourceSongId);
                if (!existing) {
                    // Song not in library — add it from embedded sections
                    Song s; s.id = item.sourceSongId; s.title = item.title;
                    s.artist = item.artist; s.sections = item.sections;
                    s.key = item.songKey; s.tempo = item.songTempo; s.copyright = item.songCopyright;
                    SongManager::instance().addSong(s);
                } else if (item.sections.isEmpty()) {
                    // Old service file with no embedded sections — populate now
                    item.sections = existing->sections;
                    item.title    = existing->title;
                    item.artist   = existing->artist;
                    item.songKey = existing->key; item.songTempo = existing->tempo; item.songCopyright = existing->copyright;
                    item.syncCards();
                }
                m_sequenceSongIds << item.id;
            }
            m_segments << item;
        }
        SongManager::instance().save();
        rebuildSegmentList();
        saveServiceOrder();
        broadcastSequence();
        QMessageBox::information(this, "Import Complete",
            QString("Service loaded: %1 items.").arg(m_segments.size()));
        return;
    }

    // Fallback: legacy song-array format
    if (!doc.isArray()) {
        QMessageBox::warning(this, "Import Failed",
            "File must be a service JSON (from the web editor).");
        return;
    }
    auto &mgr = SongManager::instance();
    int added = 0, updated = 0;
    for (const auto &v : doc.array()) {
        if (!v.isObject()) continue;
        Song s = Song::fromJson(v.toObject());
        if (s.title.isEmpty()) continue;
        if (s.id.isEmpty()) s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (mgr.findById(s.id)) { mgr.updateSong(s); ++updated; }
        else                     { mgr.addSong(s);    ++added; }
        bool inService = false;
        for (const auto &seg : m_segments)
            if (seg.type == Segment::Type::Lyrics && songIdOf(seg) == s.id) { inService = true; break; }
        if (!inService) {
            Segment si; si.type = Segment::Type::Lyrics;
            si.id = newId(); si.sourceSongId = s.id;
            si.title = s.title; si.artist = s.artist;
            si.songKey = s.key; si.songTempo = s.tempo; si.songCopyright = s.copyright;
            si.sections = s.sections;  // embed full lyrics
            si.syncCards();
            m_segments << si; m_sequenceSongIds << si.id;
        }
    }
    mgr.save();
    rebuildSegmentList();
    saveServiceOrder();
    broadcastSequence();
    QMessageBox::information(this, "Import Complete",
        QString("Songs added: %1\nSongs updated: %2").arg(added).arg(updated));
}

void LyricsDock::onBlackoutClicked()
{
    for (auto *b : m_slideButtons) b->setActive(false);
    m_activeSlide = -1;
    m_onAirSegId.clear();
    m_onAirCardId.clear();
    publishOnAir();
    applyZoomToAll();
    updateSlideCounter();
    lyrics_renderer_clear();
    if (m_wsServer) m_wsServer->broadcastClear();
}

void LyricsDock::applySegmentPreset(const Segment &seg)
{
    if (seg.colorPresetIdx < 0 || seg.colorPresetIdx >= 6 || !m_colorBtn[seg.colorPresetIdx]) return;
    const QColor fc = m_colorBtn[seg.colorPresetIdx]->fontColor();
    const QColor oc = m_colorBtn[seg.colorPresetIdx]->outlineColor();
    const uint32_t fcObs = ((uint32_t)fc.blue() << 16) | ((uint32_t)fc.green() << 8) | (uint32_t)fc.red();
    const uint32_t ocObs = ((uint32_t)oc.blue() << 16) | ((uint32_t)oc.green() << 8) | (uint32_t)oc.red();
    if (seg.type == Segment::Type::Lyrics) {
        lyrics_renderer_set_text_color(fcObs, ocObs, false);
        lyrics_renderer_set_chinese_color(fcObs, ocObs, false);
    } else if (seg.type == Segment::Type::Paragraphs) {
        lyrics_renderer_set_paragraph_text_color(fcObs, ocObs, false);
    }
}

void LyricsDock::applyParagraphStyle(const Segment &item)
{
    applySegmentPreset(item);   // this segment's colour preset, for the push that follows

    // This segment's own complete layout (taken from the default when it was created)
    QByteArray layout;
    if (item.type == Segment::Type::Paragraphs) {
        const int i = segmentIndexById(item.id);
        if (i >= 0) {
            if (ensureParagraphLayout(m_segments[i])) saveServiceOrder();
            layout = m_segments[i].layoutBytes();
        } else {
            layout = item.layoutBytes();
        }
    }
    lyrics_renderer_set_paragraph_layout(layout);

    if (!item.altSentenceColor || item.altSentencePreset < 0
        || item.altSentencePreset >= 6 || !m_colorBtn[item.altSentencePreset]) {
        lyrics_renderer_set_alt_sentence_color(false, 0, 0);
        return;
    }
    QColor fc = m_colorBtn[item.altSentencePreset]->fontColor();
    QColor oc = m_colorBtn[item.altSentencePreset]->outlineColor();
    uint32_t fcObs = ((uint32_t)fc.blue()  << 16) | ((uint32_t)fc.green() << 8) | (uint32_t)fc.red();
    uint32_t ocObs = ((uint32_t)oc.blue()  << 16) | ((uint32_t)oc.green() << 8) | (uint32_t)oc.red();
    lyrics_renderer_set_alt_sentence_color(true, fcObs, ocObs);
}

void LyricsDock::onColorPresetApplied(QColor fontColor, QColor outlineColor)
{
    // QColor stores RRGGBB; OBS text_gdiplus uses 0x00BBGGRR (COLORREF).
    // Byte-swap R↔B before passing to the renderer.
    uint32_t fc = ((uint32_t)fontColor.blue()    << 16)
                | ((uint32_t)fontColor.green()    <<  8)
                | (uint32_t)fontColor.red();
    uint32_t oc = ((uint32_t)outlineColor.blue()  << 16)
                | ((uint32_t)outlineColor.green()  <<  8)
                | (uint32_t)outlineColor.red();
    lyrics_renderer_set_text_color(fc, oc);
    lyrics_renderer_set_paragraph_text_color(fc, oc); // paragraph sections
    lyrics_renderer_set_chinese_color(fc, oc); // also update Chinese chars + translation

    // Propagate font color to all slide card labels
    m_slideFontColor = fontColor;
    for (auto *btn : m_slideButtons)
        btn->setFontColor(fontColor);

    // Save preset index to current segment so it restores on next visit.
    // m_activeColorPresetIdx is set by the connection lambda before calling here.
    if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size()
        && m_activeColorPresetIdx >= 0) {
        m_segments[m_activeSegIdx].colorPresetIdx = m_activeColorPresetIdx;
        saveServiceOrder();
    }

    saveState();
}

QJsonArray LyricsDock::colorPresetsJson() const
{
    QJsonArray arr;
    for (int i = 0; i < 6; ++i) {
        QJsonObject p;
        if (m_colorBtn[i]) {
            p["font"]    = (qint64)(m_colorBtn[i]->fontColor().rgb()    & 0x00FFFFFF);
            p["outline"] = (qint64)(m_colorBtn[i]->outlineColor().rgb() & 0x00FFFFFF);
        } else {
            p["font"]    = (qint64)0x00FFFFFF;
            p["outline"] = (qint64)0x00000000;
        }
        arr.append(p);
    }
    return arr;
}

void LyricsDock::applyColorPresetFromRemote(int idx)
{
    if (idx < 0 || idx >= 6 || !m_colorBtn[idx]) return;
    for (int j = 0; j < 6; ++j)
        if (m_colorBtn[j]) m_colorBtn[j]->setActive(j == idx);
    QColor fc = m_colorBtn[idx]->fontColor();
    QColor oc = m_colorBtn[idx]->outlineColor();
    if (idx < 4) {
        m_activeColorPresetIdx = idx;
        onColorPresetApplied(fc, oc);
    } else {
        uint32_t f = ((uint32_t)fc.blue()  << 16) | ((uint32_t)fc.green() << 8) | (uint32_t)fc.red();
        uint32_t o = ((uint32_t)oc.blue()  << 16) | ((uint32_t)oc.green() << 8) | (uint32_t)oc.red();
        lyrics_renderer_set_pinyin_color(f, o);
        saveState();
    }
    if (m_wsServer) m_wsServer->broadcastColorPresets(colorPresetsJson(), idx);
}

void LyricsDock::onNextSlide()
{
    if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size()) {
        const auto &item = m_segments[m_activeSegIdx];
        if (item.type == Segment::Type::Paragraphs) {
            if (!m_slides.isEmpty()) {
                int next = (m_activeSlide < 0) ? 0 : m_activeSlide + 1;
                if (next < m_slides.size()) { setActiveSlide(next); pushCurrentSlide(); }
            }
            return;
        }
        if (item.type != Segment::Type::Lyrics) return;
    }
    if (m_slides.isEmpty()) return;
    int next = (m_activeSlide < 0) ? 0 : m_activeSlide + 1;
    if (next >= m_slides.size()) return;
    setActiveSlide(next);
    pushCurrentSlide();
}

void LyricsDock::onPrevSlide()
{
    if (m_activeSegIdx >= 0 && m_activeSegIdx < m_segments.size()) {
        const auto &item = m_segments[m_activeSegIdx];
        if (item.type == Segment::Type::Paragraphs) {
            if (m_activeSlide > 0) { setActiveSlide(m_activeSlide - 1); pushCurrentSlide(); }
            return;
        }
        if (item.type != Segment::Type::Lyrics) return;
    }
    if (m_slides.isEmpty() || m_activeSlide <= 0) return;
    setActiveSlide(m_activeSlide - 1);
    pushCurrentSlide();
}

// ── Hotkey-accessible actions ─────────────────────────────────
void LyricsDock::activateNextSlide()  { onNextSlide(); }
void LyricsDock::activatePrevSlide()  { onPrevSlide(); }

void LyricsDock::lpNextSlide(const QString &mode)
{
    // A presentation on air owns Next/Previous: they drive the presentation and never move on to
    // the next segment (the operator selects the next segment by hand).
    if (mode == "ppt" || (m_pptTab && m_pptTab->activeType() >= 0)) {
        if (m_pptTab && m_pptTab->activeType() < 0)
            m_pptTab->requestShow(0);   // auto-activate if not yet showing
        pptNext();
        return;
    }
    navigateQueue(+1);
}

void LyricsDock::lpPrevSlide(const QString &mode)
{
    if (mode == "ppt" || (m_pptTab && m_pptTab->activeType() >= 0)) {
        if (m_pptTab && m_pptTab->activeType() < 0)
            m_pptTab->requestShow(0);
        pptPrev();
        return;
    }
    navigateQueue(-1);
}

void LyricsDock::activateNextSong()
{
    if (!m_segmentList) return;
    int row = m_segmentList->currentRow();
    if (row < m_segmentList->count() - 1)
        m_segmentList->setCurrentRow(row + 1);
}

void LyricsDock::activatePrevSong()
{
    if (!m_segmentList) return;
    int row = m_segmentList->currentRow();
    if (row > 0)
        m_segmentList->setCurrentRow(row - 1);
}

void LyricsDock::activateGalleryMedia(int oneBased)
{
    if (m_sharedGallery)
        m_sharedGallery->activateByPosition(oneBased);
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

// Reflow slide cards whenever the dock is resized so card widths (which are
// computed as a percentage of the viewport) stay accurate.
void LyricsDock::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    // Defer one event-loop cycle so Qt finishes propagating the new geometry
    // to child widgets (including the scroll-area viewport) before we measure.
    QTimer::singleShot(0, this, [this]{ applyZoomToAll(); });
}

// ── Overlay touch-pad slots ───────────────────────────────────

#include "lyrics-dock.moc"

