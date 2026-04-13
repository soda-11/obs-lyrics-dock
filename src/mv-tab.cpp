// mv-tab.cpp  v1.0
//
// MV (Music & Video) tab implementation.
// Reuses MediaCard/MediaItem from media-gallery for the video panel.
// Audio output to OBS is handled by the existing mediaAudioCb in
// lyrics-renderer.cpp — no extra wiring needed here.

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

#include <windows.h>   // CoInitializeEx

// OBS_MEDIA_STATE_ENDED == 6  (from obs-module.h / media-io/media-io.h)
static const int kMediaStateEnded = 6;

static const QString VIDEO_FILTER =
    "Video Files (*.mp4 *.mov *.avi *.mkv *.webm *.flv *.wmv *.m4v)";
static const QString AUDIO_FILTER =
    "Audio Files (*.mp3 *.wav *.aac *.flac *.ogg *.m4a *.wma *.opus)";

// ══════════════════════════════════════════════════════════════
// MvVideoPanel
// ══════════════════════════════════════════════════════════════

MvVideoPanel::MvVideoPanel(QWidget *parent) : QWidget(parent)
{
    // COM ref-counted — safe to call even if MediaGallery already called it
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    buildUi();
    loadState();
}

void MvVideoPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar ───────────────────────────────────────────────
    auto *topBar = new QWidget;
    topBar->setStyleSheet("background:#222;border-bottom:1px solid #333;");
    auto *topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(6, 4, 6, 4);

    auto *addBtn = new QPushButton("+ Add Videos");
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

    m_clearBtn = new QPushButton(QString::fromUtf8("\u25A0 Stop"));
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

    // ── Thumbnail grid ────────────────────────────────────────
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet(
        "QScrollArea{background:#181818;border:none;}");
    m_container = new QWidget;
    m_container->setStyleSheet("background:#181818;");
    m_container->setAcceptDrops(false); // no reorder in MV panel
    m_container->installEventFilter(this);
    m_grid = new QGridLayout(m_container);
    m_grid->setContentsMargins(6, 6, 6, 6);
    m_grid->setSpacing(6);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_scrollArea->setWidget(m_container);
    root->addWidget(m_scrollArea, 1);

    // ── Video controls ────────────────────────────────────────
    m_videoControls = new QWidget;
    m_videoControls->setStyleSheet(
        "background:#222;border-top:1px solid #333;");
    auto *vcL = new QVBoxLayout(m_videoControls);
    vcL->setContentsMargins(6, 4, 6, 4);
    vcL->setSpacing(4);

    auto *scrubRow = new QHBoxLayout;
    m_playPauseBtn = new QToolButton;
    m_playPauseBtn->setText(QString::fromUtf8("\u23F8")); // pause icon
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

    scrubRow->addWidget(m_playPauseBtn);
    scrubRow->addWidget(m_scrubber, 1);
    scrubRow->addWidget(m_timeLbl);
    vcL->addLayout(scrubRow);
    m_videoControls->setVisible(false);
    root->addWidget(m_videoControls);

    // ── Fade duration bar ─────────────────────────────────────
    auto *fadeBar = new QWidget;
    fadeBar->setStyleSheet(
        "background:#1e1e1e;border-top:1px solid #333;");
    auto *fadeL = new QHBoxLayout(fadeBar);
    fadeL->setContentsMargins(8, 4, 8, 4);
    auto *fadeLbl = new QLabel("Fade:");
    fadeLbl->setStyleSheet("color:#aaa;font-size:9px;");
    m_fadeSpin = new QSpinBox;
    m_fadeSpin->setRange(0, 3000);
    m_fadeSpin->setSingleStep(50);
    m_fadeSpin->setValue(500);
    m_fadeSpin->setSuffix(" ms");
    m_fadeSpin->setFixedWidth(90);
    fadeL->addWidget(fadeLbl);
    fadeL->addWidget(m_fadeSpin);
    fadeL->addStretch();
    root->addWidget(fadeBar);

    // ── Poll timer ────────────────────────────────────────────
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout,
            this, &MvVideoPanel::onPollTimer);
    m_pollTimer->start();

    // ── Connections ───────────────────────────────────────────
    connect(addBtn,       &QPushButton::clicked,
            this, &MvVideoPanel::onAddVideo);
    connect(m_clearBtn,   &QPushButton::clicked,
            this, &MvVideoPanel::onClearVideo);
    connect(m_seqBtn,     &QPushButton::toggled,
            this, &MvVideoPanel::onSequentialToggle);
    connect(m_playPauseBtn, &QToolButton::clicked,
            this, &MvVideoPanel::onPlayPause);
    // OBS-style seek: seek immediately on user drag; timer respects isSliderDown()
    connect(m_scrubber, &QSlider::sliderMoved, this, [this](int v) {
        int64_t t = 0, d = 0;
        if (!lyrics_renderer_mv_get_time(t, d) || d <= 0) return;
        int64_t seekMs = (int64_t)((double)v / 1000.0 * (double)d);
        lyrics_renderer_mv_set_time(seekMs);
        m_seekCooldown = 5; // suppress false end-detection for ~1.5 s
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
        if (nc != m_currentCols) {
            m_currentCols = nc;
            rebuildGrid();
        }
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
        // Disable drop-target reorder (no MediaGallery parent)
        card->setAcceptDrops(false);
        connect(card, &MediaCard::clicked,
                this, &MvVideoPanel::onCardClicked);
        connect(card, &MediaCard::removeRequested,
                this, &MvVideoPanel::onCardRemoveRequested);
        card->setActive(i == m_activeIndex);
        m_grid->addWidget(card, row, col);
        m_cards << card;
        if (++col >= m_currentCols) { col = 0; ++row; }
    }
}

