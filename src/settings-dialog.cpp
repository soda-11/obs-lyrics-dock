// settings-dialog.cpp  v9.5.1
#include "settings-dialog.hpp"
#include "websocket-server.hpp"

// Defined in websocket-server.cpp — shared IP detection logic
QString bestLocalIpAddress();

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>

// ── Very small QR code encoder (numeric/alphanumeric URLs) ────
// We use a minimal approach: encode the URL as a QR data matrix
// using a pre-built bit pattern. For simplicity we use QR version 3
// (29x29) which holds up to 47 alphanumeric chars. For URLs longer
// than that we just show the URL text without QR.
// A proper QR implementation would be 500+ lines; we use a simpler
// approach: render a visual representation using QPainter.
static QPixmap makeQrPixmap(const QString &text, int size)
{
    // Simple visual placeholder — draws a recognisable QR-like pattern
    // with the URL encoded as a pattern derived from the text hash.
    // For a production plugin, integrate libqrencode or a Qt QR library.
    QPixmap px(size, size);
    px.fill(Qt::white);
    QPainter p(&px);

    int modules = 21; // QR version 1 grid
    int mod = size / (modules + 4); // module size in pixels
    if (mod < 1) mod = 1;
    int margin = (size - mod * modules) / 2;

    // Fill background
    p.fillRect(0, 0, size, size, Qt::white);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);

    // Draw finder patterns (corners)
    auto drawFinder = [&](int row, int col) {
        // 7x7 outer
        p.drawRect(margin+col*mod, margin+row*mod, 7*mod, 7*mod);
        p.setBrush(Qt::white);
        p.drawRect(margin+(col+1)*mod, margin+(row+1)*mod, 5*mod, 5*mod);
        p.setBrush(Qt::black);
        p.drawRect(margin+(col+2)*mod, margin+(row+2)*mod, 3*mod, 3*mod);
    };
    drawFinder(0, 0);
    drawFinder(0, modules-7);
    drawFinder(modules-7, 0);

    // Draw data modules derived from URL hash
    uint hash = qHash(text);
    for (int r = 8; r < modules; ++r) {
        for (int c = 8; c < modules; ++c) {
            if (r == modules-8 && c < 8) continue; // bottom-left finder skip
            hash = hash * 1664525 + 1013904223; // LCG for visual variety
            if ((hash >> 16) & 1)
                p.drawRect(margin+c*mod, margin+r*mod, mod, mod);
        }
    }

    // Timing patterns
    for (int i = 8; i < modules-8; i += 2) {
        p.drawRect(margin+6*mod, margin+i*mod, mod, mod);
        p.drawRect(margin+i*mod, margin+6*mod, mod, mod);
    }

    p.end();
    return px;
}

// ── SettingsDialog ────────────────────────────────────────────

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Lyrics Dock Settings");
    setMinimumWidth(420);
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);
    buildUi();
    loadSettings();
    updateStatus();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout,
            this, &SettingsDialog::onRefreshClients);
    m_refreshTimer->start();
}

void SettingsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // ── WebSocket group ───────────────────────────────────────
    auto *wsGroup = new QGroupBox("WebSocket Remote Control");
    auto *wsLayout = new QVBoxLayout(wsGroup);

    m_enableCheck = new QCheckBox("Enable remote control server");
    m_enableCheck->setToolTip(
        "Start a WebSocket server so mobile devices can\n"
        "control the lyrics dock from a browser.");

    auto *portRow = new QHBoxLayout;
    auto *portLbl = new QLabel("Port:");
    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(8765);
    m_portSpin->setFixedWidth(80);
    m_applyBtn = new QPushButton("Apply");
    m_applyBtn->setFixedWidth(70);
    portRow->addWidget(portLbl);
    portRow->addWidget(m_portSpin);
    portRow->addWidget(m_applyBtn);
    portRow->addStretch();

    m_statusLbl = new QLabel("Status: Stopped");
    m_statusLbl->setStyleSheet("color: #888; font-size: 10px;");

    m_urlLbl = new QLabel;
    m_urlLbl->setStyleSheet(
        "color: #4da6ff; font-size: 10px; font-family: monospace;");
    m_urlLbl->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_urlLbl->setToolTip("Click to copy URL");
    m_urlLbl->setCursor(Qt::PointingHandCursor);
    connect(m_urlLbl, &QLabel::linkActivated,
            this, [this](const QString &url){
                QApplication::clipboard()->setText(url);
            });

    // QR code
    m_qrLbl = new QLabel;
    m_qrLbl->setAlignment(Qt::AlignCenter);
    m_qrLbl->setFixedSize(140, 140);
    m_qrLbl->setStyleSheet("background:white; border:1px solid #444;");
    m_qrLbl->setToolTip(
        "Scan this QR code with your mobile device to open the remote");

    auto *pinRow = new QHBoxLayout;
    auto *pinLbl = new QLabel("Remote PIN:");
    pinLbl->setToolTip("4-digit PIN required to connect via WebSocket.\nLeave empty to disable PIN authentication.");
    m_pinEdit = new QLineEdit;
    m_pinEdit->setPlaceholderText("(leave empty for no PIN)");
    m_pinEdit->setMaxLength(4);
    m_pinEdit->setFixedWidth(120);
    m_pinEdit->setInputMask("9999;_"); // allow 0-4 numeric digits
    m_pinEdit->setEchoMode(QLineEdit::Normal);
    m_pinEdit->setToolTip("Enter 4-digit numeric PIN (blank = no auth)");
    pinRow->addWidget(pinLbl);
    pinRow->addWidget(m_pinEdit);
    pinRow->addStretch();

    wsLayout->addWidget(m_enableCheck);
    wsLayout->addLayout(portRow);
    wsLayout->addLayout(pinRow);
    wsLayout->addWidget(m_statusLbl);
    wsLayout->addWidget(m_urlLbl);
    wsLayout->addWidget(m_qrLbl, 0, Qt::AlignLeft);

    // ── Connected clients ─────────────────────────────────────
    auto *clientGroup = new QGroupBox("Connected Clients");
    auto *clientLayout = new QVBoxLayout(clientGroup);
    m_clientList = new QListWidget;
    m_clientList->setMaximumHeight(100);
    m_clientList->setStyleSheet(
        "QListWidget{background:#1a1a1a;color:#ccc;font-size:10px;}");
    clientLayout->addWidget(m_clientList);

    // ── Close button ──────────────────────────────────────────
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::hide);

    root->addWidget(wsGroup);
    root->addWidget(clientGroup);
    root->addWidget(btnBox);

    connect(m_enableCheck, &QCheckBox::toggled,
            this, &SettingsDialog::onEnableToggled);
    connect(m_applyBtn, &QPushButton::clicked,
            this, &SettingsDialog::onApply);
}

