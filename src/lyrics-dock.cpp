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
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QImageReader>
#include <QPlainTextEdit>

static bool isVideoFile(const QString &path) {
    static const QStringList v = {"mp4","mov","avi","mkv","webm","flv","wmv","m4v"};
    return v.contains(QFileInfo(path).suffix().toLower());
}

// ── MediaSlideCard — used in live mode for Media service items ─
class MediaSlideCard : public QFrame {
    Q_OBJECT
public:
    MediaSlideCard(const QString &path, int svcIdx, int fileIdx, QWidget *parent = nullptr)
        : QFrame(parent), m_svcIdx(svcIdx), m_fileIdx(fileIdx), m_path(path)
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

            connect(m_rewindBtn, &QPushButton::clicked, this, [this](){ emit rewindClicked(m_svcIdx, m_fileIdx); });
            connect(m_playBtn,   &QPushButton::clicked, this, [this](){ emit playPauseClicked(m_svcIdx, m_fileIdx); });
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

    int svcIdx()  const { return m_svcIdx; }
    int fileIdx() const { return m_fileIdx; }
    const QString &path() const { return m_path; }

signals:
    void clicked(int svcIdx, int fileIdx);
    void playPauseClicked(int svcIdx, int fileIdx);
    void rewindClicked(int svcIdx, int fileIdx);

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) emit clicked(m_svcIdx, m_fileIdx);
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
    int     m_svcIdx, m_fileIdx;
    QString m_path;
    bool    m_isPicture  = false;
    QPixmap m_srcPixmap;               // original full-res thumb for picture rescaling
    QLabel      *m_thumb     = nullptr;
    QLabel      *m_nameLbl   = nullptr;
    QPushButton *m_rewindBtn = nullptr;
    QPushButton *m_playBtn   = nullptr;
};

// ── PptLiveCard — used in live mode for Presentation service items ─
class PptLiveCard : public QFrame {
    Q_OBJECT
public:
    PptLiveCard(const QString &title, int svcIdx, QWidget *parent = nullptr)
        : QFrame(parent), m_svcIdx(svcIdx)
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

        connect(m_prevBtn, &QPushButton::clicked, this, [this](){ emit prevRequested(m_svcIdx); });
        connect(m_nextBtn, &QPushButton::clicked, this, [this](){ emit nextRequested(m_svcIdx); });
    }

    void setActive(bool active) {
        applyStyle(active);
        m_statusLbl->setText(active ? "Showing on OBS  (click to stop)" : "Click to show on OBS");
        m_statusLbl->setStyleSheet(active
            ? "color:#a78bfa;font-size:9px;"
            : "color:#888;font-size:9px;");
    }

    int svcIdx() const { return m_svcIdx; }

signals:
    void activateRequested(int svcIdx);
    void prevRequested(int svcIdx);
    void nextRequested(int svcIdx);

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) emit activateRequested(m_svcIdx);
        QFrame::mousePressEvent(e);
    }

private:
    void applyStyle(bool active) {
        setStyleSheet(active
            ? "PptLiveCard{background:#1e1a3a;border:2px solid #a78bfa;border-radius:6px;}"
            : "PptLiveCard{background:#242424;border:1px solid #444;border-radius:6px;}"
              "PptLiveCard:hover{border-color:#c084fc;background:#2e2e2e;}");
    }
    int m_svcIdx;
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
    auto *editAct    = menu.addAction(makePenIcon(), "Edit this section...");
    menu.addSeparator();
    auto *bgCardAct  = menu.addAction(QString::fromUtf8("🖼  Apply current background for this card"));
    auto *bgSecAct   = menu.addAction(QString::fromUtf8("🖼  Apply current background for this section"));
    menu.addSeparator();
    auto *clrCardAct = menu.addAction(QString::fromUtf8("✕  Clear background for this card"));
    auto *clrSecAct  = menu.addAction(QString::fromUtf8("✕  Clear background for this section"));
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
// Build a Song from a Lyrics ServiceItem using embedded sections.
// Falls back to the library lookup if no sections are embedded yet
// (backward compatibility for service.json files saved before this change).
static Song songFromItem(const ServiceItem &item)
{
    if (!item.sections.isEmpty()) {
        Song s;
        s.id      = item.id;
        s.title   = item.title;
        s.artist  = item.artist;
        s.sections = item.sections;
        return s;
    }
    // Fallback: look up from library
    const Song *lib = SongManager::instance().findById(item.id);
    if (lib) return *lib;
    Song empty; empty.id = item.id; empty.title = item.title; empty.artist = item.artist;
    return empty;
}