void MvVideoPanel::setActiveCard(int index)
{
    m_activeIndex = index;
    for (auto *c : m_cards)
        c->setActive(c->index() == index);
}

void MvVideoPanel::updateVideoControls()
{
    bool show = (m_activeIndex >= 0 &&
                 m_activeIndex < m_items.size() &&
                 m_items[m_activeIndex].isVideo);
    m_videoControls->setVisible(show);
    if (show) {
        m_paused = false;
        m_playPauseBtn->setText(QString::fromUtf8("\u23F8")); // pause
    }
}

void MvVideoPanel::playVideoAt(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    setActiveCard(index);
    m_endHandled = false;
    m_paused     = false;
    updateVideoControls();
    lyrics_renderer_mv_video_push(m_items[index].path, m_fadeSpin->value());
    emit videoStarted();
}

void MvVideoPanel::onAddVideo()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Videos", QString(), VIDEO_FILTER);
    if (files.isEmpty()) return;
    for (const auto &f : files) {
        MediaItem item; item.path = f; item.isVideo = true;
        m_items << item;
    }
    rebuildGrid();
    saveState();
}

void MvVideoPanel::onCardClicked(int index)
{
    playVideoAt(index);
    saveState();
}

void MvVideoPanel::onCardRemoveRequested(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    if (m_activeIndex == index) {
        stopPlayback();
        lyrics_renderer_mv_clear();
    } else if (m_activeIndex > index) {
        m_activeIndex--;
    }
    m_items.removeAt(index);
    rebuildGrid();
    saveState();
}

void MvVideoPanel::onClearVideo()
{
    setActiveCard(-1);
    m_videoControls->setVisible(false);
    lyrics_renderer_mv_clear(m_fadeSpin->value());
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
    m_playPauseBtn->setText(
        m_paused ? QString::fromUtf8("\u25B6")
                 : QString::fromUtf8("\u23F8"));
}

// (seek bar now uses sliderMoved lambda in buildUi — no separate slot needed)

