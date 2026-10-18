// mv-tab.cpp  v2.0
//
// Video tab:  MvVideoPanel (unchanged from v1.0)
// Music tab:  MusicPicturePanel (new) + MvMusicPanel (unchanged from v1.0)

#include "mv-tab.hpp"
#include "lyrics-renderer.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStyle>
#include <QListWidgetItem>
#include <QResizeEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QImageReader>

#include <windows.h>   // CoInitializeEx

static const int kMediaStateEnded = 6;

static const QString VIDEO_FILTER =
    "Video Files (*.mp4 *.mov *.avi *.mkv *.webm *.flv *.wmv *.m4v)";
static const QString AUDIO_FILTER =
    "Audio Files (*.mp3 *.wav *.aac *.flac *.ogg *.m4a *.wma *.opus)";
static const QString IMAGE_FILTER =
    "Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tiff *.tif)";
static const QString MEDIA_FILTER =
    "Media Files (*.mp4 *.mov *.avi *.mkv *.webm *.flv *.wmv *.m4v "
    "*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tiff *.tif)";

static bool isVideoExt(const QString &path) {
    static const QStringList v = {"mp4","mov","avi","mkv","webm","flv","wmv","m4v"};
    return v.contains(QFileInfo(path).suffix().toLower());
}

// ══════════════════════════════════════════════════════════════
// MvVideoPanel  (identical to v1.0)
// ══════════════════════════════════════════════════════════════

MvVideoPanel::MvVideoPanel(QWidget *parent) : QWidget(parent)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    buildUi();
    loadState();
}

void MvVideoPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *topBar = new QWidget;
    topBar->setStyleSheet("background:#222;border-bottom:1px solid #333;");
    auto *topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(6, 4, 6, 4);

    auto *addBtn = new QPushButton("+ Add Media");
    addBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));

    m_seqBtn = new QPushButton("Sequential: Off");
    m_seqBtn->setCheckable(true);
    m_seqBtn->setChecked(false);
    m_seqBtn->setToolTip(
        "When ON: plays next video after current ends.\n"
        "When OFF: shows blank screen after video ends.");
    m_seqBtn->setStyleSheet(
        "QPushButton{background:#2a2a2a;color:#888;"
        "border:1px solid #444;border-radius:3px;padding:2px 8px;}"
        "QPushButton:checked{background:#1a3a1a;color:#6fbf6f;"
        "border:1px solid #3a8a3a;}"
        "QPushButton:hover{border-color:#888;}");

    m_clearBtn = new QPushButton(QString::fromUtf8("■ Stop"));
    m_clearBtn->setFixedSize(68, 22);
    m_clearBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;padding:0 2px;}"
        "QPushButton:hover{background:#6a2020;}");

    topL->addWidget(addBtn);
    topL->addWidget(m_seqBtn);
    topL->addStretch();
    topL->addWidget(m_clearBtn);
    root->addWidget(topBar);

    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea{background:#181818;border:none;}");
    m_container = new QWidget;
    m_container->setStyleSheet("background:#181818;");
    m_container->setAcceptDrops(false);
    m_container->installEventFilter(this);
    m_grid = new QGridLayout(m_container);
    m_grid->setContentsMargins(6, 6, 6, 6);
    m_grid->setSpacing(6);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_scrollArea->setWidget(m_container);
    root->addWidget(m_scrollArea, 1);

    m_videoControls = new QWidget;
    m_videoControls->setStyleSheet("background:#222;border-top:1px solid #333;");
    auto *vcL = new QVBoxLayout(m_videoControls);
    vcL->setContentsMargins(6, 4, 6, 4);
    vcL->setSpacing(4);

    auto *scrubRow = new QHBoxLayout;
    m_playPauseBtn = new QToolButton;
    m_playPauseBtn->setText(QString::fromUtf8("⏸"));
    m_playPauseBtn->setFixedSize(26, 22);

    m_scrubber = new QSlider(Qt::Horizontal);
    m_scrubber->setRange(0, 1000);
    m_scrubber->setStyleSheet(
        "QSlider::groove:horizontal{background:#333;height:4px;border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:#4da6ff;border-radius:2px;}"
        "QSlider::handle:horizontal{background:#4da6ff;width:12px;height:12px;"
        "margin:-4px 0;border-radius:6px;}"
        "QSlider::handle:horizontal:hover{background:#7dc4ff;}");

    m_timeLbl = new QLabel("0:00 / 0:00");
    m_timeLbl->setStyleSheet("color:#888;font-size:9px;");
    m_timeLbl->setFixedWidth(76);

    m_loopBtn = new QPushButton(QString::fromUtf8("↺"));
    m_loopBtn->setFixedSize(26, 22);
    m_loopBtn->setToolTip("Loop: Off — click to enable loop");
    m_loopBtn->setCheckable(true);
    m_loopBtn->setStyleSheet(
        "QPushButton{background:#2a2a2a;color:#666;border:1px solid #444;"
        "border-radius:3px;font-size:13px;font-weight:bold;}"
        "QPushButton:checked{background:#1a3a1a;color:#3dd68c;border:1px solid #2a8a2a;}"
        "QPushButton:hover{border-color:#888;}");
    connect(m_loopBtn, &QPushButton::toggled, this, [this](bool on) {
        m_loopOne = on;
        m_loopBtn->setToolTip(on ? "Loop: On — click to disable" : "Loop: Off — click to enable loop");
    });

    scrubRow->addWidget(m_playPauseBtn);
    scrubRow->addWidget(m_scrubber, 1);
    scrubRow->addWidget(m_timeLbl);
    scrubRow->addWidget(m_loopBtn);
    vcL->addLayout(scrubRow);
    m_videoControls->setVisible(false);
    root->addWidget(m_videoControls);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout, this, &MvVideoPanel::onPollTimer);
    m_pollTimer->start();

    connect(addBtn,       &QPushButton::clicked,  this, &MvVideoPanel::onAddVideo);
    connect(m_clearBtn,   &QPushButton::clicked,  this, &MvVideoPanel::onClearVideo);
    connect(m_seqBtn,     &QPushButton::toggled,  this, &MvVideoPanel::onSequentialToggle);
    connect(m_playPauseBtn, &QToolButton::clicked,this, &MvVideoPanel::onPlayPause);
    connect(m_scrubber, &QSlider::sliderMoved, this, [this](int v) {
        int64_t t = 0, d = 0;
        if (!lyrics_renderer_mv_get_time(t, d) || d <= 0) return;
        int64_t seekMs = (int64_t)((double)v / 1000.0 * (double)d);
        lyrics_renderer_mv_set_time(seekMs);
        m_seekCooldown = 5;
        auto fmt = [](int64_t ms) -> QString {
            int s = (int)(ms / 1000);
            return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
        };
        m_timeLbl->setText(fmt(seekMs) + " / " + fmt(d));
    });
}

