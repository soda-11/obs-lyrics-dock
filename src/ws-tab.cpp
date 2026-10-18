// ws-tab.cpp — WebSocket settings tab  v1.5
// Preview: QTimer fires at capture FPS on the UI thread.  Each tick acquires
// the OBS graphics mutex (obs_enter_graphics), maps the previous frame's staged
// surface (fast — GPU is done by now), JPEG-encodes, broadcasts over WebSocket,
// then renders the current frame into a texrender and stages it for the next tick.
// GPU hold time is <5 ms at typical resolutions.  This matches OBS's own
// screenshot code (obs-basic-main.cpp) and avoids render-thread complexity.
#include "ws-tab.hpp"
#include "websocket-server.hpp"
#include "lyrics-renderer.hpp"
#include "qrcodegen.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPixmap>
#include <QImage>
#include <QBuffer>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>
#include <QLabel>

#include <obs-module.h>
#include <graphics/graphics.h>

// Declared in websocket-server.cpp
QString bestLocalIpAddress();

// ── QR helpers ──────────────────────────────────────────────────

static QImage makeQrImage(const QString &text, int pixelSize)
{
    try {
        auto grid = qrcodegen::encode(text.toStdString());
        int  n    = (int)grid.size();
        if (n == 0) return {};
        const int border = 4;
        int total = n + 2 * border;
        QImage raw(total, total, QImage::Format_RGB32);
        raw.fill(0xFFFFFFFFu);
        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++)
                raw.setPixel(c + border, r + border,
                             grid[r][c] ? 0xFF000000u : 0xFFFFFFFFu);
        return raw.scaled(pixelSize, pixelSize,
                          Qt::KeepAspectRatio,
                          Qt::FastTransformation);
    } catch (...) {
        return {};
    }
}

static QString saveQrPng(const QString &url)
{
    QImage img = makeQrImage(url, 200);
    if (img.isNull()) return {};
    QString dir  = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString path = dir + "/obs_lyrics_qr.png";
    img.save(path, "PNG");
    return path;
}

// ── WsTab ────────────────────────────────────────────────────────

WsTab::WsTab(QWidget *parent) : QWidget(parent)
{
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, &WsTab::onCaptureTick);

    buildUi();
    loadSettings();
    updateStatus();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &WsTab::onRefreshClients);
    m_refreshTimer->start();
}

WsTab::~WsTab()
{
    stopPreview();
}

void WsTab::stopPreview()
{
    m_captureTimer->stop();
    m_staged = false;
    if (m_texrender || m_stageSurface) {
        obs_enter_graphics();
        if (m_texrender) {
            gs_texrender_destroy(static_cast<gs_texrender_t *>(m_texrender));
            m_texrender = nullptr;
        }
        if (m_stageSurface) {
            gs_stagesurface_destroy(static_cast<gs_stagesurf_t *>(m_stageSurface));
            m_stageSurface = nullptr;
        }
        obs_leave_graphics();
    }
}

// ── Capture tick (UI thread) ─────────────────────────────────────

void WsTab::onCaptureTick()
{
    int W = m_captureW;
    int H = m_captureH;
    int Q = m_captureQ;

    obs_enter_graphics();

    auto *tr = static_cast<gs_texrender_t  *>(m_texrender);
    auto *ss = static_cast<gs_stagesurf_t  *>(m_stageSurface);

    // ── Step 1: Map previous frame (GPU transfer done since last tick) ──
    if (m_staged && ss) {
        uint8_t  *data     = nullptr;
        uint32_t  linesize = 0;
        if (gs_stagesurface_map(ss, &data, &linesize) && data && linesize > 0) {
            // GS_RGBA → Format_RGBA8888
            QImage img(data, W, H, (int)linesize, QImage::Format_RGBA8888);
            QByteArray jpeg;
            QBuffer    buf(&jpeg);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "JPEG", Q);
            gs_stagesurface_unmap(ss);
            if (!jpeg.isEmpty()) {
                auto *srv = lyricsWsServer();
                if (srv) srv->broadcastPreview(jpeg);
            }
        }
        m_staged = false;
    }

    // ── Step 2: Rebuild GPU resources if needed ──
    bool sizeOk = ss &&
                  (int)gs_stagesurface_get_width(ss)  == W &&
                  (int)gs_stagesurface_get_height(ss) == H;
    if (!sizeOk) {
        if (tr) { gs_texrender_destroy(tr);    tr = nullptr; m_texrender    = nullptr; }
        if (ss) { gs_stagesurface_destroy(ss); ss = nullptr; m_stageSurface = nullptr; }
        m_staged = false;
    }
    if (!tr) { tr = gs_texrender_create(GS_RGBA, GS_ZS_NONE); m_texrender    = tr; }
    if (!ss) { ss = gs_stagesurface_create(W, H, GS_RGBA);     m_stageSurface = ss; }

    if (!tr || !ss) { obs_leave_graphics(); return; }

    // ── Step 3: Render OBS composite output into our texture ──
    gs_texrender_reset(tr);
    if (gs_texrender_begin(tr, W, H)) {
        obs_video_info ovi = {};
        if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
            gs_ortho(0.0f, (float)ovi.base_width,
                     0.0f, (float)ovi.base_height,
                     -100.0f, 100.0f);
            gs_blend_state_push();
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
            obs_render_main_texture();
            gs_blend_state_pop();
        }
        gs_texrender_end(tr);

        // ── Step 4: Initiate async GPU→CPU transfer (read next tick) ──
        gs_stage_texture(ss, gs_texrender_get_texture(tr));
        m_staged = true;
    }

    obs_leave_graphics();
}