// Each song has "slides" array matching exactly what loadSongSlides() builds.
QJsonArray LyricsDock::buildSequenceJson() const
{
    QJsonArray songs;
    int globalIdx = 0; // globally unique across ALL songs in sequence
    for (const auto &id : m_sequenceSongIds) {
        // Find the corresponding service item to use embedded sections
        const ServiceItem *svcItem = nullptr;
        for (const auto &si : m_serviceItems)
            if (si.type == ServiceItem::Type::Lyrics && si.id == id) { svcItem = &si; break; }
        if (!svcItem) continue;
        Song song = songFromItem(*svcItem);
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

QJsonArray LyricsDock::buildExportServiceJson() const
{
    QJsonArray arr;
    for (const auto &item : m_serviceItems) {
        if (item.type != ServiceItem::Type::Lyrics &&
            item.type != ServiceItem::Type::Paragraphs) continue;
        QJsonObject o = item.toJson();
        // Strip background settings
        o.remove("slideBgs");
        if (item.type == ServiceItem::Type::Lyrics && !item.sections.isEmpty()) {
            QJsonArray secs;
            for (const auto &sec : item.sections) {
                QJsonObject so = sec.toJson();
                so.remove("backgroundPath");
                secs.append(so);
            }
            o["sections"] = secs;
        }
        // Default to preset 0
        o["colorPresetIdx"] = 0;
        arr.append(o);
    }
    return arr;
}

void LyricsDock::importServiceItemsFromRemote(const QJsonArray &items)
{
    // Remove all existing Lyrics and Paragraphs items
    for (int i = m_serviceItems.size() - 1; i >= 0; --i) {
        if (m_serviceItems[i].type == ServiceItem::Type::Lyrics ||
            m_serviceItems[i].type == ServiceItem::Type::Paragraphs) {
            m_serviceItems.removeAt(i);
        }
    }

    // Insert imported items at position 0 (before media/presentation)
    int insertAt = 0;
    for (const auto &v : items) {
        if (!v.isObject()) continue;
        ServiceItem si = ServiceItem::fromJson(v.toObject());
        if (si.type != ServiceItem::Type::Lyrics &&
            si.type != ServiceItem::Type::Paragraphs) continue;
        // Strip backgrounds to be safe
        si.slideBgs.clear();
        for (auto &sec : si.sections) sec.backgroundPath.clear();
        // Default color preset
        si.colorPresetIdx = 0;
        // Library fallback for songs with no embedded sections
        if (si.type == ServiceItem::Type::Lyrics && si.sections.isEmpty()) {
            const Song *lib = SongManager::instance().findById(si.id);
            if (lib) si.sections = lib->sections;
        }
        m_serviceItems.insert(insertAt++, si);
    }

    // Rebuild sequence cache
    m_sequenceSongIds.clear();
    for (const auto &si : m_serviceItems)
        if (si.type == ServiceItem::Type::Lyrics) m_sequenceSongIds << si.id;

    m_activeServiceIdx = -1;
    clearSlideGrid();
    rebuildServiceList();
    saveServiceOrder();
    broadcastSequence();
}

QJsonArray LyricsDock::buildServiceOrderJson() const
{
    QJsonArray arr;
    int songIdx = 0;
    for (int i = 0; i < m_serviceItems.size(); ++i) {
        const auto &item = m_serviceItems[i];
        QJsonObject o;
        o["idx"]  = i;
        o["type"] = item.typeLabel().toLower();
        if (item.type == ServiceItem::Type::Lyrics) {
            o["title"]   = item.title;
            o["artist"]  = item.artist;
            o["songIdx"] = songIdx++;
        } else {
            o["title"]  = item.title;
            o["artist"] = QString{};
            if (item.type == ServiceItem::Type::Paragraphs) {
                o["text"] = item.text;
                // Per-slide background overrides
                if (!item.slideBgs.isEmpty()) {
                    QJsonObject bgs;
                    for (auto it = item.slideBgs.constBegin(); it != item.slideBgs.constEnd(); ++it)
                        bgs[QString::number(it.key())] = it.value();
                    o["slideBgs"] = bgs;
                }
            } else if (item.type == ServiceItem::Type::Media) {
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
        const ServiceItem *si = nullptr;
        for (const auto &s : m_serviceItems)
            if (s.type == ServiceItem::Type::Lyrics && s.id == m_sequenceSongIds[row])
                { si = &s; break; }
        if (!si || si->title != songTitle) continue;

        // Find the service-item index for this song (may differ from row when
        // non-Lyrics items precede it in the service order).
        int svcIdx = -1;
        for (int i = 0; i < m_serviceItems.size(); ++i) {
            if (m_serviceItems[i].type == ServiceItem::Type::Lyrics
                    && m_serviceItems[i].id == m_sequenceSongIds[row]) {
                svcIdx = i;
                break;
            }
        }
        if (svcIdx < 0) svcIdx = row; // fallback: songs-only service order

        // Select the correct row in the service list and load its slides if needed.
        if (m_serviceList && m_serviceList->currentRow() != svcIdx)
            m_serviceList->setCurrentRow(svcIdx);

        if (m_liveMode) {
            // In live mode, loadServiceItemContent calls clearSlideGrid() which destroys
            // the live button layout. Set up song state manually instead.
            if (m_currentSong.id != m_sequenceSongIds[row]) {
                Song song = songFromItem(*si);
                m_currentSong = song;
                m_slides      = song.allSlides();
            }
            m_currentSongSlideOffset = computeGlobalSlideOffset(row);
        } else {
            if (m_currentSong.id != m_sequenceSongIds[row])
                loadServiceItemContent(svcIdx);
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

// New index-driven approach: remote sends (serviceIdx, slideIdx) and
// the plugin looks up everything from its own service.json data.
void LyricsDock::activateSlideFromRemote(int serviceIdx, int slideIdx)
{
    if (serviceIdx < 0 || serviceIdx >= m_serviceItems.size()) return;
    const ServiceItem &item = m_serviceItems[serviceIdx];

    switch (item.type) {
    case ServiceItem::Type::Paragraphs: {
        QStringList slides = item.parseSlides();
        if (slideIdx < 0 || slideIdx >= slides.size()) return;
        // Clear any previously rendered lyrics text before showing paragraph
        lyrics_renderer_clear();
        // Push assigned background if any
        QString bgPath = item.slideBgs.value(slideIdx);
        if (!bgPath.isEmpty() && bgPath != m_currentBgPath) {
            int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
            lyrics_renderer_media_push(bgPath, fadeMs);
            m_currentBgPath = bgPath;
        }
        // Push text through CTW renderer
        lyrics_renderer_paragraph_push(item.title, slides[slideIdx]);
        if (m_wsServer) m_wsServer->broadcastScripture(item.title, slides[slideIdx]);
        if (m_wsServer) m_wsServer->broadcastActiveMode("paragraph");
        break;
    }
    case ServiceItem::Type::Lyrics: {
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
    case ServiceItem::Type::Media: {
        if (slideIdx < 0 || slideIdx >= item.mediaPaths.size()) return;
        const QString &path = item.mediaPaths[slideIdx];
        if (isVideoFile(path) && m_audioBin) m_audioBin->stopPlayback();
        lyrics_renderer_clear();   // wipe any lyrics from OBS source
        int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
        lyrics_renderer_media_push(path, fadeMs);
        if (m_wsServer) {
            m_wsServer->broadcastClear();        // clears confidence monitor
            m_wsServer->broadcastMediaState(path);
        }
        break;
    }
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
    if (m_activeServiceIdx < 0 || m_activeServiceIdx >= m_serviceItems.size()) return;
    const auto &item = m_serviceItems[m_activeServiceIdx];
    switch (item.type) {
    case ServiceItem::Type::Paragraphs:
        if (m_activeSlide >= 0) pushCurrentSlide();
        break;
    default:
        if (m_activeSlide >= 0) pushCurrentSlide();
        if (m_sharedGallery) m_sharedGallery->replayActive();
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
            QString sf = doc.object()["slideFont"].toString("Arial");
            if (!sf.isEmpty()) {
                m_slideFontFamily = sf;
                if (m_slideFontCombo) { QSignalBlocker b(m_slideFontCombo); m_slideFontCombo->setCurrentFont(QFont(sf)); }
            }
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

// ── ServiceItem ───────────────────────────────────────────────

QJsonObject ServiceItem::toJson() const
{
    QJsonObject o;
    switch (type) {
    case Type::Lyrics:       o["type"] = "song";         break;
    case Type::Paragraphs:   o["type"] = "paragraphs";   break;
    case Type::Media:        o["type"] = "media";        break;
    case Type::Presentation: o["type"] = "presentation"; break;
    }
    o["id"] = id; o["title"] = title; o["artist"] = artist;
    o["text"] = text; o["filePath"] = filePath;
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
    if (!slideBgs.isEmpty()) {
        QJsonObject bgs;
        for (auto it = slideBgs.constBegin(); it != slideBgs.constEnd(); ++it)
            bgs[QString::number(it.key())] = it.value();
        o["slideBgs"] = bgs;
    }
    if (colorPresetIdx >= 0)
        o["colorPresetIdx"] = colorPresetIdx;
    return o;
}

ServiceItem ServiceItem::fromJson(const QJsonObject &obj)
{
    ServiceItem item;
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
    item.filePath = obj["filePath"].toString();
    for (const auto &v : obj["mediaPaths"].toArray())
        item.mediaPaths << v.toString();
    for (const auto &v : obj["sections"].toArray())
        if (v.isObject()) item.sections << LyricsSection::fromJson(v.toObject());
    const auto bgsObj = obj["slideBgs"].toObject();
    for (auto it = bgsObj.constBegin(); it != bgsObj.constEnd(); ++it)
        item.slideBgs[it.key().toInt()] = it.value().toString();
    if (obj.contains("colorPresetIdx"))
        item.colorPresetIdx = obj["colorPresetIdx"].toInt(-1);
    return item;
}

QStringList ServiceItem::parseSlides() const
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

QString ServiceItem::typeLabel() const
{
    switch (type) {
    case Type::Lyrics:       return "Lyrics";
    case Type::Paragraphs:   return "Paragraphs";
    case Type::Media:        return "Media";
    case Type::Presentation: return "Presentation";
    }
    return {};
}

QString ServiceItem::typeIcon() const
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

QColor ServiceItem::typeColor() const
{
    switch (type) {
    case Type::Lyrics:       return QColor("#4a9eff");
    case Type::Paragraphs:   return QColor("#3dd68c");
    case Type::Media:        return QColor("#f59e0b");
    case Type::Presentation: return QColor("#c084fc");
    }
    return QColor("#4a9eff");
}

// ── ServiceItemDelegate ───────────────────────────────────────

ServiceItemDelegate::ServiceItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

QSize ServiceItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    int w = option.rect.width();
    return {w > 0 ? w : 200, 52};
}

void ServiceItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                                const QModelIndex &idx) const
{
    if (!p || !idx.isValid()) return;
    if (opt.rect.width() <= 0 || opt.rect.height() <= 0) return;
    p->save();
    const bool sel = opt.state & QStyle::State_Selected;
    const bool hov = opt.state & QStyle::State_MouseOver;
    p->fillRect(opt.rect, sel ? QColor("#3a3a45") : hov ? QColor("#2a2a32") : QColor("#16161a"));

    const auto item = idx.data(Qt::UserRole).value<ServiceItem>();
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
    m_editBtn->setToolTip("Edit mode: select a service item to view its slides");

    m_liveToggleBtn = new QPushButton("LIVE");
    m_liveToggleBtn->setCheckable(true);
    m_liveToggleBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_liveToggleBtn->setMinimumHeight(28);
    m_liveToggleBtn->setStyleSheet(
        "QPushButton{background:#1e2a1e;color:#4a8a4a;border:1px solid #2a5a2a;"
        "border-radius:3px;font-size:10px;font-weight:bold;}"
        "QPushButton:hover{background:#253025;}"
        "QPushButton:checked{background:#1a4a1a;color:#3dd68c;border:1px solid #2a8a2a;}");
    m_liveToggleBtn->setToolTip("Live mode: all sections displayed in sequence");

    auto *modeBtnRow = new QHBoxLayout;
    modeBtnRow->setSpacing(4);
    modeBtnRow->setContentsMargins(0, 0, 0, 0);
    modeBtnRow->addWidget(m_editBtn);
    modeBtnRow->addWidget(m_liveToggleBtn);
    leftL->addLayout(modeBtnRow);

    auto *orderHdr = new QLabel("SERVICE ORDER");
    orderHdr->setStyleSheet("color:#6b6b7b;font-size:9px;font-weight:bold;letter-spacing:1px;");
    leftL->addWidget(orderHdr);
    m_serviceList = new QListWidget(this);
    m_serviceList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_serviceList->setItemDelegate(new ServiceItemDelegate(m_serviceList));
    leftL->addWidget(m_serviceList, 1);

    auto *svcBtnRow = new QHBoxLayout;
    svcBtnRow->setSpacing(3);
    m_addItemBtn    = new QPushButton("+");
    m_removeItemBtn = new QPushButton(QString::fromUtf8("\u2212"));
    m_itemUpBtn     = new QPushButton(QString::fromUtf8("\u25B2"));
    m_itemDownBtn   = new QPushButton(QString::fromUtf8("\u25BC"));
    m_importWebBtn  = new QPushButton(QString::fromUtf8("\u2B06 Import"));
    for (auto *b : {m_addItemBtn, m_removeItemBtn, m_itemUpBtn, m_itemDownBtn})
        b->setFixedSize(28, 28);
    m_addItemBtn->setToolTip("Add service item");
    m_removeItemBtn->setToolTip("Remove selected item");
    m_itemUpBtn->setToolTip("Move up");
    m_itemDownBtn->setToolTip("Move down");
    m_importWebBtn->setToolTip("Import service from web editor JSON");
    m_removeItemBtn->setEnabled(false);
    m_itemUpBtn->setEnabled(false);
    m_itemDownBtn->setEnabled(false);
    svcBtnRow->addWidget(m_addItemBtn);
    svcBtnRow->addWidget(m_removeItemBtn);
    svcBtnRow->addWidget(m_itemUpBtn);
    svcBtnRow->addWidget(m_itemDownBtn);
    svcBtnRow->addStretch();
    svcBtnRow->addWidget(m_importWebBtn);

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

    leftL->addLayout(svcBtnRow);

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
    auto *emptyLbl = new QLabel("Select a service item\nto see its content.");
    emptyLbl->setAlignment(Qt::AlignCenter);
    emptyLbl->setStyleSheet("color:#6b6b7b;font-size:13px;");
    emptyL->addStretch();
    emptyL->addWidget(emptyLbl);
    emptyL->addStretch();
    m_contentStack->addWidget(emptyPage);
    m_contentStack->setCurrentIndex(3);

    centerL->addWidget(m_contentStack, 1);
    centerL->addWidget(m_slideCtrlRow);

    // ════ RIGHT — Media Bin ══════════════════════════════════
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

    // ── Media Bin (top half) ──────────────────────────────────
    auto *binHdr = new QLabel("MEDIA BIN");
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

    // Stop audio when a VIDEO starts playing (images do not stop audio)
    connect(m_sharedGallery, &MediaGallery::videoActivated,
            m_audioBin, &AudioBin::stopPlayback);
    // Stop gallery video when audio bin starts playing
    connect(m_audioBin, &AudioBin::playbackStarted, this, [this]() {
        if (m_sharedGallery) m_sharedGallery->stopPlayback();
    });

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
    connect(m_serviceList, &QListWidget::currentRowChanged,
            this, &LyricsDock::onServiceItemSelected);
    connect(m_serviceList, &QListWidget::customContextMenuRequested,
            this, &LyricsDock::onServiceContextMenu);
    connect(m_addItemBtn,    &QPushButton::clicked, this, &LyricsDock::onAddServiceItem);
    connect(m_removeItemBtn, &QPushButton::clicked, this, &LyricsDock::onRemoveServiceItem);
    connect(m_itemUpBtn,     &QPushButton::clicked, this, &LyricsDock::onServiceItemUp);
    connect(m_itemDownBtn,   &QPushButton::clicked, this, &LyricsDock::onServiceItemDown);
    connect(m_importWebBtn,  &QPushButton::clicked, this, &LyricsDock::onImportFromWeb);

    // ── Cross-panel media coordination ───────────────────────
    connect(m_sharedGallery, &MediaGallery::mediaActivated, this, [this](){
        m_videoTab->stopPlayback();
    });
    connect(m_videoTab, &VideoTab::playbackStarted, this, [this](){
        if (m_pptTab && m_pptTab->activeType() >= 0) m_pptTab->requestShow(-1);
        lyrics_renderer_clear();
        lyrics_renderer_scripture_clear();
        if (m_sharedGallery) m_sharedGallery->clearActiveCard();
        if (m_audioBin) m_audioBin->stopPlayback();
    });
    connect(m_sharedGallery, &MediaGallery::stopAllRequested, this, [this](){
        m_videoTab->stopPlayback();
    });
    // Save mediaPaths back to the active service item whenever VideoTab's list changes
    connect(m_videoTab, &VideoTab::mediaListChanged, this, [this](const QStringList &paths){
        if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size()
                && m_serviceItems[m_activeServiceIdx].type == ServiceItem::Type::Media) {
            m_serviceItems[m_activeServiceIdx].mediaPaths = paths;
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
        }
        if (m_wsServer) m_wsServer->broadcastPresentationActive(type);
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
    connect(lyricsLayoutBtn, &QPushButton::clicked, this, [this](){
        if (!m_lyricsLayoutDialog) {
            m_lyricsLayoutDialog = new LayoutDialog("lyrics", window());
            connect(m_lyricsLayoutDialog, &QObject::destroyed, this,
                    [this](){ m_lyricsLayoutDialog = nullptr; });
        }
        m_lyricsLayoutDialog->show();
        m_lyricsLayoutDialog->raise();
        m_lyricsLayoutDialog->activateWindow();
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
    m_zoomSlider->setRange(1, 8); m_zoomSlider->setValue(4); m_zoomSlider->setFixedWidth(80);
    m_zoomInBtn  = new QToolButton; m_zoomInBtn->setText("+");  m_zoomInBtn->setToolTip("Zoom in");
    m_zoomLabel  = new QLabel("100%"); m_zoomLabel->setFixedWidth(38);
    m_zoomLabel->setStyleSheet("color:#888;font-size:9px;");

    auto *fontLbl = new QLabel("Font:"); fontLbl->setStyleSheet("color:#888;font-size:9px;");
    m_slideFontCombo = new QFontComboBox;
    m_slideFontCombo->setCurrentFont(QFont("Arial"));
    m_slideFontCombo->setFixedWidth(130); m_slideFontCombo->setFixedHeight(22);
    m_slideFontCombo->setEditable(false);

    auto *wLbl = new QLabel("Cols:"); wLbl->setStyleSheet("color:#888;font-size:9px;");
    m_cardWidthOutBtn = new QToolButton; m_cardWidthOutBtn->setText("-");
    m_cardWidthSlider = new QSlider(Qt::Horizontal);
    m_cardWidthSlider->setRange(1, 5); m_cardWidthSlider->setValue(3); m_cardWidthSlider->setFixedWidth(80);
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
    ctrlL->addSpacing(6);
    ctrlL->addWidget(fontLbl); ctrlL->addWidget(m_slideFontCombo);
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
    connect(m_slideFontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &f) {
        m_slideFontFamily = f.family(); applyZoomToAll(); saveState();
    });
    connect(m_cardWidthSlider, &QSlider::valueChanged, this, [this](int v) {
        m_cardWidthScale = v;
        m_cardWidthLabel->setText(QString::number(v));
        int prevActive = m_activeSlide;
        if (m_liveMode) {
            buildAllLiveSlides();
        } else if (m_paragraphMode && m_activeServiceIdx >= 0) {
            loadParagraphSlides(m_serviceItems[m_activeServiceIdx]);
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
        if (!m_paragraphMode && !m_currentSong.id.isEmpty()) loadSongSlides(m_currentSong);
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
        const ServiceItem *si = nullptr;
        for (const auto &s : m_serviceItems)
            if (s.type == ServiceItem::Type::Lyrics && s.id == m_sequenceSongIds[r])
                { si = &s; break; }
        if (si) offset += songFromItem(*si).allSlides().size();
    }
    return offset;
}

QString LyricsDock::sequenceCurrentSongId() const
{
    if (m_activeServiceIdx < 0 || m_activeServiceIdx >= m_serviceItems.size()) return {};
    const auto &item = m_serviceItems[m_activeServiceIdx];
    return item.type == ServiceItem::Type::Lyrics ? item.id : QString{};
}

// updateServiceListItem \u2014 refresh one row in the service list widget
void LyricsDock::updateServiceListItem(int row)
{
    if (row < 0 || row >= m_serviceItems.size() || !m_serviceList) return;
    auto *lwi = m_serviceList->item(row);
    if (!lwi) return;
    lwi->setData(Qt::UserRole, QVariant::fromValue(m_serviceItems[row]));
    m_serviceList->update(m_serviceList->indexFromItem(lwi));
}

// removeServiceItem \u2014 delete one service order row
void LyricsDock::removeServiceItem(int row)
{
    if (row < 0 || row >= m_serviceItems.size()) return;
    m_serviceItems.removeAt(row);
    delete m_serviceList->takeItem(row);
    // Rebuild sequenceSongIds cache
    m_sequenceSongIds.clear();
    for (const auto &si : m_serviceItems)
        if (si.type == ServiceItem::Type::Lyrics) m_sequenceSongIds << si.id;
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
    m_liveMediaSvcIdx  = -1;
    m_liveMediaFileIdx = -1;
    m_liveMediaPlaying = false;
    m_liveMediaPaused  = false;
    m_activePptSvcIdx  = -1;
    updateSlideCounter();
}

void LyricsDock::loadSongSlides(const Song &song)
{
    clearSlideGrid();
    const int colVpw   = qMax(80, m_slideArea->viewport()->width() - 16);
    const int cols     = m_cardWidthScale;
    const int colCardW = qMax(80, (colVpw - 8 * (cols - 1)) / cols);
    int row = 0, col = 0, globalIdx = 0;

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

        // Effective background for this section
        QString secBg = sec.backgroundPath.isEmpty() ? song.backgroundPath : sec.backgroundPath;

        for (auto sl : secSlides) {
            sl.backgroundPath     = secBg;
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
            if (!secBg.isEmpty())
                btn->setBackgroundThumb(secBg);
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
        // Per-card override in ServiceItem.slideBgs takes priority over section bg
        if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size()) {
            const auto &svcItem = m_serviceItems[m_activeServiceIdx];
            if (svcItem.slideBgs.contains(m_activeSlide))
                newBg = svcItem.slideBgs[m_activeSlide];
        }
        if (newBg.isEmpty())
            newBg = m_slides[m_activeSlide].backgroundPath;
    }

    int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
    if (newBg.isEmpty()) {
        if (!m_currentBgPath.isEmpty()) {
            lyrics_renderer_media_clear(fadeMs);
            m_currentBgPath.clear();
        }
        return;
    }
    if (newBg == m_currentBgPath) return;
    lyrics_renderer_media_push(newBg, fadeMs);
    m_currentBgPath = newBg;
}

void LyricsDock::refreshSlideCardThumbs()
{
    const QMap<int,QString> *overrides = nullptr;
    if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size())
        overrides = &m_serviceItems[m_activeServiceIdx].slideBgs;

    for (auto *btn : m_slideButtons) {
        int idx = btn->slideIndex();
        QString bgPath;
        if (overrides && overrides->contains(idx))
            bgPath = (*overrides)[idx];
        else if (idx >= 0 && idx < m_slides.size())
            bgPath = m_slides[idx].backgroundPath;
        btn->setBackgroundThumb(bgPath);
    }
}

void LyricsDock::onSlideBgAssign(int slideIndex, bool forSection)
{
    // "Current background" = whatever is selected in the media gallery.
    // If nothing is selected, applying = clearing (empty path).
    QString chosen = m_sharedGallery ? m_sharedGallery->activeItemPath() : QString();

    if (m_paragraphMode) {
        // Paragraphs: store per-slide bg in ServiceItem.slideBgs
        if (m_activeServiceIdx < 0 || m_activeServiceIdx >= m_serviceItems.size()) return;
        auto &item = m_serviceItems[m_activeServiceIdx];
        if (item.type != ServiceItem::Type::Paragraphs) return;

        if (forSection) {
            // Apply to all slides in this item
            if (chosen.isEmpty()) {
                item.slideBgs.clear();
            } else {
                QStringList slides = item.parseSlides();
                for (int i = 0; i < slides.size(); ++i)
                    item.slideBgs[i] = chosen;
            }
        } else {
            if (chosen.isEmpty())
                item.slideBgs.remove(slideIndex);
            else
                item.slideBgs[slideIndex] = chosen;
        }
        saveServiceOrder();
        int savedActive = m_activeSlide;
        loadParagraphSlides(item);
        if (savedActive >= 0 && savedActive < m_slideButtons.size())
            setActiveSlide(savedActive);
        // Immediately push bg to OBS if a background was set
        if (!chosen.isEmpty()) {
            lyrics_renderer_media_push(chosen, 0);
            m_currentBgPath = chosen;
        }
        return;
    }

    // Lyrics: store bg on the Song section
    if (forSection) {
        // Apply to all sections in m_currentSong
        for (auto &sec : m_currentSong.sections)
            sec.backgroundPath = chosen;
        // Embed back into the service item
        if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size())
            m_serviceItems[m_activeServiceIdx].sections = m_currentSong.sections;
    } else {
        // Card-level: store in the active ServiceItem's slideBgs map
        if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size()) {
            auto &svcItem = m_serviceItems[m_activeServiceIdx];
            if (chosen.isEmpty())
                svcItem.slideBgs.remove(slideIndex);
            else
                svcItem.slideBgs[slideIndex] = chosen;
        }
    }

    // Keep library in sync
    SongManager::instance().updateSong(m_currentSong);
    SongManager::instance().save();
    saveServiceOrder();

    // Rebuild slide data only — avoids destroying/recreating slide buttons (visual glitch).
    // applySlideBackground() handles both assign and clear, and only updates OBS when
    // the active slide's effective background actually changed.
    m_slides = m_currentSong.allSlides();
    refreshSlideCardThumbs();
    applySlideBackground();
}

void LyricsDock::onSlideBgClear(int slideIndex, bool forSection)
{
    if (m_paragraphMode) {
        if (m_activeServiceIdx < 0 || m_activeServiceIdx >= m_serviceItems.size()) return;
        auto &item = m_serviceItems[m_activeServiceIdx];
        if (forSection)
            item.slideBgs.clear();
        else
            item.slideBgs.remove(slideIndex);
        saveServiceOrder();
        int savedActive = m_activeSlide;
        loadParagraphSlides(item);
        if (savedActive >= 0 && savedActive < m_slideButtons.size())
            setActiveSlide(savedActive);
        return;
    }

    int secIdx = (slideIndex >= 0 && slideIndex < m_slideSectionIndices.size())
                 ? m_slideSectionIndices[slideIndex] : -1;

    if (forSection) {
        if (secIdx >= 0 && secIdx < m_currentSong.sections.size()) {
            m_currentSong.sections[secIdx].backgroundPath.clear();
            // Embed back into the service item
            if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size())
                m_serviceItems[m_activeServiceIdx].sections = m_currentSong.sections;
        }
    } else {
        if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size())
            m_serviceItems[m_activeServiceIdx].slideBgs.remove(slideIndex);
    }

    SongManager::instance().updateSong(m_currentSong);
    SongManager::instance().save();
    saveServiceOrder();

    m_slides = m_currentSong.allSlides();
    refreshSlideCardThumbs();
    applySlideBackground();
}

void LyricsDock::pushCurrentSlide()
{
    if (m_activeSlide < 0 || m_activeSlide >= m_slides.size()) return;
    const LyricsSlide &sl  = m_slides[m_activeSlide];
    const QString     &title = m_currentSong.title;

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

void LyricsDock::onAddServiceItem()
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu{background:#1e1e24;color:#f0f0f5;border:1px solid #3a3a48;}"
        "QMenu::item:selected{background:#2a4a6a;}");
    struct { const char *label; ServiceItem::Type type; } types[] = {
        {"Lyrics",       ServiceItem::Type::Lyrics},
        {"Paragraphs",   ServiceItem::Type::Paragraphs},
        {"Media",        ServiceItem::Type::Media},
        {"Presentation", ServiceItem::Type::Presentation},
    };
    for (const auto &t : types)
        menu.addAction(t.label);
    auto *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    ServiceItem item;
    for (const auto &t : types)
        if (chosen->text() == t.label) { item.type = t.type; break; }
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Lyrics: open song library popup
    if (item.type == ServiceItem::Type::Lyrics) {
        SongLibraryPopup popup(this);
        popup.setSequenceIds(m_sequenceSongIds);
        if (popup.exec() != QDialog::Accepted) return;
        for (int i = m_serviceItems.size() - 1; i >= 0; --i)
            if (m_serviceItems[i].type == ServiceItem::Type::Lyrics
                    && popup.deletedFromLibrary().contains(m_serviceItems[i].id))
                removeServiceItem(i);
        for (int i = m_serviceItems.size() - 1; i >= 0; --i)
            if (m_serviceItems[i].type == ServiceItem::Type::Lyrics
                    && popup.removedIds().contains(m_serviceItems[i].id))
                removeServiceItem(i);
        for (const QString &id : popup.addedIds()) {
            if (m_sequenceSongIds.contains(id)) continue;
            auto *song = SongManager::instance().findById(id);
            if (!song) continue;
            ServiceItem si;
            si.type     = ServiceItem::Type::Lyrics;
            si.id       = id;
            si.title    = song->title;
            si.artist   = song->artist;
            si.sections = song->sections;  // embed full lyrics into service
            int insertRow = m_serviceList->currentRow() + 1;
            if (insertRow <= 0 || insertRow > m_serviceItems.size())
                insertRow = m_serviceItems.size();
            m_serviceItems.insert(insertRow, si);
        }
        rebuildServiceList();
        m_sequenceSongIds.clear();
        for (const auto &si : m_serviceItems)
            if (si.type == ServiceItem::Type::Lyrics) m_sequenceSongIds << si.id;
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

    int insertRow = m_serviceList->currentRow() + 1;
    if (insertRow <= 0 || insertRow > m_serviceItems.size())
        insertRow = m_serviceItems.size();
    m_serviceItems.insert(insertRow, item);
    rebuildServiceList();
    saveServiceOrder();
    if (m_wsServer) m_wsServer->broadcastServiceOrder(buildServiceOrderJson());
    m_serviceList->setCurrentRow(insertRow);
}

void LyricsDock::onRemoveServiceItem()
{
    int row = m_serviceList->currentRow();
    if (row < 0) return;
    removeServiceItem(row);
    updateServiceItemButtons();
}

void LyricsDock::onServiceItemUp()
{
    int row = m_serviceList->currentRow();
    if (row <= 0) return;
    m_serviceItems.swapItemsAt(row, row - 1);
    // Also swap in widget
    auto *taken = m_serviceList->takeItem(row);
    m_serviceList->insertItem(row - 1, taken);
    m_serviceList->setCurrentRow(row - 1);
    // Rebuild sequenceSongIds
    m_sequenceSongIds.clear();
    for (const auto &si : m_serviceItems)
        if (si.type == ServiceItem::Type::Lyrics) m_sequenceSongIds << si.id;
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::onServiceItemDown()
{
    int row = m_serviceList->currentRow();
    if (row < 0 || row >= m_serviceItems.size() - 1) return;
    m_serviceItems.swapItemsAt(row, row + 1);
    auto *taken = m_serviceList->takeItem(row);
    m_serviceList->insertItem(row + 1, taken);
    m_serviceList->setCurrentRow(row + 1);
    m_sequenceSongIds.clear();
    for (const auto &si : m_serviceItems)
        if (si.type == ServiceItem::Type::Lyrics) m_sequenceSongIds << si.id;
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::onServiceItemSelected(int row)
{
    m_activeServiceIdx = row;
    updateServiceItemButtons();
    if (row < 0 || row >= m_serviceItems.size()) {
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
    loadServiceItemContent(row);
}

void LyricsDock::loadServiceItemContent(int idx)
{
    if (idx < 0 || idx >= m_serviceItems.size()) {
        if (m_contentStack) m_contentStack->setCurrentIndex(3);
        return;
    }
    const auto &item = m_serviceItems[idx];
    // Restore saved color preset for this section
    if (item.colorPresetIdx >= 0 && item.colorPresetIdx < 6 && m_colorBtn[item.colorPresetIdx]) {
        QColor fc = m_colorBtn[item.colorPresetIdx]->fontColor();
        QColor oc = m_colorBtn[item.colorPresetIdx]->outlineColor();
        uint32_t fcObs = ((uint32_t)fc.blue()  << 16) | ((uint32_t)fc.green() <<  8) | (uint32_t)fc.red();
        uint32_t ocObs = ((uint32_t)oc.blue()  << 16) | ((uint32_t)oc.green() <<  8) | (uint32_t)oc.red();
        lyrics_renderer_set_text_color(fcObs, ocObs);
        lyrics_renderer_set_paragraph_text_color(fcObs, ocObs); // paragraph sections
        lyrics_renderer_set_chinese_color(fcObs, ocObs);
        m_slideFontColor = fc;
        // Highlight the matching button
        for (int j = 0; j < 6; ++j)
            if (m_colorBtn[j]) m_colorBtn[j]->setActive(j == item.colorPresetIdx);
    }

    switch (item.type) {
    case ServiceItem::Type::Lyrics: {
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
    case ServiceItem::Type::Paragraphs:
        m_paragraphMode = true;
        m_contentStack->setCurrentIndex(0);
        loadParagraphSlides(item);
        break;
    case ServiceItem::Type::Media:
        m_paragraphMode = false;
        m_contentStack->setCurrentIndex(1);
        if (m_videoTab) m_videoTab->setMediaList(item.mediaPaths);
        break;
    case ServiceItem::Type::Presentation:
        m_paragraphMode = false;
        m_contentStack->setCurrentIndex(2);
        break;
    }
}

void LyricsDock::updateServiceItemButtons()
{
    int row = m_serviceList ? m_serviceList->currentRow() : -1;
    bool has = (row >= 0 && row < m_serviceItems.size());
    if (m_removeItemBtn) m_removeItemBtn->setEnabled(has);
    if (m_itemUpBtn)   m_itemUpBtn->setEnabled(has && row > 0);
    if (m_itemDownBtn) m_itemDownBtn->setEnabled(has && row < m_serviceItems.size() - 1);
}

void LyricsDock::rebuildServiceList()
{
    if (!m_serviceList) return;
    QSignalBlocker blk(m_serviceList);
    m_serviceList->clear();
    for (const auto &item : m_serviceItems) {
        auto *lwi = new QListWidgetItem;
        lwi->setData(Qt::UserRole, QVariant::fromValue(item));
        lwi->setSizeHint({1, 52});
        m_serviceList->addItem(lwi);
    }
}

// ── loadParagraphSlides ───────────────────────────────────────

void LyricsDock::loadParagraphSlides(const ServiceItem &item)
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
        if (item.slideBgs.contains(i))
            btn->setBackgroundThumb(item.slideBgs[i]);
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

    for (int svcIdx = 0; svcIdx < m_serviceItems.size(); ++svcIdx) {
        const auto &svcItem = m_serviceItems[svcIdx];

        // Flush current row before separator so it spans the full width
        if (col > 0) { col = 0; ++row; }

        // ── Separator row with service item title ──────────────────
        auto *sep = new QWidget(m_slideContainer);
        sep->setFixedHeight(28);
        const QColor tc = svcItem.typeColor();
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
        QString firstChar = svcItem.title.isEmpty()
            ? svcItem.typeLabel().left(1)
            : QString(svcItem.title.at(0));
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
            svcItem.title.isEmpty() ? svcItem.typeLabel() : svcItem.title, sep);
        lbl->setStyleSheet(
            QString("color: %1; font-size: 10px; font-weight: bold; background: transparent;")
            .arg(tc.name()));
        sepL->addWidget(lbl, 1);
        m_slideGrid->addWidget(sep, row, 0, 1, cols);
        m_liveSeparators << sep;
        ++row;

        // Use this section's saved color preset for its slide buttons
        QColor svcFontColor = m_slideFontColor;
        if (svcItem.colorPresetIdx >= 0 && svcItem.colorPresetIdx < 6
            && m_colorBtn[svcItem.colorPresetIdx])
            svcFontColor = m_colorBtn[svcItem.colorPresetIdx]->fontColor();

        // ── Slides for this service item ───────────────────────────
        if (svcItem.type == ServiceItem::Type::Lyrics) {
            Song song = songFromItem(svcItem);
            {
                const auto allSlides = song.allSlides();
                for (int i = 0; i < allSlides.size(); ++i) {
                    const auto &sl = allSlides[i];
                    auto *btn = new SlideButton(sl.section, sl.text,
                                               globalIdx, svcIdx, i + 1, m_slideContainer);
                    btn->setFontColor(svcFontColor);
                    btn->applyScale(m_slideScale, m_slideFontFamily, cardW);
                    // Background: card-level override first, then section default
                    QString bgPath = svcItem.slideBgs.value(i);
                    if (bgPath.isEmpty()) bgPath = sl.backgroundPath;
                    if (!bgPath.isEmpty()) btn->setBackgroundThumb(bgPath);
                    connect(btn, &SlideButton::clicked, this, &LyricsDock::onSlideClicked);
                    m_slideGrid->addWidget(btn, row, col);
                    m_slideButtons << btn;
                    if (++col >= cols) { col = 0; ++row; }
                    ++globalIdx;
                }
            }
        } else if (svcItem.type == ServiceItem::Type::Paragraphs) {
            QStringList slides = svcItem.parseSlides();
            for (int i = 0; i < slides.size(); ++i) {
                auto *btn = new SlideButton(svcItem.title.left(12), slides[i],
                                           globalIdx, svcIdx, i + 1, m_slideContainer);
                btn->setSectionColor(svcItem.typeColor());
                btn->setFontColor(svcFontColor);
                btn->applyScale(m_slideScale, m_slideFontFamily, cardW);
                if (svcItem.slideBgs.contains(i))
                    btn->setBackgroundThumb(svcItem.slideBgs[i]);
                connect(btn, &SlideButton::clicked, this, &LyricsDock::onSlideClicked);
                m_slideGrid->addWidget(btn, row, col);
                m_slideButtons << btn;
                if (++col >= cols) { col = 0; ++row; }
                ++globalIdx;
            }
        } else if (svcItem.type == ServiceItem::Type::Media) {
            for (int fi = 0; fi < svcItem.mediaPaths.size(); ++fi) {
                auto *card = new MediaSlideCard(svcItem.mediaPaths[fi], svcIdx, fi, m_slideContainer);
                card->setFixedWidth(cardW);
                connect(card, &MediaSlideCard::clicked,          this, &LyricsDock::onLiveMediaCardClicked);
                connect(card, &MediaSlideCard::playPauseClicked, this, &LyricsDock::onLiveMediaPlayPause);
                connect(card, &MediaSlideCard::rewindClicked,    this, &LyricsDock::onLiveMediaRewind);
                m_slideGrid->addWidget(card, row, col);
                m_liveExtraCards << card;
                if (++col >= cols) { col = 0; ++row; }
            }
            if (svcItem.mediaPaths.isEmpty()) {
                // Placeholder card so media service items are visible in live mode
                auto *btn = new SlideButton("[Media]",
                                            svcItem.title.isEmpty() ? "(no media)" : svcItem.title,
                                            globalIdx, svcIdx, 1, m_slideContainer);
                btn->setSectionColor(svcItem.typeColor());
                btn->applyScale(m_slideScale, m_slideFontFamily, cardW);
                m_slideGrid->addWidget(btn, row, col);
                m_slideButtons << btn;
                if (++col >= cols) { col = 0; ++row; }
                ++globalIdx;
            }
        } else if (svcItem.type == ServiceItem::Type::Presentation) {
            auto *card = new PptLiveCard(svcItem.title, svcIdx, m_slideContainer);
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
        int row = m_serviceList ? m_serviceList->currentRow() : -1;
        if (row >= 0) loadServiceItemContent(row);
        else m_contentStack->setCurrentIndex(3);
    }
}

// ── Live media card slots ─────────────────────────────────────

// Push a media file to OBS and update card visuals. Called only for a fresh start.
void LyricsDock::onLiveMediaCardClicked(int svcIdx, int fileIdx)
{
    // Same card — handle resume / pause / play based on current state
    if (m_liveMediaSvcIdx == svcIdx && m_liveMediaFileIdx == fileIdx) {
        if (m_liveMediaPaused) {
            // Resume: use play_pause API — never re-push (that crashes OBS)
            lyrics_renderer_mv_play_pause(false);
            m_liveMediaPlaying = true;
            m_liveMediaPaused  = false;
            for (auto *w : m_liveExtraCards) {
                if (auto *card = qobject_cast<MediaSlideCard*>(w))
                    if (card->svcIdx() == svcIdx && card->fileIdx() == fileIdx)
                        card->setPlaying(true, false);
            }
            return;
        }
        if (m_liveMediaPlaying) {
            // Already playing — treat click as pause
            onLiveMediaPlayPause(svcIdx, fileIdx);
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
                    if (card->svcIdx() == m_liveMediaSvcIdx && card->fileIdx() == m_liveMediaFileIdx)
                        card->setPlaying(false, false);
            }
        }
        m_liveMediaSvcIdx  = svcIdx;
        m_liveMediaFileIdx = fileIdx;
        m_liveMediaPlaying = false;
        m_liveMediaPaused  = false;
    }

    // Stop any active presentation (mutual exclusion)
    if (m_pptTab && m_pptTab->activeType() >= 0) {
        m_pptTab->requestShow(-1);
        m_activePptSvcIdx = -1;
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(false);
        }
    }

    // Fresh start
    if (svcIdx < 0 || svcIdx >= m_serviceItems.size()) return;
    const QString &path = m_serviceItems[svcIdx].mediaPaths.value(fileIdx);
    if (path.isEmpty()) return;

    // Clear any displayed lyrics/CTW text so only the media is visible
    lyrics_renderer_clear();
    lyrics_renderer_paragraph_clear();
    m_currentBgPath.clear(); // force background re-push on next lyrics slide

    if (isVideoFile(path)) {
        if (m_audioBin) m_audioBin->stopPlayback();
        lyrics_renderer_mv_video_push(path, 0);
    } else
        lyrics_renderer_media_push(path, 0);

    m_liveMediaPlaying = true;
    m_liveMediaPaused  = false;

    for (auto *w : m_liveExtraCards) {
        if (auto *card = qobject_cast<MediaSlideCard*>(w))
            card->setPlaying(card->svcIdx() == svcIdx && card->fileIdx() == fileIdx, false);
    }
    setActiveSlide(-1);
}

void LyricsDock::onLiveMediaPlayPause(int svcIdx, int fileIdx)
{
    if (m_liveMediaSvcIdx != svcIdx || m_liveMediaFileIdx != fileIdx) {
        onLiveMediaCardClicked(svcIdx, fileIdx);
        return;
    }

    if (m_liveMediaPaused) {
        // Resume
        if (svcIdx >= 0 && svcIdx < m_serviceItems.size()) {
            const QString &p = m_serviceItems[svcIdx].mediaPaths.value(fileIdx);
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
                if (card->svcIdx() == svcIdx && card->fileIdx() == fileIdx)
                    card->setPlaying(true, false);
        }
        return;
    }

    if (!m_liveMediaPlaying) {
        // Fully stopped — fresh start
        onLiveMediaCardClicked(svcIdx, fileIdx);
        return;
    }

    // Currently playing — pause it
    if (svcIdx >= 0 && svcIdx < m_serviceItems.size()) {
        const QString &p = m_serviceItems[svcIdx].mediaPaths.value(fileIdx);
        if (isVideoFile(p))
            lyrics_renderer_mv_play_pause(true);
        else
            lyrics_renderer_media_clear();
    }
    m_liveMediaPlaying = false;
    m_liveMediaPaused  = true;

    for (auto *w : m_liveExtraCards) {
        if (auto *card = qobject_cast<MediaSlideCard*>(w))
            if (card->svcIdx() == svcIdx && card->fileIdx() == fileIdx)
                card->setPlaying(true, true);
    }
}

void LyricsDock::onLiveMediaRewind(int svcIdx, int fileIdx)
{
    if (svcIdx < 0 || svcIdx >= m_serviceItems.size()) return;
    const auto &item = m_serviceItems[svcIdx];
    if (fileIdx < 0 || fileIdx >= item.mediaPaths.size()) return;

    if (m_liveMediaSvcIdx == svcIdx && m_liveMediaFileIdx == fileIdx) {
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
                    if (card->svcIdx() == svcIdx && card->fileIdx() == fileIdx)
                        card->setPlaying(true, false);
            }
        }
    } else {
        // Rewind on an inactive card — start it from the beginning
        onLiveMediaCardClicked(svcIdx, fileIdx);
    }
}

// ── Live presentation card slots ──────────────────────────────

static void pptEnsureActive(PptTab *pptTab) {
    if (!pptTab) return;
    if (pptTab->activeType() >= 0) return; // already showing
    for (int t = 0; t <= 2; ++t) {
        if (pptTab->isTypeEnabled(t)) { pptTab->requestShow(t); return; }
    }
    // No type explicitly enabled yet — default to PowerPoint and auto-enable it
    pptTab->requestShow(0);
}

void LyricsDock::onLivePptActivate(int svcIdx)
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
        m_liveMediaSvcIdx  = -1;
        m_liveMediaFileIdx = -1;
    }

    // Toggle: clicking an already-active card stops it
    if (m_activePptSvcIdx == svcIdx && m_pptTab->activeType() >= 0) {
        m_pptTab->requestShow(-1); // stop
        m_activePptSvcIdx = -1;
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(false);
        }
        return;
    }

    // Presentation takes over the screen — background is no longer ours to track
    m_currentBgPath.clear();

    // Start / switch to this card
    pptEnsureActive(m_pptTab);
    m_activePptSvcIdx = svcIdx;

    for (auto *w : m_liveExtraCards) {
        if (auto *c = qobject_cast<PptLiveCard*>(w))
            c->setActive(c->svcIdx() == svcIdx);
    }
}

void LyricsDock::onLivePptPrev(int svcIdx)
{
    if (!m_pptTab) return;
    // Activate without toggle-off if not already showing for this card
    if (m_activePptSvcIdx != svcIdx || m_pptTab->activeType() < 0) {
        // Stop any live media (mutual exclusion)
        if (m_liveMediaPlaying || m_liveMediaPaused) {
            lyrics_renderer_mv_clear();
            lyrics_renderer_media_clear();
            for (auto *w : m_liveExtraCards) {
                if (auto *c = qobject_cast<MediaSlideCard*>(w))
                    c->setPlaying(false, false);
            }
            m_liveMediaPlaying = m_liveMediaPaused = false;
            m_liveMediaSvcIdx  = m_liveMediaFileIdx = -1;
        }
        pptEnsureActive(m_pptTab);
        m_activePptSvcIdx = svcIdx;
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(c->svcIdx() == svcIdx);
        }
    }
    pptPrev();
}

void LyricsDock::onLivePptNext(int svcIdx)
{
    if (!m_pptTab) return;
    // Activate without toggle-off if not already showing for this card
    if (m_activePptSvcIdx != svcIdx || m_pptTab->activeType() < 0) {
        // Stop any live media (mutual exclusion)
        if (m_liveMediaPlaying || m_liveMediaPaused) {
            lyrics_renderer_mv_clear();
            lyrics_renderer_media_clear();
            for (auto *w : m_liveExtraCards) {
                if (auto *c = qobject_cast<MediaSlideCard*>(w))
                    c->setPlaying(false, false);
            }
            m_liveMediaPlaying = m_liveMediaPaused = false;
            m_liveMediaSvcIdx  = m_liveMediaFileIdx = -1;
        }
        pptEnsureActive(m_pptTab);
        m_activePptSvcIdx = svcIdx;
        for (auto *w : m_liveExtraCards) {
            if (auto *c = qobject_cast<PptLiveCard*>(w))
                c->setActive(c->svcIdx() == svcIdx);
        }
    }
    pptNext();
}

// ── onParagraphsContextMenu ───────────────────────────────────

void LyricsDock::onParagraphsContextMenu(const QPoint &pos)
{
    if (!m_paragraphMode) return;
    int row = m_activeServiceIdx;
    if (row < 0 || row >= m_serviceItems.size()) return;
    if (m_serviceItems[row].type != ServiceItem::Type::Paragraphs) return;
    QMenu menu(this);
    auto *editAct = menu.addAction("Edit slides...");
    auto *chosen = menu.exec(m_slideArea ? m_slideArea->mapToGlobal(pos) : QCursor::pos());
    if (chosen == editAct) showParagraphEditDialog(row);
}

// ── showParagraphEditDialog ───────────────────────────────────

void LyricsDock::showParagraphEditDialog(int serviceIdx)
{
    if (serviceIdx < 0 || serviceIdx >= m_serviceItems.size()) return;
    auto &item = m_serviceItems[serviceIdx];

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
    dlgL->addLayout(btnRow);

    connect(applyBtn,  &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    item.title = titleEdit->text().trimmed();
    if (item.title.isEmpty()) item.title = "Paragraphs";
    item.text  = bodyEdit->toPlainText();
    updateServiceListItem(serviceIdx);
    saveServiceOrder();

    if (m_activeServiceIdx == serviceIdx)
        loadParagraphSlides(item);
}

// ── buildMediaPanel ───────────────────────────────────────────

QWidget *LyricsDock::buildMediaPanel()
{
    return m_videoTab;
}

void LyricsDock::saveServiceOrder()
{
    QJsonObject root;
    root["version"]     = 1;
    root["activeIndex"] = m_activeServiceIdx;
    QJsonArray arr;
    for (const auto &item : m_serviceItems)
        arr.append(item.toJson());
    root["items"] = arr;

    QString path = QStandardPaths::writableLocation(
                       QStandardPaths::AppDataLocation) + "/service.json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
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
                    ServiceItem si;
                    si.type   = ServiceItem::Type::Lyrics;
                    si.id     = id;
                    si.title  = song->title;
                    si.artist = song->artist;
                    m_serviceItems << si;
                    m_sequenceSongIds << id;
                }
            }
        }
        rebuildServiceList();
        return;
    }

    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) { rebuildServiceList(); return; }

    const auto obj = doc.object();
    m_serviceItems.clear();
    m_sequenceSongIds.clear();

    for (const auto &v : obj["items"].toArray()) {
        if (!v.isObject()) continue;
        ServiceItem item = ServiceItem::fromJson(v.toObject());
        if (item.type == ServiceItem::Type::Lyrics) {
            // If no sections embedded yet (old service.json), copy from library
            if (item.sections.isEmpty()) {
                if (auto *song = SongManager::instance().findById(item.id)) {
                    item.sections = song->sections;
                    item.title    = song->title;
                    item.artist   = song->artist;
                }
            }
            m_sequenceSongIds << item.id;
        }
        m_serviceItems << item;
    }

    rebuildServiceList();

    int savedIdx = obj["activeIndex"].toInt(-1);
    if (savedIdx >= 0 && savedIdx < m_serviceItems.size()) {
        m_serviceList->setCurrentRow(savedIdx);
        // Don't call loadServiceItemContent here — UI not ready yet
    }
}