bool MvVideoPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_container && event->type() == QEvent::Resize) {
        int nc = calcCols();
        if (nc != m_currentCols) { m_currentCols = nc; rebuildGrid(); }
    }
    return QWidget::eventFilter(obj, event);
}

int MvVideoPanel::calcCols() const
{
    int available = m_container->width() - 12;
    int colWidth  = MediaCard::CARD_W + 6;
    return qMax(1, available / colWidth);
}

void MvVideoPanel::rebuildGrid()
{
    for (auto *c : m_cards) { m_grid->removeWidget(c); c->deleteLater(); }
    m_cards.clear();
    int col = 0, row = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        auto *card = new MediaCard(m_items[i], i, m_container);
        card->setAcceptDrops(false);
        connect(card, &MediaCard::clicked,         this, &MvVideoPanel::onCardClicked);
        connect(card, &MediaCard::removeRequested, this, &MvVideoPanel::onCardRemoveRequested);
        card->setActive(i == m_activeIndex);
        m_grid->addWidget(card, row, col);
        m_cards << card;
        if (++col >= m_currentCols) { col = 0; ++row; }
    }
}

void MvVideoPanel::setActiveCard(int index)
{
    m_activeIndex = index;
    for (auto *c : m_cards) c->setActive(c->index() == index);
}

void MvVideoPanel::updateVideoControls()
{
    bool show = (m_activeIndex >= 0 &&
                 m_activeIndex < m_items.size() &&
                 m_items[m_activeIndex].isVideo);
    m_videoControls->setVisible(show);
    if (show) { m_paused = false; m_playPauseBtn->setText(QString::fromUtf8("⏸")); }
}

void MvVideoPanel::playVideoAt(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    setActiveCard(index);
    m_endHandled = false;
    m_paused     = false;
    updateVideoControls();
    lyrics_renderer_mv_video_push(m_items[index].path, m_fadeDurationMs);
    emit videoStarted();
}

void MvVideoPanel::onAddVideo()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Add Media", QString(), MEDIA_FILTER);
    if (files.isEmpty()) return;
    for (const auto &f : files) {
        MediaItem item; item.path = f; item.isVideo = isVideoExt(f);
        m_items << item;
    }
    rebuildGrid();
    saveState();
    emit mediaListChanged(mediaList());
}

void MvVideoPanel::onCardClicked(int index) { playVideoAt(index); saveState(); }

void MvVideoPanel::onCardRemoveRequested(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    if (m_activeIndex == index) { stopPlayback(); lyrics_renderer_mv_clear(); }
    else if (m_activeIndex > index) m_activeIndex--;
    m_items.removeAt(index);
    rebuildGrid();
    saveState();
    emit mediaListChanged(mediaList());
}

void MvVideoPanel::onClearVideo()
{
    setActiveCard(-1);
    m_videoControls->setVisible(false);
    lyrics_renderer_mv_clear(m_fadeDurationMs);
}

void MvVideoPanel::onSequentialToggle(bool checked)
{
    m_sequential = checked;
    m_seqBtn->setText(checked ? "Sequential: On" : "Sequential: Off");
}

void MvVideoPanel::onPlayPause()
{
    m_paused = !m_paused;
    lyrics_renderer_mv_play_pause(m_paused);
    m_playPauseBtn->setText(m_paused ? QString::fromUtf8("▶")
                                     : QString::fromUtf8("⏸"));
}

void MvVideoPanel::onPollTimer()
{
    if (m_videoControls->isVisible() && !m_scrubber->isSliderDown()) {
        int64_t t = 0, d = 0;
        if (lyrics_renderer_mv_get_time(t, d) && d > 0) {
            m_scrubber->setValue((int)((double)t / (double)d * 1000.0));
            auto fmt = [](int64_t ms) -> QString {
                int s = (int)(ms / 1000);
                return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
            };
            m_timeLbl->setText(fmt(t) + " / " + fmt(d));
        }
    }
    if (m_activeIndex < 0 || m_endHandled) return;
    if (m_seekCooldown > 0) { --m_seekCooldown; return; }
    if (lyrics_renderer_mv_get_state() != kMediaStateEnded) return;
    m_endHandled = true;
    if (m_loopOne) {
        playVideoAt(m_activeIndex);
    } else if (m_sequential) {
        int next = m_activeIndex + 1;
        if (next < m_items.size()) playVideoAt(next);
        else { setActiveCard(-1); m_videoControls->setVisible(false); lyrics_renderer_mv_clear(); }
    } else {
        setActiveCard(-1); m_videoControls->setVisible(false); lyrics_renderer_mv_clear();
    }
}

void MvVideoPanel::stopPlayback()
{
    setActiveCard(-1);
    m_videoControls->setVisible(false);
    m_paused     = false;
    m_endHandled = true;
}

void MvVideoPanel::setMediaList(const QStringList &paths)
{
    if (m_activeIndex >= 0) { stopPlayback(); lyrics_renderer_mv_clear(); }
    m_items.clear();
    for (const auto &p : paths) {
        if (!QFile::exists(p)) continue;
        MediaItem item; item.path = p; item.isVideo = isVideoExt(p);
        m_items << item;
    }
    rebuildGrid();
}

QStringList MvVideoPanel::mediaList() const
{
    QStringList out;
    for (const auto &it : m_items) out << it.path;
    return out;
}

void MvVideoPanel::setFadeDurationMs(int ms) { m_fadeDurationMs = ms; }
void MvVideoPanel::setNumCols(int n) { if (n < 1 || n > 8) return; m_currentCols = n; rebuildGrid(); }