// ── Preview controls ─────────────────────────────────────────────

void WsTab::onPreviewEnable(bool checked)
{
    if (checked) {
        int idx = m_sizeCombo ? m_sizeCombo->currentIndex() : 0;
        m_captureW = (idx == 1) ? 854  : 1280;
        m_captureH = (idx == 1) ? 480  : 720;
        m_captureQ = m_qualitySlider ? m_qualitySlider->value() : 75;

        int fps = m_fpsSlider ? m_fpsSlider->value() : 5;
        if (fps < 1) fps = 1;
        m_captureTimer->setInterval(1000 / fps);
        m_captureTimer->start();

        QString szStr = (idx == 1) ? "480p" : "720p";
        m_previewStatus->setText(
            QString("Broadcasting %1 @ %2 fps, quality %3%")
            .arg(szStr).arg(fps).arg(m_captureQ));
        m_previewStatus->setStyleSheet("color:#44ff88;font-size:10px;");
    } else {
        stopPreview();
        m_previewStatus->setText("Preview disabled");
        m_previewStatus->setStyleSheet("color:#888;font-size:10px;");
    }
    saveSettings();
}

void WsTab::onFpsChanged(int val)
{
    m_fpsLbl->setText(QString::number(val));
    if (m_captureTimer->isActive()) {
        if (val < 1) val = 1;
        m_captureTimer->setInterval(1000 / val);
    }
    if (m_previewEnable->isChecked()) {
        int idx = m_sizeCombo ? m_sizeCombo->currentIndex() : 0;
        m_previewStatus->setText(
            QString("Broadcasting %1 @ %2 fps, quality %3%")
            .arg(idx == 1 ? "480p" : "720p").arg(val).arg(m_captureQ));
    }
    saveSettings();
}

void WsTab::onQualityChanged(int val)
{
    m_qualityLbl->setText(QString("%1%").arg(val));
    m_captureQ = val;
    if (m_previewEnable->isChecked()) {
        int idx = m_sizeCombo ? m_sizeCombo->currentIndex() : 0;
        m_previewStatus->setText(
            QString("Broadcasting %1 @ %2 fps, quality %3%")
            .arg(idx == 1 ? "480p" : "720p")
            .arg(m_fpsSlider ? m_fpsSlider->value() : 5).arg(val));
    }
    saveSettings();
}

void WsTab::onSizeChanged(int index)
{
    m_captureW = (index == 1) ? 854  : 1280;
    m_captureH = (index == 1) ? 480  : 720;
    // Force resource rebuild on next tick by flushing staged flag
    if (m_captureTimer->isActive()) {
        obs_enter_graphics();
        if (m_texrender)    { gs_texrender_destroy(static_cast<gs_texrender_t*>(m_texrender));    m_texrender    = nullptr; }
        if (m_stageSurface) { gs_stagesurface_destroy(static_cast<gs_stagesurf_t*>(m_stageSurface)); m_stageSurface = nullptr; }
        obs_leave_graphics();
        m_staged = false;
    }
    if (m_previewEnable && m_previewEnable->isChecked()) {
        int fps = m_fpsSlider ? m_fpsSlider->value() : 5;
        m_previewStatus->setText(
            QString("Broadcasting %1 @ %2 fps, quality %3%")
            .arg(index == 1 ? "480p" : "720p").arg(fps).arg(m_captureQ));
    }
    saveSettings();
}