void MvVideoPanel::onPollTimer()
{
    // ── Update scrubber and time label ────────────────────────
    // isSliderDown() is Qt's native way to know if user is dragging (OBS approach)
    if (m_videoControls->isVisible() && !m_scrubber->isSliderDown()) {
        int64_t t = 0, d = 0;
        if (lyrics_renderer_mv_get_time(t, d) && d > 0) {
            m_scrubber->setValue((int)((double)t / (double)d * 1000.0));
            auto fmt = [](int64_t ms) -> QString {
                int s = (int)(ms / 1000);
                return QString("%1:%2").arg(s / 60)
                                      .arg(s % 60, 2, 10, QChar('0'));
            };
            m_timeLbl->setText(fmt(t) + " / " + fmt(d));
        }
    }

    // ── End-of-video detection ────────────────────────────────
    if (m_activeIndex < 0 || m_endHandled) return;
    if (m_seekCooldown > 0) { --m_seekCooldown; return; } // ignore state briefly after seek
    int state = lyrics_renderer_mv_get_state();
    if (state != kMediaStateEnded) return;

    m_endHandled = true;

    if (m_sequential) {
        // Play next video; stop after last (no loop)
        int next = m_activeIndex + 1;
        if (next < m_items.size()) {
            playVideoAt(next);
        } else {
            // End of list — clear screen
            setActiveCard(-1);
            m_videoControls->setVisible(false);
            lyrics_renderer_mv_clear();
        }
    } else {
        // No sequential — just go blank
        setActiveCard(-1);
        m_videoControls->setVisible(false);
        lyrics_renderer_mv_clear();
    }
}

void MvVideoPanel::stopPlayback()
{
    setActiveCard(-1);
    m_videoControls->setVisible(false);
    m_paused     = false;
    m_endHandled = true; // prevent poll timer from acting
}

QString MvVideoPanel::statePath() const
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/mv-video.json";
}

void MvVideoPanel::saveState()
{
    QJsonArray arr;
    for (const auto &item : m_items) arr.append(item.path);
    QJsonObject obj;
    obj["items"]  = arr;
    obj["fadeMs"] = m_fadeSpin->value();
    obj["sequential"] = m_sequential;
    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
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
        MediaItem item; item.path = path; item.isVideo = true;
        m_items << item;
    }
    if (obj.contains("fadeMs"))
        m_fadeSpin->setValue(obj["fadeMs"].toInt());
    bool seq = obj["sequential"].toBool(false);
    m_seqBtn->setChecked(seq); // triggers onSequentialToggle
    rebuildGrid();
}

// ══════════════════════════════════════════════════════════════
// MvMusicPanel
// ══════════════════════════════════════════════════════════════