QString MvVideoPanel::statePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/mv-video.json";
}

void MvVideoPanel::saveState()
{
    QJsonArray arr;
    for (const auto &item : m_items) arr.append(item.path);
    QJsonObject obj;
    obj["items"]      = arr;
    obj["sequential"] = m_sequential;
    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(obj).toJson());
}

void MvVideoPanel::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    QJsonObject obj = doc.object();
    m_items.clear();
    for (const auto &v : obj["items"].toArray()) {
        QString path = v.toString();
        if (!QFile::exists(path)) continue;
        MediaItem item; item.path = path; item.isVideo = isVideoExt(path);
        m_items << item;
    }
    m_seqBtn->setChecked(obj["sequential"].toBool(false));
    rebuildGrid();
}

// ══════════════════════════════════════════════════════════════
// MvMusicPanel  (identical to v1.0)
// ══════════════════════════════════════════════════════════════

MvMusicPanel::MvMusicPanel(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar: Add / engine label / reorder / loop / remove / stop ──────────
    auto *topBar = new QWidget;
    topBar->setStyleSheet("background:#222;border-bottom:1px solid #333;");
    auto *topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(6, 4, 6, 4);
    topL->setSpacing(4);

    auto *addBtn = new QPushButton("+ Add");
    addBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    addBtn->setToolTip("Add music files");

    bool vlc = lyrics_renderer_vlc_available();
    auto *engineLbl = new QLabel(vlc ? "VLC" : "FFmpeg");
    engineLbl->setToolTip(vlc
        ? "Using VLC source — accurate MP3/OGG/FLAC seeking"
        : "VLC plugin not found — using FFmpeg (MP3 seeking unreliable).\n"
          "Install OBS VLC plugin for better music seeking.");
    engineLbl->setStyleSheet(vlc
        ? "color:#5dbb5d;font-size:9px;font-weight:bold;"
          "border:1px solid #3a8a3a;border-radius:3px;padding:1px 4px;background:#1a3a1a;"
        : "color:#ffaa44;font-size:9px;font-weight:bold;"
          "border:1px solid #885522;border-radius:3px;padding:1px 4px;background:#2a1a0a;");

    // Up / Down reorder buttons
    m_upBtn = new QToolButton;
    m_upBtn->setText(QString::fromUtf8("▲"));
    m_upBtn->setFixedSize(24, 24);
    m_upBtn->setToolTip("Move selected track up");
    m_upBtn->setEnabled(false);

    m_downBtn = new QToolButton;
    m_downBtn->setText(QString::fromUtf8("▼"));
    m_downBtn->setFixedSize(24, 24);
    m_downBtn->setToolTip("Move selected track down");
    m_downBtn->setEnabled(false);

    // Loop button — cycles: Off → All → One → Off
    m_loopBtn = new QPushButton("Loop: Off");
    m_loopBtn->setFixedHeight(24);
    m_loopBtn->setToolTip("Loop mode: Off → Loop All → Loop One → Off");
    applyLoopButtonStyle();

    m_removeBtn = new QPushButton;
    m_removeBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_removeBtn->setToolTip("Remove selected track");
    m_removeBtn->setFixedSize(28, 24);
    m_removeBtn->setEnabled(false);

    m_stopBtn = new QPushButton(QString::fromUtf8("■"));
    m_stopBtn->setFixedSize(28, 24);
    m_stopBtn->setToolTip("Stop music");
    m_stopBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;}"
        "QPushButton:hover{background:#6a2020;}");

    topL->addWidget(addBtn);
    topL->addWidget(engineLbl);
    topL->addStretch();
    topL->addWidget(m_upBtn);
    topL->addWidget(m_downBtn);
    topL->addWidget(m_loopBtn);
    topL->addWidget(m_removeBtn);
    topL->addWidget(m_stopBtn);
    root->addWidget(topBar);

    // ── Track list ────────────────────────────────────────────────────────────
    m_list = new QListWidget;
    m_list->setStyleSheet(
        "QListWidget{background:#181818;border:none;}"
        "QListWidget::item{padding:4px 6px;color:#ccc;}"
        "QListWidget::item:selected{background:#1e6bb8;color:#fff;}"
        "QListWidget::item:hover{background:#252525;}");
    // Drag-drop disabled in favour of explicit ▲/▼ buttons
    m_list->setDragEnabled(false);
    m_list->setAcceptDrops(false);
    m_list->setDragDropMode(QAbstractItemView::NoDragDrop);
    root->addWidget(m_list, 1);

    // ── Seek / play-pause bar ─────────────────────────────────────────────────
    m_playPauseBtn = new QToolButton;
    m_playPauseBtn->setText(QString::fromUtf8("▶"));
    m_playPauseBtn->setFixedSize(28, 28);
    m_playPauseBtn->setToolTip("Play / Pause");

    m_seekBar = new QWidget;
    m_seekBar->setStyleSheet("background:#222;border-top:1px solid #333;");
    auto *seekL = new QHBoxLayout(m_seekBar);
    seekL->setContentsMargins(6, 3, 6, 3);
    seekL->setSpacing(6);

    m_musicScrubber = new QSlider(Qt::Horizontal);
    m_musicScrubber->setRange(0, 1000);
    m_musicScrubber->setValue(0);
    m_musicScrubber->setEnabled(false);
    m_musicScrubber->setStyleSheet(
        "QSlider::groove:horizontal{background:#333;height:4px;border-radius:2px;}"
        "QSlider::sub-page:horizontal{background:#4da6ff;border-radius:2px;}"
        "QSlider::handle:horizontal{background:#4da6ff;width:12px;height:12px;"
        "margin:-4px 0;border-radius:6px;}"
        "QSlider::handle:horizontal:hover{background:#7dc4ff;}"
        "QSlider::groove:horizontal:disabled{background:#222;}"
        "QSlider::handle:horizontal:disabled{background:#555;}");

    m_musicTimeLbl = new QLabel("0:00 / 0:00");
    m_musicTimeLbl->setStyleSheet("color:#888;font-size:9px;");
    m_musicTimeLbl->setFixedWidth(90);
    m_musicTimeLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    seekL->addWidget(m_playPauseBtn);
    seekL->addWidget(m_musicScrubber, 1);
    seekL->addWidget(m_musicTimeLbl);
    root->addWidget(m_seekBar);

    // ── Now-playing label ─────────────────────────────────────────────────────
    auto *npBar = new QWidget;
    npBar->setStyleSheet("background:#1a1a1a;border-top:1px solid #333;");
    auto *npL = new QHBoxLayout(npBar);
    npL->setContentsMargins(8, 4, 8, 4);
    auto *npIcon = new QLabel(QString::fromUtf8("♪"));
    npIcon->setStyleSheet("color:#4da6ff;font-size:12px;");
    m_nowPlayingLbl = new QLabel;
    m_nowPlayingLbl->setStyleSheet("color:#aaa;font-size:9px;");
    m_nowPlayingLbl->setWordWrap(false);
    npL->addWidget(npIcon);
    npL->addWidget(m_nowPlayingLbl, 1);
    root->addWidget(npBar);

    // ── Poll timer ────────────────────────────────────────────────────────────
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout, this, &MvMusicPanel::onPollTimer);
    m_pollTimer->start();

    // ── Connections ───────────────────────────────────────────────────────────
    connect(addBtn,         &QPushButton::clicked,       this, &MvMusicPanel::onAddMusic);
    connect(m_playPauseBtn, &QToolButton::clicked,       this, &MvMusicPanel::onPlayPause);
    connect(m_removeBtn,    &QPushButton::clicked,       this, &MvMusicPanel::onRemoveMusic);
    connect(m_stopBtn,      &QPushButton::clicked,       this, &MvMusicPanel::onStopMusic);
    connect(m_upBtn,        &QToolButton::clicked,       this, &MvMusicPanel::onMoveUp);
    connect(m_downBtn,      &QToolButton::clicked,       this, &MvMusicPanel::onMoveDown);
    connect(m_loopBtn,      &QPushButton::clicked,       this, &MvMusicPanel::onLoopToggle);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &MvMusicPanel::onItemDoubleClicked);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int){ updateButtons(); });

    // Scrubber: seek using music-specific API (works even when picture is showing)
    connect(m_musicScrubber, &QSlider::sliderMoved, this, [this](int v) {
        if (m_trackDuration <= 0) {
            int64_t t = 0, d = 0;
            if (!lyrics_renderer_music_get_time(t, d) || d <= 0) return;
            m_trackDuration = d;
        }
        int64_t seekMs = (int64_t)((double)v / 1000.0 * (double)m_trackDuration);
        lyrics_renderer_music_set_time(seekMs);
        m_seekCooldown = 5;
        auto fmt = [](int64_t ms) -> QString {
            int s = (int)(ms / 1000);
            return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
        };
        m_musicTimeLbl->setText(fmt(seekMs) + " / " + fmt(m_trackDuration));
    });

    loadState();
}