// ── Build UI ─────────────────────────────────────────────────────

void WsTab::buildUi()
{
    auto *rootV = new QVBoxLayout(this);
    rootV->setContentsMargins(6, 6, 6, 6);
    rootV->setSpacing(8);

    // ── Top row: three columns ────────────────────────────────────
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(8);

    // LEFT: WebSocket Remote Control
    auto *wsGroup  = new QGroupBox("WebSocket Remote Control");
    auto *wsLayout = new QVBoxLayout(wsGroup);
    wsLayout->setSpacing(6);

    m_enableCheck = new QCheckBox("Enable remote control server");

    auto *portRow = new QHBoxLayout;
    portRow->addWidget(new QLabel("Port:"));
    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(8765);
    m_portSpin->setFixedWidth(75);
    m_applyBtn = new QPushButton("Apply");
    m_applyBtn->setFixedWidth(60);
    portRow->addWidget(m_portSpin);
    portRow->addWidget(m_applyBtn);
    portRow->addStretch();

    auto *pinRow = new QHBoxLayout;
    pinRow->addWidget(new QLabel("PIN:"));
    m_pinEdit = new QLineEdit;
    m_pinEdit->setPlaceholderText("(empty = no PIN)");
    m_pinEdit->setMaxLength(4);
    m_pinEdit->setFixedWidth(100);
    m_pinEdit->setInputMask("9999;_");
    pinRow->addWidget(m_pinEdit);
    pinRow->addStretch();

    m_statusLbl = new QLabel("Status: Stopped");
    m_statusLbl->setStyleSheet("color:#888;font-size:10px;");

    m_urlLbl = new QLabel;
    m_urlLbl->setStyleSheet("color:#4da6ff;font-size:10px;font-family:monospace;");
    m_urlLbl->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_urlLbl->setWordWrap(true);
    m_urlLbl->setCursor(Qt::PointingHandCursor);
    connect(m_urlLbl, &QLabel::linkActivated, this, [](const QString &url){
        QApplication::clipboard()->setText(url);
    });

    m_showQrCheck = new QCheckBox("Show QR on OBS preview");
    m_showQrCheck->setToolTip("Overlay the QR code at the bottom-right of the OBS canvas\nwhile this tab is active.");
    connect(m_showQrCheck, &QCheckBox::toggled, this, &WsTab::onShowQrToggled);

    wsLayout->addWidget(m_enableCheck);
    wsLayout->addLayout(portRow);
    wsLayout->addLayout(pinRow);
    wsLayout->addWidget(m_statusLbl);
    wsLayout->addWidget(m_urlLbl);
    wsLayout->addWidget(m_showQrCheck);
    wsLayout->addStretch();

    // MIDDLE: QR Code
    auto *qrGroup  = new QGroupBox("QR Code");
    auto *qrLayout = new QVBoxLayout(qrGroup);
    m_qrLbl = new QLabel;
    m_qrLbl->setAlignment(Qt::AlignCenter);
    m_qrLbl->setFixedSize(160, 160);
    m_qrLbl->setStyleSheet("background:white;border:1px solid #444;");
    m_qrLbl->setToolTip("Scan to open the remote page on your phone");
    qrLayout->addWidget(m_qrLbl, 0, Qt::AlignCenter);
    qrLayout->addStretch();

    // RIGHT: Connected Clients
    auto *clientGroup  = new QGroupBox("Connected Clients");
    auto *clientLayout = new QVBoxLayout(clientGroup);
    m_clientList = new QListWidget;
    m_clientList->setStyleSheet("QListWidget{background:#1a1a1a;color:#ccc;font-size:10px;}");
    clientLayout->addWidget(m_clientList);

    topRow->addWidget(wsGroup, 1);
    topRow->addWidget(qrGroup, 1);
    topRow->addWidget(clientGroup, 1);
    rootV->addLayout(topRow);

    // ── Bottom row: Preview Stream ────────────────────────────────
    auto *pvGroup  = new QGroupBox("Preview Stream (OBS → Remote)");
    auto *pvLayout = new QHBoxLayout(pvGroup);
    pvLayout->setSpacing(12);

    m_previewEnable = new QCheckBox("Enable Preview");
    m_previewEnable->setToolTip("Stream OBS composite output as JPEG to all connected clients");

    auto *pvCols = new QVBoxLayout;

    // Size
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel("Size:"));
    m_sizeCombo = new QComboBox;
    m_sizeCombo->addItem("720p  (1280×720)");
    m_sizeCombo->addItem("480p  (854×480)");
    m_sizeCombo->setFixedWidth(140);
    sizeRow->addWidget(m_sizeCombo);
    sizeRow->addStretch();
    pvCols->addLayout(sizeRow);

    // FPS
    auto *fpsRow = new QHBoxLayout;
    fpsRow->addWidget(new QLabel("FPS:"));
    m_fpsSlider = new QSlider(Qt::Horizontal);
    m_fpsSlider->setRange(1, 30);
    m_fpsSlider->setValue(5);
    m_fpsSlider->setFixedWidth(100);
    m_fpsLbl = new QLabel("5");
    m_fpsLbl->setFixedWidth(24);
    m_fpsLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fpsRow->addWidget(m_fpsSlider);
    fpsRow->addWidget(m_fpsLbl);
    pvCols->addLayout(fpsRow);

    // Quality
    auto *qRow = new QHBoxLayout;
    qRow->addWidget(new QLabel("Quality:"));
    m_qualitySlider = new QSlider(Qt::Horizontal);
    m_qualitySlider->setRange(1, 100);
    m_qualitySlider->setValue(75);
    m_qualitySlider->setFixedWidth(100);
    m_qualityLbl = new QLabel("75%");
    m_qualityLbl->setFixedWidth(34);
    m_qualityLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    qRow->addWidget(m_qualitySlider);
    qRow->addWidget(m_qualityLbl);
    pvCols->addLayout(qRow);

    m_previewStatus = new QLabel("Preview disabled");
    m_previewStatus->setStyleSheet("color:#888;font-size:10px;");

    pvLayout->addWidget(m_previewEnable);
    pvLayout->addLayout(pvCols);
    pvLayout->addWidget(m_previewStatus, 1);

    rootV->addWidget(pvGroup);

    // ── Connections ───────────────────────────────────────────────
    connect(m_enableCheck,   &QCheckBox::toggled,    this, &WsTab::onEnableToggled);
    connect(m_applyBtn,      &QPushButton::clicked,  this, &WsTab::onApply);
    connect(m_previewEnable, &QCheckBox::toggled,    this, &WsTab::onPreviewEnable);
    connect(m_fpsSlider,     &QSlider::valueChanged, this, &WsTab::onFpsChanged);
    connect(m_qualitySlider, &QSlider::valueChanged, this, &WsTab::onQualityChanged);
    connect(m_sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WsTab::onSizeChanged);
}