MvMusicPanel::MvMusicPanel(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top bar ───────────────────────────────────────────────
    auto *topBar = new QWidget;
    topBar->setStyleSheet("background:#222;border-bottom:1px solid #333;");
    auto *topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(6, 4, 6, 4);

    auto *addBtn = new QPushButton("+ Add Music");
    addBtn->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));

    // Engine badge — shows which player backend will be used for music
    bool vlc = lyrics_renderer_vlc_available();
    auto *engineLbl = new QLabel(vlc ? "VLC" : "FFmpeg");
    engineLbl->setToolTip(vlc
        ? "Using VLC source — accurate MP3/OGG/FLAC seeking"
        : "VLC plugin not found — using FFmpeg (MP3 seeking unreliable).\n"
          "Install OBS VLC plugin for better music seeking.");
    engineLbl->setStyleSheet(vlc
        ? "color:#5dbb5d;font-size:9px;font-weight:bold;"
          "border:1px solid #3a8a3a;border-radius:3px;padding:1px 4px;"
          "background:#1a3a1a;"
        : "color:#ffaa44;font-size:9px;font-weight:bold;"
          "border:1px solid #885522;border-radius:3px;padding:1px 4px;"
          "background:#2a1a0a;");

    m_removeBtn = new QPushButton;
    m_removeBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_removeBtn->setToolTip("Remove selected track");
    m_removeBtn->setFixedSize(32, 28);
    m_removeBtn->setEnabled(false);

    m_stopBtn = new QPushButton(QString::fromUtf8("\u25A0 Stop"));
    m_stopBtn->setFixedSize(68, 22);
    m_stopBtn->setStyleSheet(
        "QPushButton{background:#4a1a1a;color:#ff6b6b;"
        "border:1px solid #8b2020;border-radius:3px;padding:0 2px;}"
        "QPushButton:hover{background:#6a2020;}");

    topL->addWidget(addBtn);
    topL->addWidget(engineLbl);
    topL->addStretch();
    topL->addWidget(m_removeBtn);
    topL->addWidget(m_stopBtn);
    root->addWidget(topBar);

    // ── Playlist ──────────────────────────────────────────────
    m_list = new QListWidget;
    m_list->setStyleSheet(
        "QListWidget{background:#181818;border:none;}"
        "QListWidget::item{padding:4px 6px;color:#ccc;}"
        "QListWidget::item:selected{background:#1e6bb8;color:#fff;}"
        "QListWidget::item:hover{background:#252525;}");
    m_list->setDragEnabled(true);
    m_list->setAcceptDrops(true);
    m_list->setDropIndicatorShown(true);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    root->addWidget(m_list, 1);

    // ── Seek bar (play/pause + scrubber + time) ───────────────────────────────────────────────
    m_playPauseBtn = new QToolButton;
    m_playPauseBtn->setText(QString::fromUtf8("\u25B6")); // ▶
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

    // ── Now-playing bar ───────────────────────────────────────
    auto *npBar = new QWidget;
    npBar->setStyleSheet(
        "background:#1a1a1a;border-top:1px solid #333;");
    auto *npL = new QHBoxLayout(npBar);
    npL->setContentsMargins(8, 4, 8, 4);
    auto *npIcon = new QLabel(QString::fromUtf8("\u266A")); // ♪
    npIcon->setStyleSheet("color:#4da6ff;font-size:12px;");
    m_nowPlayingLbl = new QLabel;
    m_nowPlayingLbl->setStyleSheet("color:#aaa;font-size:9px;");
    m_nowPlayingLbl->setWordWrap(false);
    npL->addWidget(npIcon);
    npL->addWidget(m_nowPlayingLbl, 1);
    root->addWidget(npBar);

    // ── Poll timer ────────────────────────────────────────────
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(300);
    connect(m_pollTimer, &QTimer::timeout,
            this, &MvMusicPanel::onPollTimer);
    m_pollTimer->start();

    // ── Connections ───────────────────────────────────────────
    connect(addBtn,         &QPushButton::clicked,
            this, &MvMusicPanel::onAddMusic);
    connect(m_playPauseBtn, &QToolButton::clicked,
            this, &MvMusicPanel::onPlayPause);
    connect(m_removeBtn,    &QPushButton::clicked,
            this, &MvMusicPanel::onRemoveMusic);
    connect(m_stopBtn,      &QPushButton::clicked,
            this, &MvMusicPanel::onStopMusic);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &MvMusicPanel::onItemDoubleClicked);
    connect(m_list, &QListWidget::currentRowChanged,
            this, [this](int){ updateButtons(); });
    // OBS-style seek: seek immediately on user drag; timer respects isSliderDown().
    // Uses m_trackDuration (cached by poll timer) so seek works even if
    // obs_source_media_get_duration returns 0 on the first query during a drag.
    connect(m_musicScrubber, &QSlider::sliderMoved, this, [this](int v) {
        if (m_trackDuration <= 0) {
            // Duration not yet cached — try to get it now
            int64_t t = 0, d = 0;
            if (!lyrics_renderer_mv_get_time(t, d) || d <= 0) return;
            m_trackDuration = d;
        }
        int64_t seekMs = (int64_t)((double)v / 1000.0 * (double)m_trackDuration);
        lyrics_renderer_mv_set_time(seekMs);
        m_seekCooldown = 5; // suppress false end-detection for ~1.5 s
        auto fmt = [](int64_t ms) -> QString {
            int s = (int)(ms / 1000);
            return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
        };
        m_musicTimeLbl->setText(fmt(seekMs) + " / " + fmt(m_trackDuration));
    });
    connect(m_list->model(), &QAbstractItemModel::rowsMoved,
            this, [this]() {
                // Rebuild m_items from widget order (UserRole holds file paths)
                QString activePath = (m_activeIndex >= 0 && m_activeIndex < m_items.size())
                                     ? m_items[m_activeIndex] : QString();
                m_items.clear();
                for (int i = 0; i < m_list->count(); ++i)
                    m_items << m_list->item(i)->data(Qt::UserRole).toString();
                m_activeIndex = activePath.isEmpty() ? -1 : m_items.indexOf(activePath);
                saveState();
            });

    loadState();
}

