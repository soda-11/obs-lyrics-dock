// audio-bin.cpp — background music player routed through the OBS lyrics source.
// Audio plays via a dedicated ffmpeg_source child of the lyrics OBS source,
// so it appears in the OBS sound meter and uses OBS audio monitoring.
#include "audio-bin.hpp"
#include "websocket-server.hpp"
#include "lyrics-renderer.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMenu>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

// ── AudioBin ─────────────────────────────────────────────────────

AudioBin::AudioBin(QWidget *parent) : QWidget(parent)
{
    auto *mainL = new QVBoxLayout(this);
    mainL->setContentsMargins(0, 0, 0, 0);
    mainL->setSpacing(4);

    // List of audio files — single click to play
    m_list = new QListWidget(this);
    m_list->setStyleSheet(
        "QListWidget { background:#141414; color:#ddd; border:1px solid #333;"
        "  font-size:11px; }"
        "QListWidget::item { padding:4px 6px; }"
        "QListWidget::item:selected { background:#2a4a6a; }"
        "QListWidget::item:hover { background:#1e2a3a; }");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::itemClicked,
            this, &AudioBin::onItemActivated);
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &AudioBin::onContextMenu);
    mainL->addWidget(m_list, 1);

    // Status label — shows playing file name or error
    m_statusLbl = new QLabel(QString::fromUtf8("■ Stopped"), this);
    m_statusLbl->setStyleSheet("color:#6b6b7b; font-size:9px; padding:0 2px;");
    m_statusLbl->setWordWrap(true);
    mainL->addWidget(m_statusLbl);

    // Buttons
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);
    m_addBtn = new QPushButton("+ Add", this);
    m_addBtn->setStyleSheet(
        "QPushButton{background:#1e1e28;color:#aaa;border:1px solid #444;"
        "border-radius:3px;padding:3px 8px;font-size:10px;}"
        "QPushButton:hover{background:#2a2a38;color:#eee;}");
    m_stopBtn = new QPushButton(QString::fromUtf8("■ Stop"), this);
    m_stopBtn->setStyleSheet(
        "QPushButton{background:#2a1a1a;color:#e05050;border:1px solid #5a2a2a;"
        "border-radius:3px;padding:3px 8px;font-size:10px;}"
        "QPushButton:hover{background:#3a2020;color:#ff6060;}");
    btnRow->addWidget(m_addBtn, 1);
    btnRow->addWidget(m_stopBtn);
    mainL->addLayout(btnRow);

    connect(m_addBtn,  &QPushButton::clicked, this, &AudioBin::onAddAudio);
    connect(m_stopBtn, &QPushButton::clicked, this, &AudioBin::onStop);
}

// ── Persistence ───────────────────────────────────────────────────

QString AudioBin::statePath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + "/audio-bin.json";
}

void AudioBin::saveState()
{
    QJsonArray arr;
    for (const auto &f : m_files) arr.append(f);
    QJsonObject obj; obj["files"] = arr;
    QFile fp(statePath());
    if (fp.open(QIODevice::WriteOnly))
        fp.write(QJsonDocument(obj).toJson());
}

void AudioBin::loadState()
{
    QFile fp(statePath());
    if (!fp.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(fp.readAll());
    m_files.clear();
    for (const auto &v : doc.object()["files"].toArray())
        m_files << v.toString();
    rebuildList();
}

// ── Slots ─────────────────────────────────────────────────────────

void AudioBin::onAddAudio()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, "Add Audio Files", {},
        "Audio Files (*.mp3 *.wav *.aac *.m4a *.ogg *.flac *.wma);;All Files (*)");
    for (const auto &p : paths) {
        if (!m_files.contains(p))
            m_files << p;
    }
    rebuildList();
    saveState();
    if (m_wsServer) m_wsServer->broadcastAudioBinList(m_files);
}

void AudioBin::onItemActivated(QListWidgetItem *item)
{
    playFile(m_list->row(item));
}

void AudioBin::onStop()
{
    stopPlayback();
}

void AudioBin::onContextMenu(const QPoint &pos)
{
    auto *item = m_list->itemAt(pos);
    if (!item) return;
    int idx = m_list->row(item);

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu{background:#1e1e24;color:#f0f0f5;border:1px solid #3a3a48;}"
        "QMenu::item:selected{background:#2a4a6a;}");
    auto *playAct   = menu.addAction("Play");
    auto *removeAct = menu.addAction("Remove");

    auto *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == playAct)
        playFile(idx);
    else if (chosen == removeAct) {
        if (m_activeIndex == idx) stopPlayback();
        else if (m_activeIndex > idx) --m_activeIndex;
        m_files.removeAt(idx);
        rebuildList();
        saveState();
        if (m_wsServer) m_wsServer->broadcastAudioBinList(m_files);
    }
}

// ── Private ───────────────────────────────────────────────────────

void AudioBin::playFileFromRemote(int index)
{
    playFile(index, true);
}

void AudioBin::playFile(int index, bool fromRemote)
{
    if (index < 0 || index >= m_files.size()) return;

    // Stop current playback first (clears OBS audio slot)
    stopPlayback();

    const QString &path = m_files[index];

    // Unload any playing video before starting audio
    lyrics_renderer_media_clear();

    // Route audio through the lyrics OBS source so it appears in the OBS
    // sound meter and uses OBS audio monitoring device settings.
    lyrics_renderer_audio_push(path);

    m_activeIndex = index;
    m_playing     = true;

    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        it->setForeground(i == index ? QColor("#4fc3f7") : QColor("#cccccc"));
        QFont f = it->font(); f.setBold(i == index); it->setFont(f);
    }
    m_statusLbl->setText(
        QString::fromUtf8("▶  ") + QFileInfo(m_files[index]).fileName());
    m_statusLbl->setStyleSheet("color:#4fc3f7; font-size:9px; padding:0 2px;");
    emit playbackStarted();
    if (m_wsServer && !fromRemote)
        m_wsServer->broadcastAudioBinState(index, true);
}

void AudioBin::stopPlayback()
{
    if (!m_playing && m_activeIndex < 0) return;

    lyrics_renderer_audio_clear();
    m_playing     = false;
    m_activeIndex = -1;

    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        it->setForeground(QColor("#cccccc"));
        QFont f = it->font(); f.setBold(false); it->setFont(f);
    }
    m_statusLbl->setText(QString::fromUtf8("■ Stopped"));
    m_statusLbl->setStyleSheet("color:#6b6b7b; font-size:9px; padding:0 2px;");

    if (m_wsServer) m_wsServer->broadcastAudioBinState(-1, false);
}

void AudioBin::rebuildList()
{
    m_list->clear();
    for (const auto &f : m_files) {
        auto *it = new QListWidgetItem(QFileInfo(f).fileName());
        it->setToolTip(f);
        it->setForeground(QColor("#cccccc"));
        m_list->addItem(it);
    }
    m_activeIndex = -1;
}