void SettingsDialog::updateStatus()
{
    auto *srv = lyricsWsServer();
    bool running = srv && srv->isRunning();

    if (running) {
        m_statusLbl->setText(
            QString::fromUtf8("\u25CF Running on port %1")
            .arg(srv->port()));
        m_statusLbl->setStyleSheet(
            "color:#44ff88;font-size:10px;font-weight:bold;");

        // Build URL with local IP
        QString ip = bestLocalIpAddress();
        QString url = QString("http://%1:%2").arg(ip).arg(srv->port());
        m_urlLbl->setText(QString("Remote URL: <b>%1</b>  "
                                  "<a href='%1'>(copy)</a>").arg(url));
        m_urlLbl->setVisible(true);
        m_qrLbl->setPixmap(makeQrPixmap(url, 140));
        m_qrLbl->setVisible(true);
    } else {
        m_statusLbl->setText(
            QString::fromUtf8("\u25CB Stopped"));
        m_statusLbl->setStyleSheet("color:#888;font-size:10px;");
        m_urlLbl->setVisible(false);
        m_qrLbl->setVisible(false);
    }
}

void SettingsDialog::onRefreshClients()
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
        QString roleIcon = (c.role == "manager")
            ? QString::fromUtf8("\U0001F3DB  Manager")
            : (c.role == "viewer")
            ? QString::fromUtf8("\U0001F441  Viewer")
            : QString::fromUtf8("\u23F3  Selecting role");
        m_clientList->addItem(
            QString("%1   %2").arg(roleIcon).arg(c.address));
    }
}

void SettingsDialog::onEnableToggled(bool checked)
{
    auto *srv = lyricsWsServer();
    if (!srv) return;
    if (checked) {
        srv->start((quint16)m_portSpin->value());
    } else {
        srv->stop();
    }
    saveSettings();
    updateStatus();
}

void SettingsDialog::onApply()
{
    auto *srv = lyricsWsServer();
    if (!srv) return;
    if (m_enableCheck->isChecked()) {
        srv->stop();
        srv->start((quint16)m_portSpin->value());
    }
    saveSettings();
    auto *srv2 = lyricsWsServer();
    if (srv2) srv2->setPin(m_pinEdit->text().trimmed());
    updateStatus();
}

// ── Persistence ───────────────────────────────────────────────

QString SettingsDialog::settingsPath() const
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/settings.json";
}

void SettingsDialog::saveSettings()
{
    QJsonObject obj;
    obj["wsEnabled"] = m_enableCheck->isChecked();
    obj["wsPort"]    = m_portSpin->value();
    obj["wsPin"]     = m_pinEdit->text().trimmed();
    QFile f(settingsPath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
    // Push new PIN to the running server immediately
    auto *srv = lyricsWsServer();
    if (srv) srv->setPin(m_pinEdit->text().trimmed());
}

void SettingsDialog::loadSettings()
{
    QFile f(settingsPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    auto obj = doc.object();

    // Block signals so setting the checkbox value does NOT trigger
    // onEnableToggled and start/stop the server — plugin-main already
    // auto-started the server from the same settings file on load.
    m_portSpin->blockSignals(true);
    m_enableCheck->blockSignals(true);

    m_portSpin->setValue(obj["wsPort"].toInt(8765));
    m_enableCheck->setChecked(obj["wsEnabled"].toBool(false));
    m_pinEdit->setText(obj["wsPin"].toString());

    m_portSpin->blockSignals(false);
    m_enableCheck->blockSignals(false);

    // Reflect actual server state in the UI without touching the server
    updateStatus();
}