void MvMusicPanel::updateButtons()
{
    int row = m_list->currentRow();
    bool has = (row >= 0);
    m_removeBtn->setEnabled(has);
    m_upBtn->setEnabled(has && row > 0);
    m_downBtn->setEnabled(has && row < m_items.size() - 1);
}

void MvMusicPanel::applyLoopButtonStyle()
{
    switch (m_loopMode) {
    case LoopMode::None:
        m_loopBtn->setText("Loop: Off");
        m_loopBtn->setStyleSheet(
            "QPushButton{background:#2a2a2a;color:#666;"
            "border:1px solid #444;border-radius:3px;padding:0 6px;}"
            "QPushButton:hover{border-color:#888;color:#aaa;}");
        break;
    case LoopMode::All:
        m_loopBtn->setText(QString::fromUtf8("⟳ Loop All"));
        m_loopBtn->setStyleSheet(
            "QPushButton{background:#1a2a3a;color:#4da6ff;"
            "border:1px solid #2a6ab8;border-radius:3px;padding:0 6px;}"
            "QPushButton:hover{background:#1e3a5a;}");
        break;
    case LoopMode::One:
        m_loopBtn->setText(QString::fromUtf8("⟳ Loop 1"));
        m_loopBtn->setStyleSheet(
            "QPushButton{background:#2a1a3a;color:#cc88ff;"
            "border:1px solid #7a3ab8;border-radius:3px;padding:0 6px;}"
            "QPushButton:hover{background:#3a2a4a;}");
        break;
    }
}

void MvMusicPanel::onMoveUp()
{
    int row = m_list->currentRow();
    if (row <= 0 || row >= m_items.size()) return;
    // Track active index across the swap
    if      (m_activeIndex == row)     m_activeIndex = row - 1;
    else if (m_activeIndex == row - 1) m_activeIndex = row;
    m_items.swapItemsAt(row, row - 1);
    auto *item = m_list->takeItem(row);
    m_list->insertItem(row - 1, item);
    m_list->setCurrentRow(row - 1);
    saveState();
}

void MvMusicPanel::onMoveDown()
{
    int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size() - 1) return;
    if      (m_activeIndex == row)     m_activeIndex = row + 1;
    else if (m_activeIndex == row + 1) m_activeIndex = row;
    m_items.swapItemsAt(row, row + 1);
    auto *item = m_list->takeItem(row);
    m_list->insertItem(row + 1, item);
    m_list->setCurrentRow(row + 1);
    saveState();
}

void MvMusicPanel::onLoopToggle()
{
    // Cycle: None → All → One → None
    switch (m_loopMode) {
    case LoopMode::None: m_loopMode = LoopMode::All; break;
    case LoopMode::All:  m_loopMode = LoopMode::One; break;
    case LoopMode::One:  m_loopMode = LoopMode::None; break;
    }
    applyLoopButtonStyle();
    saveState();
}

void MvMusicPanel::setActiveRow(int index)
{
    m_activeIndex = index;
    {
        QSignalBlocker blk(m_list);
        if (index >= 0) m_list->setCurrentRow(index);
        else            m_list->clearSelection();
    }
    if (index >= 0 && index < m_items.size()) {
        QString name = QFileInfo(m_items[index]).fileName();
        QFontMetrics fm(m_nowPlayingLbl->font());
        m_nowPlayingLbl->setText(
            fm.elidedText(name, Qt::ElideMiddle,
                          m_nowPlayingLbl->width() > 0 ? m_nowPlayingLbl->width() : 200));
    } else {
        m_nowPlayingLbl->clear();
    }
}