void MvMusicPanel::updateButtons()
{
    m_removeBtn->setEnabled(m_list->currentRow() >= 0);
}

void MvMusicPanel::setActiveRow(int index)
{
    m_activeIndex = index;
    // Use QListWidget's built-in selection for highlighting so the
    // stylesheet's ::item:selected rule is what paints the active row.
    // Manual setBackground/setForeground are overridden by stylesheets in Qt.
    {
        // Block the currentRowChanged signal so updateButtons() isn't called
        // spuriously while we programmatically select the playing row.
        QSignalBlocker blk(m_list);
        if (index >= 0)
            m_list->setCurrentRow(index);
        else
            m_list->clearSelection();
    }
    if (index >= 0 && index < m_items.size()) {
        QString name = QFileInfo(m_items[index]).fileName();
        QFontMetrics fm(m_nowPlayingLbl->font());
        m_nowPlayingLbl->setText(
            fm.elidedText(name, Qt::ElideMiddle,
                          m_nowPlayingLbl->width() > 0
                          ? m_nowPlayingLbl->width() : 200));
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
    m_trackDuration = 0;  // will be populated by poll timer once source reports it
    m_musicScrubber->setValue(0);
    m_musicScrubber->setEnabled(true);
    m_musicTimeLbl->setText("0:00 / 0:00");
    m_playPauseBtn->setText(QString::fromUtf8("\u23F8")); // ⏸ playing
    lyrics_renderer_mv_music_push(m_items[index]);
    emit musicStarted();
}

void MvMusicPanel::onAddMusic()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "Add Music", QString(), AUDIO_FILTER);
    if (files.isEmpty()) return;
    for (const auto &f : files) {
        if (QFile::exists(f)) {
            m_items << f;
            QString name = QFileInfo(f).fileName();
            auto *it = new QListWidgetItem(name);
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
    if (m_activeIndex == row) {
        stopPlayback();
        lyrics_renderer_mv_clear();
    } else if (m_activeIndex > row) {
        m_activeIndex--;
    }
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
    m_playPauseBtn->setText(QString::fromUtf8("\u25B6"));
    m_musicScrubber->setValue(0);
    m_musicScrubber->setEnabled(false);
    m_musicTimeLbl->setText("0:00 / 0:00");
    lyrics_renderer_mv_clear();
}

void MvMusicPanel::onPlayPause()
{
    if (m_activeIndex < 0) {
        // Nothing playing — start the selected track
        int row = m_list->currentRow();
        if (row >= 0) playMusicAt(row);
        return;
    }
    m_musicPaused = !m_musicPaused;
    lyrics_renderer_mv_play_pause(m_musicPaused);
    m_playPauseBtn->setText(
        m_musicPaused ? QString::fromUtf8("\u25B6")   // ▶ resume
                      : QString::fromUtf8("\u23F8"));  // ⏸ pause
}

// (scrubber now uses sliderMoved lambda in buildUi — no separate slots needed)

void MvMusicPanel::onPollTimer()
{
    // ── Update scrubber position ───────────────────────────────
    // isSliderDown() is Qt's native drag-detection (OBS approach)
    if (m_activeIndex >= 0) {
        int64_t t = 0, d = 0;
        if (lyrics_renderer_mv_get_time(t, d) && d > 0) {
            if (m_trackDuration <= 0)
                m_trackDuration = d; // cache duration the first time we get it
            if (!m_musicScrubber->isSliderDown()) {
                m_musicScrubber->setValue((int)((double)t / (double)m_trackDuration * 1000.0));
                auto fmt = [](int64_t ms) -> QString {
                    int s = (int)(ms / 1000);
                    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
                };
                m_musicTimeLbl->setText(fmt(t) + " / " + fmt(m_trackDuration));
            }
        }
    }

    // ── End-of-track detection ─────────────────────────────────
    if (m_activeIndex < 0 || m_endHandled) return;
    if (m_seekCooldown > 0) { --m_seekCooldown; return; } // ignore state after seek
    int state = lyrics_renderer_mv_get_state();
    if (state != kMediaStateEnded) return;

    m_endHandled = true;

    // Always auto-advance to next track (playlist behaviour)
    int next = m_activeIndex + 1;
    if (next < m_items.size()) {
        playMusicAt(next);
    } else {
        // End of playlist — stop
        setActiveRow(-1);
        m_musicScrubber->setValue(0);
        m_musicScrubber->setEnabled(false);
        m_musicTimeLbl->setText("0:00 / 0:00");
        lyrics_renderer_mv_clear();
    }
}

void MvMusicPanel::stopPlayback()
{
    setActiveRow(-1);
    m_endHandled    = true;
    m_musicPaused   = false;
    m_seekCooldown  = 0;
    m_trackDuration = 0;
    m_playPauseBtn->setText(QString::fromUtf8("\u25B6"));
    m_musicScrubber->setValue(0);
    m_musicScrubber->setEnabled(false);
    m_musicTimeLbl->setText("0:00 / 0:00");
}

QString MvMusicPanel::statePath() const
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/mv-music.json";
}