// ── Status / QR ──────────────────────────────────────────────────

void WsTab::updateStatus()
{
    auto *srv    = lyricsWsServer();
    bool running = srv && srv->isRunning();

    if (running) {
        m_statusLbl->setText(
            QString::fromUtf8("● Running on port %1").arg(srv->port()));
        m_statusLbl->setStyleSheet("color:#44ff88;font-size:10px;font-weight:bold;");

        QString ip  = bestLocalIpAddress();
        QString url = QString("http://%1:%2").arg(ip).arg(srv->port());
        m_urlLbl->setText(
            QString("<b>%1</b> <a href='%1'>(copy)</a>").arg(url));
        m_urlLbl->setVisible(true);

        QImage img = makeQrImage(url, 160);
        if (!img.isNull())
            m_qrLbl->setPixmap(QPixmap::fromImage(img));
        m_qrLbl->setVisible(true);
        m_showQrCheck->setVisible(true);

        if (m_tabActive && m_showQrCheck->isChecked())
            pushQrToObs(true);
    } else {
        m_statusLbl->setText(QString::fromUtf8("○ Stopped"));
        m_statusLbl->setStyleSheet("color:#888;font-size:10px;");
        m_urlLbl->setVisible(false);
        m_qrLbl->setVisible(false);
        m_showQrCheck->setVisible(false);
        pushQrToObs(false);
    }
}

void WsTab::pushQrToObs(bool show)
{
    if (!show) { lyrics_renderer_qr_hide(); return; }
    auto *srv = lyricsWsServer();
    if (!srv || !srv->isRunning()) { lyrics_renderer_qr_hide(); return; }
    QString ip   = bestLocalIpAddress();
    QString url  = QString("http://%1:%2").arg(ip).arg(srv->port());
    QString path = saveQrPng(url);
    if (!path.isEmpty()) lyrics_renderer_qr_show(path);
    else                 lyrics_renderer_qr_hide();
}