void MvMusicPanel::playMusicAt(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    setActiveRow(index);
    m_endHandled    = false;
    m_musicPaused   = false;
    m_seekCooldown  = 0;
    m_trackDuration = 0;
    m_musicScrubber->setValue(0);
    m_musicScrubber->setEnabled(true);
    m_musicTimeLbl->setText("0:00 / 0:00");
    m_playPauseBtn->setText(QString::fromUtf8("⏸"));
    lyrics_renderer_mv_music_push(m_items[index]);
    emit musicStarted();
}

void MvMusicPanel::onAddMusic()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Add Music", QString(), AUDIO_FILTER);
    if (files.isEmpty()) return;
    for (const auto &f : files) {
        if (QFile::exists(f)) {
            m_items << f;
            auto *it = new QListWidgetItem(QFileInfo(f).fileName());
            it->setData(Qt::UserRole, f);
            m_list->addItem(it);
        }
    }
    saveState();
}

void MvMusicPanel::onItemDoubleClicked(QListWidgetItem *item)
{
    int row = m_list->row(item);
    if (row >= 0) playMusicAt(row);
}

void MvMusicPanel::onRemoveMusic()
{
    int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size()) return;
    if (m_activeIndex == row) { stopPlayback(); lyrics_renderer_mv_music_clear(); }
    else if (m_activeIndex > row) m_activeIndex--;
    m_items.removeAt(row);
    delete m_list->takeItem(row);
    updateButtons();
    saveState();
}

void MvMusicPanel::onStopMusic()
{
    setActiveRow(-1);
    m_musicPaused   = false;
    m_seekCooldown  = 0;
    m_trackDuration = 0;
    m_playPauseBtn->setText(QString::fromUtf8("▶"));
    m_musicScrubber->setValue(0);
    m_musicScrubber->setEnabled(false);
    m_musicTimeLbl->setText("0:00 / 0:00");
    lyrics_renderer_mv_music_clear();
    emit stopped();
}

void MvMusicPanel::onPlayPause()
{
    if (m_activeIndex < 0) {
        int row = m_list->currentRow();
        if (row >= 0) playMusicAt(row);
        return;
    }
    m_musicPaused = !m_musicPaused;
    lyrics_renderer_music_play_pause(m_musicPaused);
    m_playPauseBtn->setText(m_musicPaused ? QString::fromUtf8("▶")
                                          : QString::fromUtf8("⏸"));
}

void MvMusicPanel::onPollTimer()
{
    // ── Update seek bar (Bug 1 fix: use music-specific API, not mv_get_time) ──
    if (m_activeIndex >= 0) {
        int64_t t = 0, d = 0;
        if (lyrics_renderer_music_get_time(t, d) && d > 0) {
            if (m_trackDuration <= 0) m_trackDuration = d;
            if (!m_musicScrubber->isSliderDown()) {
                m_musicScrubber->setValue(
                    (int)((double)t / (double)m_trackDuration * 1000.0));
                auto fmt = [](int64_t ms) -> QString {
                    int s = (int)(ms / 1000);
                    return QString("%1:%2").arg(s/60).arg(s%60,2,10,QChar('0'));
                };
                m_musicTimeLbl->setText(fmt(t) + " / " + fmt(m_trackDuration));
            }
        }
    }

    // ── End-of-track detection (Bug 3 fix: use music-specific API + loop logic) ─
    if (m_activeIndex < 0 || m_endHandled) return;
    if (m_seekCooldown > 0) { --m_seekCooldown; return; }
    if (lyrics_renderer_music_get_state() != kMediaStateEnded) return;

    m_endHandled = true;

    if (m_loopMode == LoopMode::One) {
        // Replay the same track
        playMusicAt(m_activeIndex);
        return;
    }

    int next = m_activeIndex + 1;
    if (next < m_items.size()) {
        playMusicAt(next);
    } else if (m_loopMode == LoopMode::All) {
        playMusicAt(0);  // wrap to first track
    } else {
        // No loop — stop
        setActiveRow(-1);
        m_musicScrubber->setValue(0);
        m_musicScrubber->setEnabled(false);
        m_musicTimeLbl->setText("0:00 / 0:00");
        lyrics_renderer_mv_music_clear();
        emit stopped();
    }
}

void MvMusicPanel::stopPlayback()
{
    setActiveRow(-1);
    m_endHandled    = true;
    m_musicPaused   = false;
    m_seekCooldown  = 0;
    m_trackDuration = 0;
    m_playPauseBtn->setText(QString::fromUtf8("▶"));
    m_musicScrubber->setValue(0);
    m_musicScrubber->setEnabled(false);
    m_musicTimeLbl->setText("0:00 / 0:00");
}

QString MvMusicPanel::statePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/mv-music.json";
}

void MvMusicPanel::saveState()
{
    QJsonArray arr;
    for (const auto &path : m_items) arr.append(path);
    QJsonObject obj;
    obj["items"]    = arr;
    obj["loopMode"] = (int)m_loopMode;
    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(obj).toJson());
}

void MvMusicPanel::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    auto obj = doc.object();
    m_items.clear();
    m_list->clear();
    for (const auto &v : obj["items"].toArray()) {
        QString path = v.toString();
        if (!QFile::exists(path)) continue;
        m_items << path;
        auto *it = new QListWidgetItem(QFileInfo(path).fileName());
        it->setData(Qt::UserRole, path);
        m_list->addItem(it);
    }
    int lm = obj["loopMode"].toInt(0);
    m_loopMode = (lm == 1) ? LoopMode::All : (lm == 2) ? LoopMode::One : LoopMode::None;
    applyLoopButtonStyle();
}

// ══════════════════════════════════════════════════════════════
// PictureCard
// ══════════════════════════════════════════════════════════════

