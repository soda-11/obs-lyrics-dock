// ws-tab.cpp — WebSocket settings tab  v1.1
#include "ws-tab.hpp"
#include "websocket-server.hpp"
#include "lyrics-renderer.hpp"
#include "qrcodegen.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPixmap>
#include <QImage>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>
#include <QLabel>

// Declared in websocket-server.cpp
QString bestLocalIpAddress();

// ── QR helpers ─────────────────────────────────────────────────

// Build a QR image with a 4-module quiet zone included.
// pixelSize is the output pixel dimensions of the final image.
static QImage makeQrImage(const QString &text, int pixelSize)
{
    try {
        auto grid = qrcodegen::encode(text.toStdString());
        int  n    = (int)grid.size();
        if (n == 0) return {};

        const int border = 4;          // QR spec: 4-module quiet zone
        int total = n + 2 * border;

        QImage raw(total, total, QImage::Format_RGB32);
        raw.fill(0xFFFFFFFFu);         // white background (quiet zone)

        for (int r = 0; r < n; r++)
            for (int c = 0; c < n; c++)
                raw.setPixel(c + border, r + border,
                             grid[r][c] ? 0xFF000000u : 0xFFFFFFFFu);

        // Scale up with nearest-neighbour (no blurring on QR modules)
        return raw.scaled(pixelSize, pixelSize,
                          Qt::KeepAspectRatio,
                          Qt::FastTransformation);
    } catch (...) {
        return {};
    }
}

// Save a 200×200 QR PNG to a temp file; returns the path.
// 200 px on a 1280-wide OBS canvas is readable but not overwhelming.
static QString saveQrPng(const QString &url)
{
    QImage img = makeQrImage(url, 200);
    if (img.isNull()) return {};

    QString dir  = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString path = dir + "/obs_lyrics_qr.png";
    img.save(path, "PNG");
    return path;
}

// ── WsTab ──────────────────────────────────────────────────────

WsTab::WsTab(QWidget *parent) : QWidget(parent)
{
    buildUi();
    loadSettings();
    updateStatus();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &WsTab::onRefreshClients);
    m_refreshTimer->start();
}

void WsTab::buildUi()
{
    // Top-level: two group boxes side by side
    auto *rootH = new QHBoxLayout(this);
    rootH->setContentsMargins(6, 6, 6, 6);
    rootH->setSpacing(8);

    // ── LEFT: WebSocket Remote Control ────────────────────────
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
    m_urlLbl->setStyleSheet(
        "color:#4da6ff;font-size:10px;font-family:monospace;");
    m_urlLbl->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_urlLbl->setWordWrap(true);
    m_urlLbl->setCursor(Qt::PointingHandCursor);
    connect(m_urlLbl, &QLabel::linkActivated, this, [](const QString &url){
        QApplication::clipboard()->setText(url);
    });

    m_showQrCheck = new QCheckBox("Show QR on OBS preview");
    m_showQrCheck->setToolTip(
        "Overlay the QR code at the bottom-right of the OBS canvas\n"
        "while this tab is active.");
    connect(m_showQrCheck, &QCheckBox::toggled, this, &WsTab::onShowQrToggled);

    wsLayout->addWidget(m_enableCheck);
    wsLayout->addLayout(portRow);
    wsLayout->addLayout(pinRow);
    wsLayout->addWidget(m_statusLbl);
    wsLayout->addWidget(m_urlLbl);
    wsLayout->addWidget(m_showQrCheck);
    wsLayout->addStretch();

    // ── MIDDLE: QR Code preview ───────────────────────────────
    auto *qrGroup  = new QGroupBox("QR Code");
    auto *qrLayout = new QVBoxLayout(qrGroup);
    m_qrLbl = new QLabel;
    m_qrLbl->setAlignment(Qt::AlignCenter);
    m_qrLbl->setFixedSize(160, 160);
    m_qrLbl->setStyleSheet("background:white;border:1px solid #444;");
    m_qrLbl->setToolTip("Scan to open the remote page on your phone");
    qrLayout->addWidget(m_qrLbl, 0, Qt::AlignCenter);
    qrLayout->addStretch();

    // ── RIGHT: Connected Clients ──────────────────────────────
    auto *clientGroup  = new QGroupBox("Connected Clients");
    auto *clientLayout = new QVBoxLayout(clientGroup);
    m_clientList = new QListWidget;
    m_clientList->setStyleSheet(
        "QListWidget{background:#1a1a1a;color:#ccc;font-size:10px;}");
    clientLayout->addWidget(m_clientList);

    rootH->addWidget(wsGroup, 1);
    rootH->addWidget(qrGroup, 1);
    rootH->addWidget(clientGroup, 1);

    connect(m_enableCheck, &QCheckBox::toggled, this, &WsTab::onEnableToggled);
    connect(m_applyBtn,    &QPushButton::clicked, this, &WsTab::onApply);
}

void WsTab::updateStatus()
{
    auto *srv    = lyricsWsServer();
    bool running = srv && srv->isRunning();

    if (running) {
        m_statusLbl->setText(
            QString::fromUtf8("● Running on port %1").arg(srv->port()));
        m_statusLbl->setStyleSheet(
            "color:#44ff88;font-size:10px;font-weight:bold;");

        QString ip  = bestLocalIpAddress();
        QString url = QString("http://%1:%2").arg(ip).arg(srv->port());
        m_urlLbl->setText(
            QString("<b>%1</b> <a href='%1'>(copy)</a>").arg(url));
        m_urlLbl->setVisible(true);

        // QR preview in the tab (160 px, with quiet zone)
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

// ── Persistence ───────────────────────────────────────────────

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
    obj["wsEnabled"] = m_enableCheck->isChecked();
    obj["wsPort"]    = m_portSpin->value();
    obj["wsPin"]     = m_pinEdit->text().trimmed();
    obj["wsShowQr"]  = m_showQrCheck->isChecked();
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

    m_portSpin->setValue(obj["wsPort"].toInt(8765));
    m_enableCheck->setChecked(obj["wsEnabled"].toBool(false));
    m_pinEdit->setText(obj["wsPin"].toString());
    m_showQrCheck->setChecked(obj["wsShowQr"].toBool(false));

    m_portSpin->blockSignals(false);
    m_enableCheck->blockSignals(false);
    m_showQrCheck->blockSignals(false);

    updateStatus();
}