void MvMusicPanel::saveState()
{
    QJsonArray arr;
    for (const auto &path : m_items) arr.append(path);
    QJsonObject obj; obj["items"] = arr;
    QFile f(statePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
}

void MvMusicPanel::loadState()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    m_items.clear();
    m_list->clear();
    for (const auto &v : doc.object()["items"].toArray()) {
        QString path = v.toString();
        if (!QFile::exists(path)) continue;
        m_items << path;
        auto *it = new QListWidgetItem(QFileInfo(path).fileName());
        it->setData(Qt::UserRole, path);
        m_list->addItem(it);
    }
}

// ══════════════════════════════════════════════════════════════
// MvTab
// ══════════════════════════════════════════════════════════════

MvTab::MvTab(QWidget *parent) : QWidget(parent)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal);

    auto *videoGroup  = new QGroupBox("Videos");
    auto *videoLayout = new QVBoxLayout(videoGroup);
    videoLayout->setContentsMargins(0, 4, 0, 0);
    m_videoPanel = new MvVideoPanel;
    videoLayout->addWidget(m_videoPanel);

    auto *musicGroup  = new QGroupBox("Music");
    auto *musicLayout = new QVBoxLayout(musicGroup);
    musicLayout->setContentsMargins(0, 4, 0, 0);
    m_musicPanel = new MvMusicPanel;
    musicLayout->addWidget(m_musicPanel);

    splitter->addWidget(videoGroup);
    splitter->addWidget(musicGroup);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 280});

    root->addWidget(splitter);

    // ── Mutual exclusion between panels ──────────────────────
    // Video starts → stop music panel UI (renderer source is replaced anyway)
    connect(m_videoPanel, &MvVideoPanel::videoStarted, this, [this](){
        m_musicPanel->stopPlayback();
        emit playbackStarted();
    });
    // Music starts → stop video panel UI
    connect(m_musicPanel, &MvMusicPanel::musicStarted, this, [this](){
        m_videoPanel->stopPlayback();
        emit playbackStarted();
    });
}

void MvTab::stopPlayback()
{
    lyrics_renderer_mv_clear();
    m_videoPanel->stopPlayback();
    m_musicPanel->stopPlayback();
}

void MvTab::saveState()
{
    m_videoPanel->saveState();
    m_musicPanel->saveState();
}

void MvTab::loadState()
{
    m_videoPanel->loadState();
    m_musicPanel->loadState();
}