PictureCard::PictureCard(const QString &path, int index, QWidget *parent)
    : QFrame(parent), m_path(path), m_index(index)
{
    setFixedSize(CARD_W, CARD_H);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(
        "QFrame{background:#252525;border:1px solid #3a3a3a;border-radius:3px;}"
        "QFrame:hover{border-color:#666;}");

    auto *vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(2);

    // ── Checkbox row ──────────────────────────────────────────
    m_check = new QCheckBox(this);
    m_check->setStyleSheet("QCheckBox{color:#aaa;font-size:8px;}");
    m_check->setToolTip("Include in slideshow");
    vl->addWidget(m_check);

    // ── Thumbnail ─────────────────────────────────────────────
    m_thumb = new QLabel(this);
    m_thumb->setFixedSize(THUMB_W, THUMB_H);
    m_thumb->setAlignment(Qt::AlignCenter);
    m_thumb->setStyleSheet("background:#1a1a1a;border-radius:2px;");
    vl->addWidget(m_thumb);

    // ── Filename ──────────────────────────────────────────────
    m_nameLbl = new QLabel(this);
    m_nameLbl->setStyleSheet("color:#aaa;font-size:8px;");
    m_nameLbl->setAlignment(Qt::AlignCenter);
    vl->addWidget(m_nameLbl);

    buildThumbnail();

    connect(m_check, &QCheckBox::toggled, this, [this](bool checked){
        emit checkToggled(m_index, checked);
    });
}

void PictureCard::buildThumbnail()
{
    QImageReader reader(m_path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (!img.isNull()) {
        QPixmap px = QPixmap::fromImage(
            img.scaled(THUMB_W, THUMB_H, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_thumb->setPixmap(px);
    } else {
        m_thumb->setText("?");
        m_thumb->setStyleSheet("background:#1a1a1a;border-radius:2px;color:#666;font-size:20px;");
    }
    QString name = QFileInfo(m_path).fileName();
    QFontMetrics fm(m_nameLbl->font());
    m_nameLbl->setText(fm.elidedText(name, Qt::ElideMiddle, THUMB_W));
}

void PictureCard::setActive(bool active)
{
    m_active = active;
    setStyleSheet(active
        ? "QFrame{background:#1a2a3a;border:2px solid #4da6ff;border-radius:3px;}"
        : "QFrame{background:#252525;border:1px solid #3a3a3a;border-radius:3px;}"
          "QFrame:hover{border-color:#666;}");
}

bool PictureCard::isChecked() const { return m_check && m_check->isChecked(); }

void PictureCard::setChecked(bool checked) { if (m_check) m_check->setChecked(checked); }

void PictureCard::mousePressEvent(QMouseEvent *e)
{
    // If the click is on the checkbox, let it handle its own toggle
    if (m_check && m_check->geometry().contains(e->pos())) {
        QFrame::mousePressEvent(e);
        return;
    }
    if (e->button() == Qt::LeftButton) {
        emit clicked(m_index);
    } else if (e->button() == Qt::RightButton) {
        QMenu menu(this);
        auto *removeAct = menu.addAction(
            style()->standardIcon(QStyle::SP_TrashIcon), "Remove");
        if (menu.exec(mapToGlobal(e->pos())) == removeAct)
            emit removeRequested(m_index);
    }
    QFrame::mousePressEvent(e);
}

// ══════════════════════════════════════════════════════════════
// MusicPicturePanel
// ══════════════════════════════════════════════════════════════

MusicPicturePanel::MusicPicturePanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    loadState();
}

void MusicPicturePanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar ───────────────────────────────────────────────
    auto *topBar = new QWidget;
    topBar->setStyleSheet("background:#222;border-bottom:1px solid #333;");
    auto *topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(6, 4, 6, 4);

    auto *addBtn = new QPushButton("+ Add Pictures");
    addBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));

    m_slideshowBtn = new QPushButton(QString::fromUtf8("▶ Slideshow"));
    m_slideshowBtn->setCheckable(true);
    m_slideshowBtn->setEnabled(false); // enabled only when ≥2 boxes checked
    m_slideshowBtn->setToolTip("Start / stop the picture slideshow (requires 2+ checked)");
    m_slideshowBtn->setStyleSheet(
        "QPushButton{background:#2a2a2a;color:#888;"
        "border:1px solid #444;border-radius:3px;padding:2px 8px;}"
        "QPushButton:checked{background:#1a3a1a;color:#6fbf6f;"
        "border:1px solid #3a8a3a;}"
        "QPushButton:disabled{color:#444;border-color:#333;}"
        "QPushButton:hover:!disabled{border-color:#888;}");

    m_blankBtn = new QPushButton(QString::fromUtf8("■ Blank"));
    m_blankBtn->setFixedSize(68, 22);
    m_blankBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;padding:0 2px;}"
        "QPushButton:hover{background:#6a2020;}");

    topL->addWidget(addBtn);
    topL->addWidget(m_slideshowBtn);
    topL->addStretch();
    topL->addWidget(m_blankBtn);
    root->addWidget(topBar);

    // ── Thumbnail grid ────────────────────────────────────────
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea{background:#181818;border:none;}");
    m_container = new QWidget;
    m_container->setStyleSheet("background:#181818;");
    m_container->installEventFilter(this);
    m_grid = new QGridLayout(m_container);
    m_grid->setContentsMargins(6, 6, 6, 6);
    m_grid->setSpacing(6);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_scrollArea->setWidget(m_container);
    root->addWidget(m_scrollArea, 1);

    // ── Bottom bar: slideshow interval ────────────────────────
    auto *bottomBar = new QWidget;
    bottomBar->setStyleSheet("background:#1e1e1e;border-top:1px solid #333;");
    auto *bottomL = new QHBoxLayout(bottomBar);
    bottomL->setContentsMargins(8, 4, 8, 4);
    auto *intervalLbl = new QLabel("Slideshow Interval:");
    intervalLbl->setStyleSheet("color:#aaa;font-size:9px;");
    m_intervalSpin = new QSpinBox;
    m_intervalSpin->setRange(1, 3600);
    m_intervalSpin->setValue(10);
    m_intervalSpin->setSuffix(" s");
    m_intervalSpin->setFixedWidth(72);
    m_intervalSpin->setToolTip("Time between pictures in slideshow mode (when 2+ are checked)");
    auto *intervalNote = new QLabel("(check 2+ pictures to start)");
    intervalNote->setStyleSheet("color:#555;font-size:8px;");
    bottomL->addWidget(intervalLbl);
    bottomL->addWidget(m_intervalSpin);
    bottomL->addWidget(intervalNote);
    bottomL->addStretch();
    root->addWidget(bottomBar);

    // ── Slideshow timer ───────────────────────────────────────
    m_slideshowTimer = new QTimer(this);
    m_slideshowTimer->setSingleShot(false);
    connect(m_slideshowTimer, &QTimer::timeout, this, &MusicPicturePanel::onSlideshowTimer);

    // Update timer interval when spinbox changes (only if timer is running)
    connect(m_intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){
        if (m_slideshowTimer->isActive()) {
            m_slideshowTimer->setInterval(v * 1000);
        }
        saveState();
    });

    connect(addBtn,         &QPushButton::clicked,  this, &MusicPicturePanel::onAddPictures);
    connect(m_blankBtn,     &QPushButton::clicked,  this, &MusicPicturePanel::onBlank);
    connect(m_slideshowBtn, &QPushButton::toggled,  this, [this](bool on){
        if (on) {
            // Start slideshow from the first checked picture
            QList<int> checked;
            for (int i = 0; i < m_cards.size(); ++i)
                if (m_cards[i]->isChecked()) checked << i;
            if (checked.size() < 2) {
                // Shouldn't happen (button disabled), but guard anyway
                QSignalBlocker blk(m_slideshowBtn);
                m_slideshowBtn->setChecked(false);
                return;
            }
            m_slideshowPos = 0;
            pushPicture(checked[0]);
            m_slideshowTimer->start(m_intervalSpin->value() * 1000);
            m_slideshowBtn->setText(QString::fromUtf8("■ Stop Slideshow"));
        } else {
            m_slideshowTimer->stop();
            m_slideshowBtn->setText(QString::fromUtf8("▶ Slideshow"));
        }
    });
}