void LyricsDock::onSequenceEditSong()
{
    int row = m_activeServiceIdx;
    if (row < 0 || row >= m_serviceItems.size()) return;
    if (m_serviceItems[row].type != ServiceItem::Type::Lyrics) return;
    QString id = m_serviceItems[row].id;

    // Build a Song from embedded sections (with library fallback)
    Song current = songFromItem(m_serviceItems[row]);

    SongEditDialog dlg(current, this);
    if (dlg.exec() != QDialog::Accepted) return;

    Song res = dlg.result();
    // Always update library so it stays in sync
    if (dlg.savedAsNew()) {
        SongManager::instance().addSong(res);
    } else {
        SongManager::instance().updateSong(res);
    }
    SongManager::instance().save();

    // Update embedded copy in the service item
    if (dlg.savedAsNew()) {
        m_sequenceSongIds[m_sequenceSongIds.indexOf(id)] = res.id;
        m_serviceItems[row].id = res.id;
    }
    m_serviceItems[row].title    = res.title;
    m_serviceItems[row].artist   = res.artist;
    m_serviceItems[row].sections = res.sections;
    updateServiceListItem(row);

    int prevActive = m_activeSlide;
    m_currentSong = res;
    loadSongSlides(m_currentSong);
    if (prevActive >= 0 && prevActive < m_slides.size()) {
        setActiveSlide(prevActive);
        pushCurrentSlide();
    }
    saveServiceOrder();
    broadcastSequence();
}