void WsTab::onTabActivated()
{
    m_tabActive = true;
    if (m_showQrCheck->isChecked()) pushQrToObs(true);
}

void WsTab::onTabDeactivated()
{
    m_tabActive = false;
    pushQrToObs(false);
}

void WsTab::onShowQrToggled(bool checked)
{
    if (m_tabActive) pushQrToObs(checked);
    saveSettings();
}

void WsTab::onRefreshClients()
{
    updateStatus();
    m_clientList->clear();
    auto *srv = lyricsWsServer();
    if (!srv || !srv->isRunning()) {
        m_clientList->addItem("(server not running)");
        return;
    }
    auto clients = srv->connectedClients();
    if (clients.isEmpty()) {
        m_clientList->addItem("No clients connected");
        return;
    }
    for (const auto &c : clients) {
        QString icon = (c.role == "manager")
            ? QString::fromUtf8("\U0001F3DB  Manager")
            : (c.role == "viewer")
            ? QString::fromUtf8("\U0001F441  Viewer")
            : QString::fromUtf8("⏳  Selecting role");
        m_clientList->addItem(QString("%1   %2").arg(icon).arg(c.address));
    }
}

void WsTab::onEnableToggled(bool checked)
{
    auto *srv = lyricsWsServer();
    if (!srv) return;
    if (checked) srv->start((quint16)m_portSpin->value());
    else         srv->stop();
    saveSettings();
    updateStatus();
}

void WsTab::onApply()
{
    auto *srv = lyricsWsServer();
    if (!srv) return;
    if (m_enableCheck->isChecked()) {
        srv->stop();
        srv->start((quint16)m_portSpin->value());
    }
    srv->setPin(m_pinEdit->text().trimmed());
    saveSettings();
    updateStatus();
}

// ── Persistence ───────────────────────────────────────────────────

QString WsTab::settingsPath() const
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/settings.json";
}

void WsTab::saveSettings()
{
    QJsonObject obj;
    obj["wsEnabled"]      = m_enableCheck->isChecked();
    obj["wsPort"]         = m_portSpin->value();
    obj["wsPin"]          = m_pinEdit->text().trimmed();
    obj["wsShowQr"]       = m_showQrCheck->isChecked();
    obj["previewEnabled"] = m_previewEnable->isChecked();
    obj["previewFps"]     = m_fpsSlider->value();
    obj["previewQuality"] = m_qualitySlider->value();
    obj["previewSize"]    = m_sizeCombo->currentIndex();
    QFile f(settingsPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
    auto *srv = lyricsWsServer();
    if (srv) srv->setPin(m_pinEdit->text().trimmed());
}

void WsTab::loadSettings()
{
    QFile f(settingsPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    auto obj = doc.object();

    m_portSpin->blockSignals(true);
    m_enableCheck->blockSignals(true);
    m_showQrCheck->blockSignals(true);
    m_previewEnable->blockSignals(true);
    m_fpsSlider->blockSignals(true);
    m_qualitySlider->blockSignals(true);
    m_sizeCombo->blockSignals(true);

    m_portSpin->setValue(obj["wsPort"].toInt(8765));
    m_enableCheck->setChecked(obj["wsEnabled"].toBool(false));
    m_pinEdit->setText(obj["wsPin"].toString());
    m_showQrCheck->setChecked(obj["wsShowQr"].toBool(false));

    int fps     = obj["previewFps"].toInt(5);
    int quality = obj["previewQuality"].toInt(75);
    int szIdx   = obj["previewSize"].toInt(0);
    m_fpsSlider->setValue(fps);
    m_qualitySlider->setValue(quality);
    m_sizeCombo->setCurrentIndex(szIdx);
    m_fpsLbl->setText(QString::number(fps));
    m_qualityLbl->setText(QString("%1%").arg(quality));
    m_captureW = (szIdx == 1) ? 854  : 1280;
    m_captureH = (szIdx == 1) ? 480  : 720;
    m_captureQ = quality;

    m_portSpin->blockSignals(false);
    m_enableCheck->blockSignals(false);
    m_showQrCheck->blockSignals(false);
    m_fpsSlider->blockSignals(false);
    m_qualitySlider->blockSignals(false);
    m_sizeCombo->blockSignals(false);

    bool prevEnabled = obj["previewEnabled"].toBool(false);
    m_previewEnable->blockSignals(false);
    m_previewEnable->setChecked(prevEnabled);
    if (prevEnabled) onPreviewEnable(true);

    updateStatus();
}