bool MusicPicturePanel::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_container && event->type() == QEvent::Resize) {
        int nc = calcCols();
        if (nc != m_currentCols) { m_currentCols = nc; rebuildGrid(); }
    }
    return QWidget::eventFilter(obj, event);
}

int MusicPicturePanel::calcCols() const
{
    int available = m_container->width() - 12;
    int colWidth  = PictureCard::CARD_W + 6;
    return qMax(1, available / colWidth);
}

void MusicPicturePanel::rebuildGrid()
{
    // Save checked state before rebuilding
    QList<bool> checkedState;
    for (auto *c : m_cards) checkedState << c->isChecked();

    for (auto *c : m_cards) { m_grid->removeWidget(c); c->deleteLater(); }
    m_cards.clear();
    int col = 0, row = 0;
    for (int i = 0; i < m_paths.size(); ++i) {
        auto *card = new PictureCard(m_paths[i], i, m_container);
        if (i < checkedState.size()) card->setChecked(checkedState[i]);
        connect(card, &PictureCard::clicked,        this, &MusicPicturePanel::onCardClicked);
        connect(card, &PictureCard::checkToggled,   this, &MusicPicturePanel::onCheckToggled);
        connect(card, &PictureCard::removeRequested,this, &MusicPicturePanel::onCardRemoveRequested);
        card->setActive(i == m_activeIndex);
        m_grid->addWidget(card, row, col);
        m_cards << card;
        if (++col >= m_currentCols) { col = 0; ++row; }
    }
    updateSlideshowState(); // refresh button enabled state
}

void MusicPicturePanel::setActiveCard(int index)
{
    m_activeIndex = index;
    for (auto *c : m_cards) c->setActive(c->index() == index);
}

void MusicPicturePanel::pushPicture(int index)
{
    if (index < 0 || index >= m_paths.size()) return;
    setActiveCard(index);
    lyrics_renderer_media_push(m_paths[index], 500);
    emit pictureActivated();
}

void MusicPicturePanel::updateSlideshowState()
{
    // Count checked cards and update button enabled state.
    // The slideshow is NOT auto-started — user must press the button explicitly.
    int checkedCount = 0;
    for (auto *c : m_cards)
        if (c->isChecked()) ++checkedCount;

    bool canSlideshow = (checkedCount >= 2);
    m_slideshowBtn->setEnabled(canSlideshow);

    // If slideshow was running but now < 2 checked, stop it
    if (!canSlideshow && m_slideshowTimer->isActive()) {
        m_slideshowTimer->stop();
        QSignalBlocker blk(m_slideshowBtn);
        m_slideshowBtn->setChecked(false);
        m_slideshowBtn->setText(QString::fromUtf8("▶ Slideshow"));
    }
}

void MusicPicturePanel::onAddPictures()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Pictures", QString(), IMAGE_FILTER);
    if (files.isEmpty()) return;
    for (const auto &f : files) {
        if (QFile::exists(f)) m_paths << f;
    }
    rebuildGrid();
    saveState();
}

void MusicPicturePanel::onCardClicked(int index)
{
    // Immediately show this picture; stop any running slideshow
    m_slideshowTimer->stop();
    pushPicture(index);
}

void MusicPicturePanel::onCheckToggled(int /*index*/, bool /*checked*/)
{
    updateSlideshowState(); // updates button enabled state only
    saveState();
}

void MusicPicturePanel::onCardRemoveRequested(int index)
{
    if (index < 0 || index >= m_paths.size()) return;
    bool wasActive = (m_activeIndex == index);
    if (wasActive) {
        m_slideshowTimer->stop();
        lyrics_renderer_media_clear();
        m_activeIndex = -1;
    } else if (m_activeIndex > index) {
        m_activeIndex--;
    }
    m_paths.removeAt(index);
    rebuildGrid();
    saveState();
}

void MusicPicturePanel::onBlank()
{
    m_slideshowTimer->stop();
    setActiveCard(-1);
    lyrics_renderer_media_clear();
}