void LyricsDock::onServiceContextMenu(const QPoint &pos)
{
    auto *item = m_serviceList->itemAt(pos);
    if (!item) return;
    int row = m_serviceList->row(item);
    m_serviceList->setCurrentRow(row);
    if (row < 0 || row >= m_serviceItems.size()) return;

    QMenu menu(m_serviceList);
    auto *removeAct = menu.addAction(
        style()->standardIcon(QStyle::SP_TrashIcon), "Remove");
    QAction *editAct    = nullptr;
    QAction *renameAct  = nullptr;
    const auto &svcItem = m_serviceItems[row];
    if (svcItem.type == ServiceItem::Type::Lyrics)
        editAct = menu.addAction(makePenIcon(), "Edit Song");
    else
        renameAct = menu.addAction(makePenIcon(), "Rename");

    auto *chosen = menu.exec(m_serviceList->viewport()->mapToGlobal(pos));
    if (chosen == removeAct) {
        onRemoveServiceItem();
    } else if (editAct && chosen == editAct) {
        onSequenceEditSong();
    } else if (renameAct && chosen == renameAct) {
        bool ok = false;
        QString newTitle = QInputDialog::getText(
            this, "Rename Section",
            "New name:", QLineEdit::Normal,
            m_serviceItems[row].title, &ok);
        if (ok && !newTitle.trimmed().isEmpty()) {
            m_serviceItems[row].title = newTitle.trimmed();
            updateServiceListItem(row);
            saveServiceOrder();
            // Refresh panel if this section is currently loaded
            if (row == m_activeServiceIdx && !m_liveMode)
                loadServiceItemContent(row);
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
        if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size()) {
            const auto &item = m_serviceItems[m_activeServiceIdx];
            QStringList slides = item.parseSlides();
            if (index >= 0 && index < slides.size()) {
                // Push assigned background if any
                QString bgPath = item.slideBgs.value(index);
                if (!bgPath.isEmpty() && bgPath != m_currentBgPath) {
                    int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
                    lyrics_renderer_media_push(bgPath, fadeMs);
                    m_currentBgPath = bgPath;
                }
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
            if (m_liveMediaSvcIdx >= 0 && m_liveMediaSvcIdx < m_serviceItems.size()) {
                const QString &p = m_serviceItems[m_liveMediaSvcIdx].mediaPaths.value(m_liveMediaFileIdx);
                wasVideo = isVideoFile(p);
                if (!wasVideo && m_currentBgPath.isEmpty())
                    m_currentBgPath = p; // let bg block compare against this image
            }
            if (wasVideo)
                lyrics_renderer_media_clear();
            for (auto *w : m_liveExtraCards)
                if (auto *c = qobject_cast<MediaSlideCard*>(w))
                    c->setPlaying(false, false);
            m_liveMediaPlaying = false;
            m_liveMediaPaused  = false;
            m_liveMediaSvcIdx  = -1;
            m_liveMediaFileIdx = -1;
        }

        // Clicking a lyrics/paragraph slide stops any active presentation
        if (m_pptTab && m_pptTab->activeType() >= 0) {
            m_pptTab->requestShow(-1);
            m_activePptSvcIdx = -1;
            for (auto *w : m_liveExtraCards) {
                if (auto *c = qobject_cast<PptLiveCard*>(w))
                    c->setActive(false);
            }
        }

        // In live mode, index is a flat global index across all items
        setActiveSlide(index);  // highlight button
        // Find which service item + slide this index corresponds to
        int globalIdx = 0;
        for (int svcIdx = 0; svcIdx < m_serviceItems.size(); ++svcIdx) {
            const auto &svcItem = m_serviceItems[svcIdx];
            if (svcItem.type == ServiceItem::Type::Lyrics) {
                Song song = songFromItem(svcItem);
                const auto allSlides = song.allSlides();
                if (!allSlides.isEmpty() && index < globalIdx + allSlides.size()) {
                    int localIdx = index - globalIdx;
                    m_currentSong = song;
                    m_currentSongSlideOffset = globalIdx;
                    m_slides = allSlides;

                    // Apply section color preset to OBS before pushing
                    if (svcItem.colorPresetIdx >= 0 && svcItem.colorPresetIdx < 6
                            && m_colorBtn[svcItem.colorPresetIdx]) {
                        QColor fc = m_colorBtn[svcItem.colorPresetIdx]->fontColor();
                        QColor oc = m_colorBtn[svcItem.colorPresetIdx]->outlineColor();
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
                        int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
                        if (newBg.isEmpty()) {
                            if (!m_currentBgPath.isEmpty()) { lyrics_renderer_media_clear(fadeMs); m_currentBgPath.clear(); }
                        } else if (newBg != m_currentBgPath) {
                            lyrics_renderer_media_push(newBg, fadeMs);
                            m_currentBgPath = newBg;
                        }
                    }

                    // Push slide content using local index into m_slides[]
                    const auto &sl = allSlides[localIdx];
                    if (sl.isChinese)
                        lyrics_renderer_push_chinese(sl.text, sl.pinyin, sl.english, sl.section, m_currentSong.title);
                    else
                        lyrics_renderer_push(sl.text, sl.section, m_currentSong.title);
                    m_activeSlide = localIdx;

                    if (m_wsServer) m_wsServer->broadcastSlide(sl.text, sl.section, m_currentSong.title, index);
                    if (m_wsServer) m_wsServer->broadcastActiveMode("lyr");
                    return;
                }
                globalIdx += allSlides.size();
            } else if (svcItem.type == ServiceItem::Type::Paragraphs) {
                QStringList slides = svcItem.parseSlides();
                if (index < globalIdx + slides.size()) {
                    int localIdx = index - globalIdx;

                    // Apply section color preset to OBS before pushing
                    if (svcItem.colorPresetIdx >= 0 && svcItem.colorPresetIdx < 6
                            && m_colorBtn[svcItem.colorPresetIdx]) {
                        QColor fc = m_colorBtn[svcItem.colorPresetIdx]->fontColor();
                        QColor oc = m_colorBtn[svcItem.colorPresetIdx]->outlineColor();
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
                        int fadeMs = m_sharedGallery ? m_sharedGallery->fadeDurationMs() : 500;
                        if (newBg.isEmpty()) {
                            if (!m_currentBgPath.isEmpty()) { lyrics_renderer_media_clear(fadeMs); m_currentBgPath.clear(); }
                        } else if (newBg != m_currentBgPath) {
                            lyrics_renderer_media_push(newBg, fadeMs);
                            m_currentBgPath = newBg;
                        }
                    }

                    lyrics_renderer_paragraph_push(svcItem.title, slides[localIdx]);
                    if (m_wsServer) m_wsServer->broadcastScripture(svcItem.title, slides[localIdx]);
                    if (m_wsServer) m_wsServer->broadcastActiveMode("paragraph");
                    return;
                }
                globalIdx += slides.size();
            } else if (svcItem.type == ServiceItem::Type::Media) {
                if (svcItem.mediaPaths.isEmpty()) {
                    // Placeholder SlideButton was created — consumes one globalIdx slot
                    if (index == globalIdx) {
                        if (m_serviceList) m_serviceList->setCurrentRow(svcIdx);
                        onLiveToggled(false);
                        return;
                    }
                    ++globalIdx;
                }
                // Media with actual paths: MediaSlideCards used, no globalIdx slot consumed
            } else if (svcItem.type == ServiceItem::Type::Presentation) {
                // PptLiveCard used — no globalIdx slot consumed
            }
        }
        return;
    }

    // Default: Lyrics mode
    setActiveSlide(index);
    pushCurrentSlide();
    if (m_wsServer) m_wsServer->broadcastActiveMode("lyr");
    if (m_sharedGallery) m_sharedGallery->replayActive();
}

void LyricsDock::onSlideEditRequested(int sectionIndex)
{
    if (m_paragraphMode) {
        showParagraphEditDialog(m_activeServiceIdx);
        return;
    }
    if (m_currentSong.id.isEmpty()) return;
    QString oldId = m_currentSong.id;

    SongEditDialog dlg(m_currentSong, this, sectionIndex);
    if (dlg.exec() != QDialog::Accepted) return;

    Song res = dlg.result();
    // Update library
    if (dlg.savedAsNew()) {
        SongManager::instance().addSong(res);
    } else {
        SongManager::instance().updateSong(res);
    }
    SongManager::instance().save();

    // Update embedded sections in every service instance of this song
    for (int i = 0; i < m_serviceItems.size(); ++i) {
        if (m_serviceItems[i].type != ServiceItem::Type::Lyrics) continue;
        if (m_serviceItems[i].id != oldId && m_serviceItems[i].id != res.id) continue;
        if (dlg.savedAsNew()) m_serviceItems[i].id = res.id;
        m_serviceItems[i].title    = res.title;
        m_serviceItems[i].artist   = res.artist;
        m_serviceItems[i].sections = res.sections;
        updateServiceListItem(i);
    }
    if (dlg.savedAsNew()) {
        m_sequenceSongIds.clear();
        for (const auto &si : m_serviceItems)
            if (si.type == ServiceItem::Type::Lyrics) m_sequenceSongIds << si.id;
    }

    int prevActive = m_activeSlide;
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
                        && m_activeServiceIdx >= 0
                        && m_activeServiceIdx < m_serviceItems.size()
                        && m_activeSlide >= 0;
    bool hasLyrics = !m_paragraphMode && !m_liveMode
                     && !m_currentSong.id.isEmpty()
                     && m_activeSlide >= 0
                     && m_activeSlide < m_slides.size();

    if (hasParagraph) {
        const auto &item = m_serviceItems[m_activeServiceIdx];
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

    if (m_paragraphMode && m_activeServiceIdx >= 0
            && m_activeServiceIdx < m_serviceItems.size()) {
        auto &item = m_serviceItems[m_activeServiceIdx];
        QStringList slides = item.parseSlides();
        if (m_activeSlide >= 0 && m_activeSlide < slides.size()) {
            slides[m_activeSlide] = newText.trimmed();
            item.text = slides.join("\n\n\n");
            saveServiceOrder();
            int saved = m_activeSlide;
            loadParagraphSlides(item);
            if (saved >= 0 && saved < m_slideButtons.size())
                setActiveSlide(saved);
            // Push the updated slide live
            if (!newText.trimmed().isEmpty()) {
                lyrics_renderer_paragraph_push(item.title, newText.trimmed());
                if (m_wsServer) m_wsServer->broadcastScripture(item.title, newText.trimmed());
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

        // Update library and embedded copy in service
        SongManager::instance().updateSong(m_currentSong);
        SongManager::instance().save();
        for (int i = 0; i < m_serviceItems.size(); ++i) {
            if (m_serviceItems[i].type == ServiceItem::Type::Lyrics
                    && m_serviceItems[i].id == m_currentSong.id) {
                m_serviceItems[i].sections = m_currentSong.sections;
            }
        }
        saveServiceOrder();

        int saved = m_activeSlide;
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
        this, "Export Service (Lyrics & Paragraphs)",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
            + "/service_export.json",
        "JSON Files (*.json)");
    if (path.isEmpty()) return;

    QJsonArray items = buildExportServiceJson();
    QJsonObject root;
    root["version"] = 1;
    root["items"]   = items;

    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.close();
        QMessageBox::information(this, "Export Complete",
            QString("Exported %1 item(s) to:\n%2").arg(items.size()).arg(path));
    } else {
        QMessageBox::warning(this, "Export Failed", "Could not write file:\n" + path);
    }
}

void LyricsDock::onImportService()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Import Service (Lyrics & Paragraphs)",
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

    QJsonArray rawItems = doc.object()["items"].toArray();
    QJsonArray toImport;
    for (const auto &v : rawItems) {
        if (!v.isObject()) continue;
        const QString t = v.toObject()["type"].toString();
        if (t == "song" || t == "paragraphs" || t == "paragraph" || t == "scripture")
            toImport.append(v);
    }
    if (toImport.isEmpty()) {
        QMessageBox::information(this, "Import", "No lyrics or paragraphs found in file.");
        return;
    }

    importServiceItemsFromRemote(toImport);
    QMessageBox::information(this, "Import Complete",
        QString("Imported %1 item(s).").arg(toImport.size()));
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

    // Try service.json format first (array of service items)
    if (doc.isObject() && doc.object().contains("items")) {
        // Full service.json: replace entire service order
        const auto obj = doc.object();
        m_serviceItems.clear();
        m_sequenceSongIds.clear();
        for (const auto &v : obj["items"].toArray()) {
            if (!v.isObject()) continue;
            ServiceItem item = ServiceItem::fromJson(v.toObject());
            if (item.type == ServiceItem::Type::Lyrics) {
                auto *existing = SongManager::instance().findById(item.id);
                if (!existing) {
                    // Song not in library — add it from embedded sections
                    Song s; s.id = item.id; s.title = item.title;
                    s.artist = item.artist; s.sections = item.sections;
                    SongManager::instance().addSong(s);
                } else if (item.sections.isEmpty()) {
                    // Old service file with no embedded sections — populate now
                    item.sections = existing->sections;
                    item.title    = existing->title;
                    item.artist   = existing->artist;
                }
                m_sequenceSongIds << item.id;
            }
            m_serviceItems << item;
        }
        SongManager::instance().save();
        rebuildServiceList();
        saveServiceOrder();
        broadcastSequence();
        QMessageBox::information(this, "Import Complete",
            QString("Service loaded: %1 items.").arg(m_serviceItems.size()));
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
        if (!m_sequenceSongIds.contains(s.id)) {
            ServiceItem si; si.type = ServiceItem::Type::Lyrics;
            si.id = s.id; si.title = s.title; si.artist = s.artist;
            si.sections = s.sections;  // embed full lyrics
            m_serviceItems << si; m_sequenceSongIds << s.id;
        }
    }
    mgr.save();
    rebuildServiceList();
    saveServiceOrder();
    broadcastSequence();
    QMessageBox::information(this, "Import Complete",
        QString("Songs added: %1\nSongs updated: %2").arg(added).arg(updated));
}

void LyricsDock::onBlackoutClicked()
{
    for (auto *b : m_slideButtons) b->setActive(false);
    m_activeSlide = -1;
    applyZoomToAll();
    updateSlideCounter();
    lyrics_renderer_clear();
    if (m_wsServer) m_wsServer->broadcastClear();
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

    // Save preset index to current service item so it restores on next visit.
    // m_activeColorPresetIdx is set by the connection lambda before calling here.
    if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size()
        && m_activeColorPresetIdx >= 0) {
        m_serviceItems[m_activeServiceIdx].colorPresetIdx = m_activeColorPresetIdx;
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
    if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size()) {
        const auto &item = m_serviceItems[m_activeServiceIdx];
        if (item.type == ServiceItem::Type::Paragraphs) {
            if (!m_slides.isEmpty()) {
                int next = (m_activeSlide < 0) ? 0 : m_activeSlide + 1;
                if (next < m_slides.size()) { setActiveSlide(next); pushCurrentSlide(); }
            }
            return;
        }
        if (item.type != ServiceItem::Type::Lyrics) return;
    }
    if (m_slides.isEmpty()) return;
    int next = (m_activeSlide < 0) ? 0 : m_activeSlide + 1;
    if (next >= m_slides.size()) return;
    setActiveSlide(next);
    pushCurrentSlide();
}

void LyricsDock::onPrevSlide()
{
    if (m_activeServiceIdx >= 0 && m_activeServiceIdx < m_serviceItems.size()) {
        const auto &item = m_serviceItems[m_activeServiceIdx];
        if (item.type == ServiceItem::Type::Paragraphs) {
            if (m_activeSlide > 0) { setActiveSlide(m_activeSlide - 1); pushCurrentSlide(); }
            return;
        }
        if (item.type != ServiceItem::Type::Lyrics) return;
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
    if (mode == "paragraph" || mode == "scr") {
        onNextSlide();
    } else if (mode == "ppt") {
        if (m_pptTab && m_pptTab->activeType() < 0)
            m_pptTab->requestShow(0); // auto-activate if not yet showing
        pptNext();
    } else {
        if (m_liveMode) {
            // In live mode m_slides is empty; navigate via global slide button index.
            int numButtons = m_slideButtons.size();
            if (numButtons == 0) return;
            // Current global index = song offset + local active slide index.
            int curGlobal = m_currentSongSlideOffset + (m_activeSlide >= 0 ? m_activeSlide : -1);
            int nextGlobal = curGlobal + 1;
            if (nextGlobal < 0) nextGlobal = 0;
            if (nextGlobal >= numButtons) return;
            onSlideClicked(nextGlobal);
        } else {
            if (m_slides.isEmpty()) return;
            int next = (m_activeSlide < 0) ? 0 : m_activeSlide + 1;
            if (next >= m_slides.size()) return;
            setActiveSlide(next);
            pushCurrentSlide();
        }
        if (m_wsServer) m_wsServer->broadcastActiveMode("lyr");
    }
}

void LyricsDock::lpPrevSlide(const QString &mode)
{
    if (mode == "paragraph" || mode == "scr") {
        onPrevSlide();
    } else if (mode == "ppt") {
        if (m_pptTab && m_pptTab->activeType() < 0)
            m_pptTab->requestShow(0); // auto-activate if not yet showing
        pptPrev();
    } else {
        if (m_liveMode) {
            // In live mode m_slides is empty; navigate via global slide button index.
            int curGlobal = m_currentSongSlideOffset + (m_activeSlide >= 0 ? m_activeSlide : 0);
            int prevGlobal = curGlobal - 1;
            if (prevGlobal < 0) return;
            onSlideClicked(prevGlobal);
        } else {
            if (m_slides.isEmpty() || m_activeSlide <= 0) return;
            setActiveSlide(m_activeSlide - 1);
            pushCurrentSlide();
        }
        if (m_wsServer) m_wsServer->broadcastActiveMode("lyr");
    }
}

void LyricsDock::activateNextSong()
{
    if (!m_serviceList) return;
    int row = m_serviceList->currentRow();
    if (row < m_serviceList->count() - 1)
        m_serviceList->setCurrentRow(row + 1);
}

void LyricsDock::activatePrevSong()
{
    if (!m_serviceList) return;
    int row = m_serviceList->currentRow();
    if (row > 0)
        m_serviceList->setCurrentRow(row - 1);
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