void MusicPicturePanel::onSlideshowTimer()
{
    QList<int> checked;
    for (int i = 0; i < m_cards.size(); ++i)
        if (m_cards[i]->isChecked()) checked << i;

    if (checked.size() < 2) {
        m_slideshowTimer->stop();
        if (m_slideshowBtn) {
            QSignalBlocker blk(m_slideshowBtn);
            m_slideshowBtn->setChecked(false);
            m_slideshowBtn->setText(QString::fromUtf8("▶ Slideshow"));
        }
        return;
    }
    // Advance to next checked picture (wrap around)
    m_slideshowPos = (m_slideshowPos + 1) % checked.size();
    pushPicture(checked[m_slideshowPos]);
}

void MusicPicturePanel::stopSlideshow()
{
    m_slideshowTimer->stop();
    setActiveCard(-1);
    if (m_slideshowBtn) {
        QSignalBlocker blk(m_slideshowBtn);
        m_slideshowBtn->setChecked(false);
        m_slideshowBtn->setText(QString::fromUtf8("▶ Slideshow"));
    }
}

void MusicPicturePanel::clearDisplay()
{
    m_slideshowTimer->stop();
    setActiveCard(-1);
    if (m_slideshowBtn) {
        QSignalBlocker blk(m_slideshowBtn);
        m_slideshowBtn->setChecked(false);
        m_slideshowBtn->setText(QString::fromUtf8("▶ Slideshow"));
    }
    lyrics_renderer_media_clear();
}

void MusicPicturePanel::clearActiveCard()
{
    // UI-only: remove highlight without calling renderer
    setActiveCard(-1);
}

QString MusicPicturePanel::statePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/music-pictures.json";
}

void MusicPicturePanel::saveState()
{
    QJsonArray paths;
    for (const auto &p : m_paths) paths.append(p);

    QJsonArray checks;
    for (auto *c : m_cards) checks.append(c->isChecked());

    QJsonObject obj;
    obj["paths"]    = paths;
    obj["checks"]   = checks;
    obj["interval"] = m_intervalSpin->value();
    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(obj).toJson());
}

void MusicPicturePanel::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    QJsonObject obj = doc.object();
    m_paths.clear();
    for (const auto &v : obj["paths"].toArray()) {
        QString p = v.toString();
        if (QFile::exists(p)) m_paths << p;
    }
    if (obj.contains("interval"))
        m_intervalSpin->setValue(obj["interval"].toInt());

    rebuildGrid();

    // Restore checked state
    QJsonArray checks = obj["checks"].toArray();
    for (int i = 0; i < qMin(m_cards.size(), (int)checks.size()); ++i)
        m_cards[i]->setChecked(checks[i].toBool());
}

// ══════════════════════════════════════════════════════════════
// VideoTab
// ══════════════════════════════════════════════════════════════

VideoTab::VideoTab(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_videoPanel = new MvVideoPanel;
    root->addWidget(m_videoPanel);

    connect(m_videoPanel, &MvVideoPanel::videoStarted, this, [this](){
        emit playbackStarted();
    });
    connect(m_videoPanel, &MvVideoPanel::mediaListChanged, this, [this](const QStringList &p){
        emit mediaListChanged(p);
    });
}

void VideoTab::stopPlayback()
{
    // UI-only: deselect card and hide controls.
    // Do NOT call lyrics_renderer_mv_clear() here — the renderer source is
    // shared with the Music tab (MV source). Calling clear() after Music tab
    // starts would kill music that was just pushed.
    if (m_videoPanel) m_videoPanel->stopPlayback();
}

void VideoTab::setFadeDurationMs(int ms) { if (m_videoPanel) m_videoPanel->setFadeDurationMs(ms); }
void VideoTab::setNumCols(int n) { if (m_videoPanel) m_videoPanel->setNumCols(n); }

// ══════════════════════════════════════════════════════════════
// MusicTab
// ══════════════════════════════════════════════════════════════

MusicTab::MusicTab(QWidget *parent) : QWidget(parent)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal);

    auto *picGroup  = new QGroupBox("Background Pictures");
    auto *picLayout = new QVBoxLayout(picGroup);
    picLayout->setContentsMargins(0, 4, 0, 0);
    m_picturePanel = new MusicPicturePanel;
    picLayout->addWidget(m_picturePanel);

    auto *musicGroup  = new QGroupBox("Music");
    auto *musicLayout = new QVBoxLayout(musicGroup);
    musicLayout->setContentsMargins(0, 4, 0, 0);
    m_musicPanel = new MvMusicPanel;
    musicLayout->addWidget(m_musicPanel);

    splitter->addWidget(picGroup);
    splitter->addWidget(musicGroup);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 280});

    root->addWidget(splitter);

    // Music start doesn't stop pictures (they coexist by design)
    connect(m_musicPanel, &MvMusicPanel::musicStarted, this, [this](){
        emit playbackStarted();
    });
    // Picture activation also signals playback started (for cross-tab exclusion with VideoTab)
    connect(m_picturePanel, &MusicPicturePanel::pictureActivated, this, [this](){
        emit playbackStarted();
    });
    // When music is stopped (stop button or playlist end), also blank the picture slideshow
    connect(m_musicPanel, &MvMusicPanel::stopped,
            m_picturePanel, &MusicPicturePanel::clearDisplay);
}

void MusicTab::saveState()
{
    if (m_picturePanel) m_picturePanel->saveState();
    if (m_musicPanel)   m_musicPanel->saveState();
}

void MusicTab::loadState()
{
    if (m_picturePanel) m_picturePanel->loadState();
    if (m_musicPanel)   m_musicPanel->loadState();
}

void MusicTab::stopPlayback()
{
    // Full stop: clear OBS picture layer, stop slideshow, stop music, reset UI.
    if (m_picturePanel) m_picturePanel->clearDisplay();  // stops timer + clears picture OBS layer
    if (m_musicPanel)   m_musicPanel->stopPlayback();    // resets music UI
    lyrics_renderer_mv_music_clear();                    // destroy the OBS music source
}

void MusicTab::clearActiveUI()
{
    // Called when Video tab takes over. Stop slideshow UI and kill the OBS music source.
    if (m_picturePanel) m_picturePanel->stopSlideshow(); // stop timer, deselect card
    if (m_musicPanel)   m_musicPanel->stopPlayback();    // reset music UI
    lyrics_renderer_mv_music_clear();                    // destroy OBS music source
}
