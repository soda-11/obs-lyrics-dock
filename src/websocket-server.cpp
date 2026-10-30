// websocket-server.cpp  v9.5.1.1
// Fixes:
//   1. Frame parser: payload always starts at hdrLen (was reading mask as payload)
//   2. Ping/pong: respond to opcode 0x9 with pong 0xA — keeps browser alive
//   3. State not sent before setRole — removed premature send after upgrade
//   4. broadcastMediaList: sends gallery file list to new manager connections
//   5. stop(): abort() for immediate close, signals disconnected before abort

#include "websocket-server.hpp"
#include "lyrics-dock.hpp"
#include "song-manager.hpp"
#include "lyrics-renderer.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QUrl>
#include <QRegularExpression>
#include <QMetaObject>
#include <QApplication>

// ── Global instance ───────────────────────────────────────────
static WebSocketServer *s_server = nullptr;
WebSocketServer *lyricsWsServer()          { return s_server; }
void setLyricsWsServer(WebSocketServer *s) { s_server = s; }

// ── Constructor / destructor ──────────────────────────────────

WebSocketServer::WebSocketServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &WebSocketServer::onNewConnection);

    m_mvTimeTimer = new QTimer(this);
    m_mvTimeTimer->setInterval(500);
    connect(m_mvTimeTimer, &QTimer::timeout, this, &WebSocketServer::onMvTimeTick);
}

WebSocketServer::~WebSocketServer()
{
    // stop() should have been called explicitly before destruction.
    // If somehow not called yet, do a final cleanup.
    if (m_server->isListening() || !m_clients.isEmpty())
        stop();
    // m_server is a child QObject — deleted automatically by Qt parent system
}

// ── Start / stop ──────────────────────────────────────────────

bool WebSocketServer::start(quint16 port)
{
    stop();
    m_port = port;
    if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
        blog(LOG_WARNING, "[lyrics-ws] Failed to listen on port %d: %s",
             (int)port,
             m_server->errorString().toUtf8().constData());
        return false;
    }
    blog(LOG_INFO, "[lyrics-ws] Listening on port %d", (int)port);

    // Send a WebSocket ping frame to every connected client every 20 seconds.
    // Mobile OS and NAT routers drop idle TCP connections quickly; this keeps
    // the connection alive even when no slides are being pushed.
    if (!m_pingTimer) {
        m_pingTimer = new QTimer(this);
        connect(m_pingTimer, &QTimer::timeout,
                this, &WebSocketServer::sendPingAll);
    }
    m_pingTimer->start(20000); // 20 s
    return true;
}

void WebSocketServer::stop()
{
    if (m_pingTimer) m_pingTimer->stop();

    // Stop accepting new connections first
    m_server->close();

    // Disconnect all signals BEFORE abort() so onDisconnected()
    // is not fired while we are iterating/clearing m_clients.
    // Use delete (not deleteLater) so there are no deferred-deletion
    // objects pending when obs_module_unload() destroys this object.
    for (auto *c : m_clients) {
        c->socket->disconnect();  // disconnect ALL signals from this socket
        c->socket->abort();       // immediate close, no async tail
        delete c->socket;         // delete now — signals are disconnected, safe
        delete c;
    }
    m_clients.clear();
}

bool WebSocketServer::isRunning() const
{
    return m_server->isListening();
}

// ── New connection ────────────────────────────────────────────

void WebSocketServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        auto *client     = new WsClient;
        client->socket   = sock;
        m_clients << client;

        connect(sock, &QTcpSocket::readyRead,
                this, &WebSocketServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected,
                this, &WebSocketServer::onDisconnected);

        blog(LOG_INFO, "[lyrics-ws] Client connected: %s",
             sock->peerAddress().toString().toUtf8().constData());
        emit clientCountChanged(m_clients.size());
    }
}

void WebSocketServer::onDisconnected()
{
    auto *sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    for (int i = m_clients.size()-1; i >= 0; --i) {
        if (m_clients[i]->socket == sock) {
            delete m_clients[i];
            m_clients.removeAt(i);
            break;
        }
    }
    sock->deleteLater();
    emit clientCountChanged(m_clients.size());
}

// ── Read & frame parsing ──────────────────────────────────────

void WebSocketServer::onReadyRead()
{
    auto *sock = qobject_cast<QTcpSocket*>(sender());
    WsClient *client = nullptr;
    for (auto *c : m_clients)
        if (c->socket == sock) { client = c; break; }
    if (!client) return;

    client->rxBuf += sock->readAll();

    if (!client->upgraded) {
        handleHttpOrWs(client, client->rxBuf);
        return;
    }

    // Process all complete WebSocket frames in the buffer
    while (client->rxBuf.size() >= 2) {
        uint8_t b0     = (uint8_t)client->rxBuf[0];
        uint8_t b1     = (uint8_t)client->rxBuf[1];
        bool    masked = (b1 & 0x80) != 0;
        int     payLen = b1 & 0x7F;

        // Header size: 2 base + 2 or 8 extended length + 4 mask
        int extLen = (payLen == 126) ? 2 : (payLen == 127) ? 8 : 0;
        int hdrLen = 2 + extLen + (masked ? 4 : 0);

        if (client->rxBuf.size() < hdrLen) break; // wait for more data

        // Parse extended payload length
        if (payLen == 126) {
            payLen = ((uint8_t)client->rxBuf[2] << 8) |
                      (uint8_t)client->rxBuf[3];
        } else if (payLen == 127) {
            // 64-bit length — not needed for our use case
            // skip this frame
            client->rxBuf.clear();
            break;
        }

        int totalLen = hdrLen + payLen;
        if (client->rxBuf.size() < totalLen) break; // wait for more data

        // ── Extract and unmask payload ────────────────────────
        // Payload always starts at offset hdrLen (after header + mask bytes)
        QByteArray payload = client->rxBuf.mid(hdrLen, payLen);

        if (masked && payLen > 0) {
            // Mask bytes are the 4 bytes immediately before the payload
            int maskOffset = hdrLen - 4;
            uint8_t mask[4];
            for (int i = 0; i < 4; ++i)
                mask[i] = (uint8_t)client->rxBuf[maskOffset + i];
            for (int i = 0; i < payLen; ++i)
                payload[i] = payload[i] ^ (char)mask[i % 4];
        }

        uint8_t opcode = b0 & 0x0F;

        switch (opcode) {
        case 0x0: // Continuation
        case 0x1: // Text
        case 0x2: // Binary
            handleWsFrame(client, payload);
            break;
        case 0x8: // Close
            client->socket->disconnectFromHost();
            return;
        case 0x9: // Ping → reply with Pong
            sendPong(client->socket, payload);
            break;
        case 0xA: // Pong — nothing to do
            break;
        default:
            break;
        }

        client->rxBuf.remove(0, totalLen);
    }
}

// ── HTTP / WebSocket upgrade ──────────────────────────────────

void WebSocketServer::handleHttpOrWs(WsClient *client,
                                      const QByteArray &data)
{
    QString req = QString::fromUtf8(data);

    // Must have the complete HTTP request before processing
    if (!req.contains("\r\n\r\n")) return;

    // ── WebSocket upgrade — check FIRST before HTML check ────
    // Both the HTML request and the WS upgrade start with "GET /"
    // so we must check for the Upgrade header first.
    if (req.contains("Upgrade: websocket") ||
        req.contains("upgrade: websocket")) {

        // Extract Sec-WebSocket-Key
        QString key;
        for (const auto &line : req.split("\r\n")) {
            if (line.startsWith("Sec-WebSocket-Key:"))
                key = line.mid(18).trimmed();
        }
        if (key.isEmpty()) {
            blog(LOG_WARNING, "[lyrics-ws] WS upgrade missing key");
            client->socket->disconnectFromHost();
            return;
        }

        // Compute accept key (RFC 6455)
        QByteArray accept = QCryptographicHash::hash(
            (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").toUtf8(),
            QCryptographicHash::Sha1).toBase64();

        // ── Enforce max 2 WebSocket connections per IP ────────
        QHostAddress peer = client->socket->peerAddress();
        // Normalize IPv4-mapped IPv6 (e.g. ::ffff:127.0.0.1 → 127.0.0.1)
        if (peer.protocol() == QAbstractSocket::IPv6Protocol) {
            bool ok = false;
            QHostAddress v4(peer.toIPv4Address(&ok));
            if (ok && !v4.isNull()) peer = v4;
        }
        int wsCount = 0;
        for (auto *c : m_clients) {
            if (!c->upgraded || c == client) continue;
            QHostAddress cp = c->socket->peerAddress();
            if (cp.protocol() == QAbstractSocket::IPv6Protocol) {
                bool ok = false; QHostAddress v4(cp.toIPv4Address(&ok));
                if (ok && !v4.isNull()) cp = v4;
            }
            if (cp == peer) wsCount++;
        }
        if (wsCount >= 4) {
            blog(LOG_WARNING,
                 "[lyrics-ws] Rejected WS from %s — max 4 per IP",
                 peer.toString().toUtf8().constData());
            client->socket->write(
                "HTTP/1.1 429 Too Many Requests\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n");
            client->socket->disconnectFromHost();
            return;
        }

        QByteArray resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

        client->socket->write(resp);
        client->upgraded = true;
        client->rxBuf.clear();

        // Always notify the client of auth state so it knows when to show
        // the PIN screen vs. the role selection screen.
        if (m_pin.isEmpty()) {
            client->authed = true;
            QJsonObject authOk;
            authOk["type"]   = "auth";
            authOk["result"] = "ok";
            sendJson(client, authOk);
        } else {
            // Ask the client to provide the PIN
            QJsonObject challenge;
            challenge["type"]   = "auth";
            challenge["result"] = "required";
            sendJson(client, challenge);
        }

        blog(LOG_INFO, "[lyrics-ws] WebSocket upgraded: %s",
             client->socket->peerAddress().toString().toUtf8().constData());

        // Do NOT send state here — wait for client to send setRole
        return;
    }

    // ── Serve media thumbnails: GET /thumb?p=<url-encoded-path> ─
    // Supports images (direct) and videos (Range requests for first-frame).
    if (req.startsWith("GET /thumb?")) {
        QString firstLine = req.left(req.indexOf("\r\n"));
        int qPos  = firstLine.indexOf('?');
        int spPos = firstLine.lastIndexOf(' ');
        QString filePath;
        if (qPos > 0 && spPos > qPos) {
            for (const auto &part : firstLine.mid(qPos + 1, spPos - qPos - 1).split('&')) {
                if (part.startsWith("p=")) {
                    filePath = QUrl::fromPercentEncoding(part.mid(2).toUtf8());
                    break;
                }
            }
        }

        static QRegularExpression kImgExt(
            R"(\.(jpe?g|png|gif|bmp|webp|tiff?)$)",
            QRegularExpression::CaseInsensitiveOption);
        static QRegularExpression kVidExt(
            R"(\.(mp4|m4v|mov|avi|mkv|webm|flv|wmv)$)",
            QRegularExpression::CaseInsensitiveOption);

        bool isImg = kImgExt.match(filePath).hasMatch();
        bool isVid = kVidExt.match(filePath).hasMatch();

        QFile f(filePath);
        if (!filePath.isEmpty() && (isImg || isVid) && f.open(QIODevice::ReadOnly)) {
            qint64 fileSize = f.size();

            // Parse Range header (browsers send this for video preload)
            qint64 rangeStart = 0, rangeEnd = fileSize - 1;
            bool hasRange = false;
            static QRegularExpression kRangeRe(
                R"(Range:\s*bytes=(\d+)-(\d*))",
                QRegularExpression::CaseInsensitiveOption);
            auto rm = kRangeRe.match(req);
            if (rm.hasMatch()) {
                hasRange    = true;
                rangeStart  = rm.captured(1).toLongLong();
                QString end = rm.captured(2);
                rangeEnd    = end.isEmpty()
                              ? fileSize - 1
                              : qMin(end.toLongLong(), fileSize - 1);
            }
            qint64 rangeLen = qMax(0LL, rangeEnd - rangeStart + 1);

            f.seek(rangeStart);
            QByteArray data = f.read(rangeLen);

            // MIME type
            QString ext = QFileInfo(filePath).suffix().toLower();
            QByteArray mime = "application/octet-stream";
            if      (ext=="jpg"||ext=="jpeg") mime="image/jpeg";
            else if (ext=="png")   mime="image/png";
            else if (ext=="gif")   mime="image/gif";
            else if (ext=="webp")  mime="image/webp";
            else if (ext=="bmp")   mime="image/bmp";
            else if (ext=="mp4"||ext=="m4v") mime="video/mp4";
            else if (ext=="mov")   mime="video/quicktime";
            else if (ext=="avi")   mime="video/x-msvideo";
            else if (ext=="mkv")   mime="video/x-matroska";
            else if (ext=="webm")  mime="video/webm";
            else if (ext=="flv")   mime="video/x-flv";
            else if (ext=="wmv")   mime="video/x-ms-wmv";

            QByteArray hdr;
            if (hasRange) {
                hdr = "HTTP/1.1 206 Partial Content\r\n"
                      "Content-Range: bytes "
                      + QByteArray::number(rangeStart) + "-"
                      + QByteArray::number(rangeEnd) + "/"
                      + QByteArray::number(fileSize) + "\r\n";
            } else {
                hdr = "HTTP/1.1 200 OK\r\n"
                      "Cache-Control: max-age=3600\r\n";
            }
            hdr += "Content-Type: "   + mime + "\r\n"
                   "Content-Length: " + QByteArray::number(data.size()) + "\r\n"
                   "Accept-Ranges: bytes\r\n"
                   "Connection: close\r\n\r\n";
            client->socket->write(hdr + data);
        } else {
            client->socket->write(
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n");
        }
        client->socket->disconnectFromHost();
        return;
    }

    // ── Serve HTML page on plain HTTP GET / ──────────────────
    if (req.startsWith("GET /")) {
        QByteArray html = serveHtmlPage();
        QByteArray resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + QByteArray::number(html.size()) + "\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "Connection: close\r\n\r\n" + html;
        client->socket->write(resp);
        client->socket->disconnectFromHost();
        return;
    }

    // Unknown request — close
    client->socket->disconnectFromHost();
}

// ── Frame handling ────────────────────────────────────────────

void WebSocketServer::handleWsFrame(WsClient *client,
                                     const QByteArray &payload)
{
    auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;
    QJsonObject msg = doc.object();
    QString type = msg["type"].toString();

    // ── PIN authentication gate ───────────────────────────────
    if (!client->authed) {
        if (type == "auth") {
            if (m_pin.isEmpty() || msg["pin"].toString() == m_pin) {
                client->authed = true;
                QJsonObject ok;
                ok["type"]   = "auth";
                ok["result"] = "ok";
                sendJson(client, ok);
                // Now prompt for role
            } else {
                QJsonObject deny;
                deny["type"]   = "auth";
                deny["result"] = "denied";
                sendJson(client, deny);
                client->socket->disconnectFromHost();
            }
        } else {
            // Require auth first
            QJsonObject req;
            req["type"]   = "auth";
            req["result"] = "required";
            sendJson(client, req);
        }
        return;
    }

    // Role selection — always allowed before and after role is set
    if (type == "setRole") {
        QString role = msg["role"].toString();
        if (role == "manager" || role == "viewer" || role == "lp") {
            client->role = role;
            blog(LOG_INFO, "[lyrics-ws] Client set role: %s",
                 role.toUtf8().constData());

            // Send full state snapshot
            sendJson(client, buildStateForRole(role));

            // Send sequence-based song list to ALL roles
            // Always send — even empty array clears stale data on client
            {
                QJsonObject songMsg;
                songMsg["type"]  = "songs";
                songMsg["songs"] = m_lastSongsJson; // may be empty array
                sendJson(client, songMsg);
            }

            // Send full service order (manager + LP)
            if ((role == "manager" || role == "lp") && !m_lastServiceOrderJson.isEmpty()) {
                QJsonObject soMsg;
                soMsg["type"]  = "serviceOrder";
                soMsg["items"] = m_lastServiceOrderJson;
                sendJson(client, soMsg);
            }

            // Manager-only: media lists (per tab) and scenes
            if (role == "manager") {
                auto sendMediaList = [&](const QString &tab, const QStringList &list) {
                    QJsonArray arr;
                    for (const auto &p : list) arr.append(p);
                    QJsonObject msg2;
                    msg2["type"]  = "mediaList";
                    msg2["tab"]   = tab;
                    msg2["items"] = arr;
                    sendJson(client, msg2);
                };
                sendMediaList("lyrics",    m_lastLyricsMediaList);
                sendMediaList("paragraph",       m_lastParagraphMediaList);
                sendMediaList("scripture", m_lastScriptureMediaList);

                QJsonArray sceneArr;
                struct obs_frontend_source_list list = {};
                obs_frontend_get_scenes(&list);
                for (size_t i = 0; i < list.sources.num; ++i)
                    sceneArr.append(QString::fromUtf8(
                        obs_source_get_name(list.sources.array[i])));
                obs_frontend_source_list_free(&list);
                obs_source_t *cur = obs_frontend_get_current_scene();
                QString curName;
                if (cur) {
                    curName = QString::fromUtf8(obs_source_get_name(cur));
                    obs_source_release(cur);
                }
                QJsonObject sceneMsg;
                sceneMsg["type"]         = "scenes";
                sceneMsg["scenes"]       = sceneArr;
                sceneMsg["currentScene"] = curName;
                sendJson(client, sceneMsg);

                // MV state
                {
                    QJsonObject mvMsg;
                    mvMsg["type"] = "mvState";
                    mvMsg["mode"] = m_lastMvMode;
                    mvMsg["path"] = m_lastMvPath;
                    sendJson(client, mvMsg);
                }
                // Scripture display state (for confidence monitor)
                {
                    QJsonObject scrMsg;
                    scrMsg["type"]  = "scriptureUpdate";
                    scrMsg["title"] = m_lastScripTitle;
                    scrMsg["text"]  = m_lastScripText;
                    sendJson(client, scrMsg);
                }
                // Scripture full state (for manager slide rebuild)
                {
                    QJsonObject scrState;
                    scrState["type"]  = "scriptureState";
                    scrState["title"] = m_lastScripFullTitle;
                    scrState["text"]  = m_lastScripFullText;
                    sendJson(client, scrState);
                }
                // CTW full state (for manager slide rebuild)
                {
                    QJsonObject paragraphState;
                    paragraphState["type"]  = "paragraphState";
                    paragraphState["title"] = m_lastParagraphTitle;
                    paragraphState["text"]  = m_lastParagraphText;
                    sendJson(client, paragraphState);
                }
                // Color presets — read live from dock so new managers see current colors
                if (m_dock) {
                    QJsonObject cpMsg;
                    cpMsg["type"]    = "colorPresets";
                    cpMsg["presets"] = m_dock->colorPresetsJson();
                    cpMsg["active"]  = m_lastActivePreset;
                    sendJson(client, cpMsg);
                }
                // MV lists — read fresh from disk so we always have the
                // latest even if broadcastMvVideoList/MusicList was never called.
                {
                    auto readMvList = [](const QString &fname) -> QStringList {
                        QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation) + "/obs-lyrics-dock";
                        QFile f(dir + "/" + fname);
                        if (!f.open(QIODevice::ReadOnly)) return {};
                        auto doc = QJsonDocument::fromJson(f.readAll());
                        if (doc.isNull()) return {};
                        QStringList out;
                        for (const auto &v : doc.object()["items"].toArray())
                            out << v.toString();
                        return out;
                    };
                    m_lastMvVideoList = readMvList("mv-video.json");
                    m_lastMvMusicList = readMvList("mv-music.json");

                    QJsonArray va, ma;
                    for (const auto &p : m_lastMvVideoList) va.append(p);
                    for (const auto &p : m_lastMvMusicList) ma.append(p);
                    QJsonObject mvLists;
                    mvLists["type"]   = "mvLists";
                    mvLists["videos"] = va;
                    mvLists["music"]  = ma;
                    sendJson(client, mvLists);
                }
                // AudioBin — read fresh from audio-bin.json
                {
                    auto readAudioBin = []() -> QStringList {
                        QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation);
                        QFile f(dir + "/audio-bin.json");
                        if (!f.open(QIODevice::ReadOnly)) return {};
                        auto doc = QJsonDocument::fromJson(f.readAll());
                        if (doc.isNull()) return {};
                        QStringList out;
                        for (const auto &v : doc.object()["files"].toArray())
                            out << v.toString();
                        return out;
                    };
                    m_lastAudioBinList = readAudioBin();
                    QJsonArray aa;
                    for (const auto &p : m_lastAudioBinList) aa.append(p);
                    QJsonObject abMsg;
                    abMsg["type"]        = "audioBinList";
                    abMsg["files"]       = aa;
                    abMsg["activeIndex"] = m_lastAudioBinActive;
                    abMsg["playing"]     = m_lastAudioBinPlaying;
                    sendJson(client, abMsg);
                }
            }

            emit clientCountChanged(m_clients.size());
        }
        return;
    }

    // LP role can send a limited set of navigation commands
    if (client->role == "lp") {
        LyricsDock *dock = m_dock;
        if (type == "lpNext") {
            QString mode = msg["mode"].toString("lyr");
            QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->lpNextSlide(mode); }, Qt::QueuedConnection);
        } else if (type == "lpPrev") {
            QString mode = msg["mode"].toString("lyr");
            QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->lpPrevSlide(mode); }, Qt::QueuedConnection);
        } else if (type == "lpParagraph") {
            QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pushParagraphCurrent(); }, Qt::QueuedConnection);
        } else if (type == "lpScripture") {
            QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pushScriptureCurrent(); }, Qt::QueuedConnection);
        } else if (type == "presentationShow") {
            int presType = msg["presType"].toInt(-1);
            QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->presentationShowFromRemote(presType); }, Qt::QueuedConnection);
        } else if (type == "activateSlide") {
            int serviceIdx = msg["serviceIdx"].toInt(-1);
            int slideIdx   = msg["slideIdx"].toInt(0);
            QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->activateSlideFromRemote(serviceIdx, slideIdx); }, Qt::QueuedConnection);
        } else if (type == "getSongs") {
            // LP requests service order on connect
            if (!m_lastSongsJson.isEmpty()) {
                QJsonObject resp; resp["type"] = "songs"; resp["songs"] = m_lastSongsJson;
                sendJson(client, resp);
            }
            if (!m_lastServiceOrderJson.isEmpty()) {
                QJsonObject resp; resp["type"] = "serviceOrder"; resp["items"] = m_lastServiceOrderJson;
                sendJson(client, resp);
            }
        }
        return;
    }

    // Viewer cannot send commands
    if (client->role != "manager") {
        QJsonObject err;
        err["type"]    = "error";
        err["message"] = "viewer role cannot send commands";
        sendJson(client, err);
        return;
    }

    handleCommand(client, msg);
}

// ── Manager commands ──────────────────────────────────────────

void WebSocketServer::handleCommand(WsClient *client,
                                     const QJsonObject &msg)
{
    QString type = msg["type"].toString();

    if (type == "keepalive") return; // client heartbeat — no response needed

    if (type == "getSongs") {
        // Send cached sequence-based song list
        if (!m_lastSongsJson.isEmpty()) {
            QJsonObject resp;
            resp["type"]  = "songs";
            resp["songs"] = m_lastSongsJson;
            sendJson(client, resp);
        }
        // Send full service order (includes Paragraphs/Media/Presentation items)
        if (!m_lastServiceOrderJson.isEmpty()) {
            QJsonObject resp;
            resp["type"]  = "serviceOrder";
            resp["items"] = m_lastServiceOrderJson;
            sendJson(client, resp);
        }
        return;
    }

    if (type == "activateSlide") {
        int serviceIdx = msg["serviceIdx"].toInt(-1);
        int slideIdx   = msg["slideIdx"].toInt(-1);
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=]() {
            if (dock) dock->activateSlideFromRemote(serviceIdx, slideIdx);
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "pushSlide") {
        QString lyrics  = msg["lyrics"].toString();
        QString section = msg["section"].toString();
        QString title   = msg["title"].toString();
        int     idx     = msg["slideIndex"].toInt(-1);
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=]() {
            // Update the dock highlight AND push to renderer
            if (dock)
                dock->pushSlideFromRemote(title, idx, lyrics, section);
            else
                lyrics_renderer_push(lyrics, section, title);
            if (s_server) s_server->broadcastSlide(lyrics,section,title,idx);
            if (s_server) s_server->broadcastActiveMode("lyr");
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "clearLyrics") {
        QMetaObject::invokeMethod(qApp, [=]() {
            lyrics_renderer_clear();
            if (s_server) s_server->broadcastClear();
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "pushMedia") {
        QString path      = msg["path"].toString();
        int     fadeMs    = msg["fadeMs"].toInt(500);
        bool    stopAudio = msg["stopAudio"].toBool(false);
        QMetaObject::invokeMethod(qApp, [=]() {
            if (stopAudio) lyrics_renderer_audio_clear();
            lyrics_renderer_media_push(path, fadeMs);
            if (s_server) s_server->broadcastMediaState(path);
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "clearMedia") {
        QMetaObject::invokeMethod(qApp, [=]() {
            lyrics_renderer_media_clear();
            if (s_server) s_server->broadcastMediaState(QString());
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "getScenes") {
        QJsonArray scenes;
        struct obs_frontend_source_list list = {};
        obs_frontend_get_scenes(&list);
        for (size_t i = 0; i < list.sources.num; ++i)
            scenes.append(QString::fromUtf8(
                obs_source_get_name(list.sources.array[i])));
        obs_frontend_source_list_free(&list);
        obs_source_t *cur = obs_frontend_get_current_scene();
        QString curName;
        if (cur) {
            curName = QString::fromUtf8(obs_source_get_name(cur));
            obs_source_release(cur);
        }
        QJsonObject resp;
        resp["type"] = "scenes"; resp["scenes"] = scenes;
        resp["currentScene"] = curName;
        sendJson(client, resp);
        return;
    }

    if (type == "switchScene") {
        QString scene = msg["scene"].toString();
        QMetaObject::invokeMethod(qApp, [=]() {
            obs_source_t *src = obs_get_source_by_name(
                scene.toUtf8().constData());
            if (src) {
                obs_frontend_set_current_scene(src);
                obs_source_release(src);
                if (s_server) s_server->broadcastScene(scene);
            }
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "getMvLists") {
        QJsonObject resp;
        resp["type"] = "mvLists";
        QJsonArray va, ma;
        for (const auto &p : m_lastMvVideoList) va.append(p);
        for (const auto &p : m_lastMvMusicList) ma.append(p);
        resp["videos"] = va;
        resp["music"]  = ma;
        sendJson(client, resp);
        return;
    }

    if (type == "mvPlayVideo") {
        QString path   = msg["path"].toString();
        int     fadeMs = msg["fadeMs"].toInt(500);
        QMetaObject::invokeMethod(qApp, [=]() {
            lyrics_renderer_mv_video_push(path, fadeMs);
            if (s_server) s_server->broadcastMvState("video", path);
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "mvPlayMusic") {
        QString path = msg["path"].toString();
        QMetaObject::invokeMethod(qApp, [=]() {
            lyrics_renderer_mv_music_push(path);
            if (s_server) s_server->broadcastMvState("music", path);
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "mvStop") {
        QMetaObject::invokeMethod(qApp, [=]() {
            lyrics_renderer_mv_clear(0);
            if (s_server) s_server->broadcastMvState("", "");
        }, Qt::QueuedConnection);
        return;
    }

    // Remote manager pressed Apply — sends full title+text to OBS dock so it
    // rebuilds its slide grid identically to what the remote shows.
    if (type == "applyScripture") {
        QString title = msg["title"].toString();
        QString text  = msg["text"].toString();
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=]() {
            if (dock) dock->applyScriptureFromRemote(title, text);
            // Broadcast the full state so other connected remotes also update
            if (s_server) s_server->broadcastScriptureState(title, text);
        }, Qt::QueuedConnection);
        return;
    }

    // Remote manager clicked a slide — push that slide text to OBS
    if (type == "pushScriptureSlide") {
        QString title = msg["title"].toString();
        QString text  = msg["text"].toString();
        QMetaObject::invokeMethod(qApp, [=]() {
            lyrics_renderer_scripture_push(title, text);
            if (s_server) s_server->broadcastScripture(title, text);
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "clearScripture") {
        QMetaObject::invokeMethod(qApp, [=]() {
            lyrics_renderer_scripture_clear();
            if (s_server) s_server->broadcastScripture(QString(), QString());
        }, Qt::QueuedConnection);
        return;
    }

    if (type == "startStream")
        QMetaObject::invokeMethod(qApp,
            [](){ obs_frontend_streaming_start(); }, Qt::QueuedConnection);
    else if (type == "stopStream")
        QMetaObject::invokeMethod(qApp,
            [](){ obs_frontend_streaming_stop(); }, Qt::QueuedConnection);
    else if (type == "startRecord")
        QMetaObject::invokeMethod(qApp,
            [](){ obs_frontend_recording_start(); }, Qt::QueuedConnection);
    else if (type == "stopRecord")
        QMetaObject::invokeMethod(qApp,
            [](){ obs_frontend_recording_stop(); }, Qt::QueuedConnection);

    // ── Presentation ─────────────────────────────────────────────
    if (type == "presentationShow") {
        int presType = msg["presType"].toInt(-1);
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->presentationShowFromRemote(presType); },
            Qt::QueuedConnection);
        return;
    }

    // ── Live-preview context-aware nav (works for lyrics/CTW/scripture/ppt) ──
    if (type == "lpNext") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->activateNextSlide(); },
            Qt::QueuedConnection);
        return;
    }
    if (type == "lpPrev") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->activatePrevSlide(); },
            Qt::QueuedConnection);
        return;
    }
    if (type == "lpParagraph") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pushParagraphCurrent(); },
            Qt::QueuedConnection);
        return;
    }
    if (type == "lpScripture") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pushScriptureCurrent(); },
            Qt::QueuedConnection);
        return;
    }

    // ── PPT ──────────────────────────────────────────────────────
    if (type == "pptNext") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pptNext(); },
            Qt::QueuedConnection);
        return;
    }
    if (type == "pptPrev") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pptPrev(); },
            Qt::QueuedConnection);
        return;
    }
    if (type == "pptGoto") {
        int slide = msg["slide"].toInt(0);
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pptGoto(slide); },
            Qt::QueuedConnection);
        return;
    }
    if (type == "pptGetState") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){ if(dock) dock->pptGetState(); },
            Qt::QueuedConnection);
        return;
    }

    // ── Call to Worship ───────────────────────────────────────────
    if (type == "applyParagraph") {
        QString title = msg["title"].toString();
        QString text  = msg["text"].toString();
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){
            if (dock) dock->applyParagraphFromRemote(title, text);
            if (s_server) s_server->broadcastCtwState(title, text);
        }, Qt::QueuedConnection);
        return;
    }
    if (type == "pushParagraphSlide") {
        QString title = msg["title"].toString();
        QString text  = msg["text"].toString();
        QMetaObject::invokeMethod(qApp, [=](){
            lyrics_renderer_scripture_push(title, text);
            if (s_server) s_server->broadcastScripture(title, text);
        }, Qt::QueuedConnection);
        return;
    }
    if (type == "clearParagraph") {
        QMetaObject::invokeMethod(qApp, [=](){
            lyrics_renderer_scripture_clear();
            if (s_server) s_server->broadcastScripture(QString(), QString());
        }, Qt::QueuedConnection);
        return;
    }

    // ── MV pause / seek controls ──────────────────────────────────
    if (type == "mvPause") {
        bool p = msg["pause"].toBool(true);
        QMetaObject::invokeMethod(qApp,
            [p](){ lyrics_renderer_mv_play_pause(p); }, Qt::QueuedConnection);
        return;
    }
    if (type == "mvMusicPause") {
        bool p = msg["pause"].toBool(true);
        QMetaObject::invokeMethod(qApp,
            [p](){ lyrics_renderer_music_play_pause(p); }, Qt::QueuedConnection);
        return;
    }
    if (type == "mvMusicStop") {
        QMetaObject::invokeMethod(qApp, [=](){
            lyrics_renderer_mv_music_clear();
            if (s_server) s_server->broadcastMvState("", "");
        }, Qt::QueuedConnection);
        return;
    }
    if (type == "audioBinPlay") {
        int idx = msg["index"].toInt(-1);
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){
            if (dock) dock->audioBinPlayFromRemote(idx);
        }, Qt::QueuedConnection);
        return;
    }
    if (type == "audioBinStop") {
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){
            if (dock) dock->audioBinStopFromRemote();
        }, Qt::QueuedConnection);
        return;
    }
    if (type == "mvSeek") {
        int64_t ms = (int64_t)msg["timeMs"].toDouble(0);
        QMetaObject::invokeMethod(qApp,
            [ms](){ lyrics_renderer_mv_set_time(ms); }, Qt::QueuedConnection);
        return;
    }
    if (type == "musicSeek") {
        int64_t ms = (int64_t)msg["timeMs"].toDouble(0);
        QMetaObject::invokeMethod(qApp,
            [ms](){ lyrics_renderer_music_set_time(ms); }, Qt::QueuedConnection);
        return;
    }
    // ── Color preset ──────────────────────────────────────────────
    if (type == "applyColorPreset") {
        int idx = msg["preset"].toInt(-1);
        LyricsDock *dock = m_dock;
        QMetaObject::invokeMethod(qApp, [=](){
            if (dock) dock->applyColorPresetFromRemote(idx);
        }, Qt::QueuedConnection);
        return;
    }
    // ── OBS source volume ─────────────────────────────────────────
    if (type == "setLyricsVolume") {
        float vol = (float)(msg["volume"].toDouble(100.0) / 100.0);
        QMetaObject::invokeMethod(qApp, [=](){
            obs_source_t *src = obs_get_source_by_name(LYRICS_SOURCE_ID);
            if (src) { obs_source_set_volume(src, vol); obs_source_release(src); }
        }, Qt::QueuedConnection);
        return;
    }
}

// ── Keepalive ─────────────────────────────────────────────────

void WebSocketServer::sendPingAll()
{
    // Send a WebSocket ping frame (opcode 0x9) to every upgraded client.
    // RFC 6455 requires browsers to reply with a pong, which resets the
    // idle timer on NAT routers and prevents the mobile OS from killing
    // the connection.
    static const QByteArray kPayload("ka"); // minimal 2-byte payload
    for (auto *c : m_clients) {
        if (c->upgraded && c->socket && c->socket->isOpen())
            sendPing(c->socket, kPayload);
    }
}

// ── MV time broadcast (500 ms tick) ──────────────────────────

void WebSocketServer::onMvTimeTick()
{
    bool hasManager = false;
    for (auto *c : m_clients)
        if (c->authed && c->role == "manager") { hasManager = true; break; }
    if (!hasManager) return;

    int64_t tMs = 0, dMs = 0;
    QJsonObject obj;
    bool ok = false;

    if (m_lastMvMode == "video") {
        ok = lyrics_renderer_mv_get_time(tMs, dMs);
        if (ok) obj["type"] = "mvTime";
    } else if (m_lastMvMode == "music" || m_lastMvMode == "musicOnly") {
        ok = lyrics_renderer_music_get_time(tMs, dMs);
        if (ok) obj["type"] = "musicTime";
    }
    if (ok) {
        obj["timeMs"]      = (double)tMs;
        obj["durationMs"]  = (double)dMs;
        broadcastJson(obj, true);
    }
}

// ── Broadcast helpers ─────────────────────────────────────────

void WebSocketServer::broadcastSlide(const QString &lyrics,
                                      const QString &section,
                                      const QString &title,
                                      int slideIndex)
{
    m_lastLyrics = lyrics; m_lastSection = section;
    m_lastTitle  = title;  m_lastSlideIndex = slideIndex;
    QJsonObject obj;
    obj["type"]       = "slideUpdate";
    obj["lyrics"]     = lyrics;
    obj["section"]    = section;
    obj["title"]      = title;
    obj["slideIndex"] = slideIndex;
    broadcastJson(obj);
}

void WebSocketServer::broadcastClear()
{
    m_lastLyrics.clear(); m_lastSection.clear();
    m_lastTitle.clear();  m_lastSlideIndex = -1;
    QJsonObject obj;
    obj["type"]       = "slideUpdate";
    obj["lyrics"]     = "";
    obj["section"]    = "";
    obj["title"]      = "";
    obj["slideIndex"] = -1;
    broadcastJson(obj);
}

void WebSocketServer::broadcastScripture(const QString &title,
                                          const QString &text)
{
    m_lastScripTitle = title;
    m_lastScripText  = text;
    QJsonObject obj;
    obj["type"]  = "scriptureUpdate";
    obj["title"] = title;
    obj["text"]  = text;
    broadcastJson(obj);
}

void WebSocketServer::broadcastScriptureState(const QString &title,
                                               const QString &text)
{
    m_lastScripFullTitle = title;
    m_lastScripFullText  = text;
    QJsonObject obj;
    obj["type"]  = "scriptureState";  // full title+raw text for slide rebuild
    obj["title"] = title;
    obj["text"]  = text;
    broadcastJson(obj, true); // managers only
}

void WebSocketServer::broadcastCtwState(const QString &title, const QString &text)
{
    m_lastParagraphTitle = title;
    m_lastParagraphText  = text;
    QJsonObject obj;
    obj["type"]  = "paragraphState";
    obj["title"] = title;
    obj["text"]  = text;
    broadcastJson(obj, true); // managers only
}

void WebSocketServer::broadcastColorPresets(const QJsonArray &presets, int activeIdx)
{
    m_lastColorPresets  = presets;
    m_lastActivePreset  = activeIdx;
    QJsonObject obj;
    obj["type"]    = "colorPresets";
    obj["presets"] = presets;
    obj["active"]  = activeIdx;
    broadcastJson(obj, true); // managers only
}

void WebSocketServer::broadcastPptState(int current, int total, const QString &title)
{
    QJsonObject obj;
    obj["type"]    = "pptState";
    obj["current"] = current;
    obj["total"]   = total;
    if (!title.isEmpty()) obj["title"] = title;
    broadcastJson(obj, true); // managers only
}

void WebSocketServer::broadcastPresentationActive(int type)
{
    static const char *kNames[] = { "PowerPoint", "Google Slides", "Canva" };
    QJsonObject obj;
    obj["type"]     = "presentationActive";
    obj["presType"] = type;
    obj["presName"] = (type >= 0 && type <= 2) ? kNames[type] : "";
    broadcastJson(obj); // all roles — LP needs this to update pres button state
}

void WebSocketServer::broadcastActiveMode(const QString &mode)
{
    QJsonObject obj;
    obj["type"] = "lpActiveMode";
    obj["mode"] = mode;
    broadcastJson(obj);
}

void WebSocketServer::broadcastScene(const QString &sceneName)
{
    m_lastScene = sceneName;
    QJsonObject obj;
    obj["type"]  = "sceneUpdate";
    obj["scene"] = sceneName;
    broadcastJson(obj);
}

void WebSocketServer::broadcastMediaState(const QString &filePath)
{
    m_lastMediaPath = filePath;
    QJsonObject obj;
    obj["type"] = "mediaUpdate";
    obj["path"] = filePath;
    broadcastJson(obj);
}

void WebSocketServer::broadcastSongs(const QJsonArray &songs)
{
    m_lastSongsJson = songs;
    QJsonObject obj;
    obj["type"]  = "songs";
    obj["songs"] = songs;
    broadcastJson(obj); // send to all roles (viewer needs for score mode)
}

void WebSocketServer::broadcastServiceOrder(const QJsonArray &order)
{
    m_lastServiceOrderJson = order;
    QJsonObject obj;
    obj["type"]  = "serviceOrder";
    obj["items"] = order;
    broadcastJson(obj, true); // managers only
}

void WebSocketServer::broadcastMediaList(const QString &tab, const QStringList &paths)
{
    // Cache per-tab
    if      (tab == "paragraph")       m_lastParagraphMediaList       = paths;
    else if (tab == "scripture") m_lastScriptureMediaList = paths;
    else                         m_lastLyricsMediaList    = paths;

    QJsonArray arr;
    for (const auto &p : paths) arr.append(p);
    QJsonObject obj;
    obj["type"]  = "mediaList";
    obj["tab"]   = tab;
    obj["items"] = arr;
    broadcastJson(obj, true); // managers only
}

void WebSocketServer::broadcastMvState(const QString &mode, const QString &path)
{
    m_lastMvMode = mode;
    m_lastMvPath = path;
    QJsonObject obj;
    obj["type"] = "mvState";
    obj["mode"] = mode;
    obj["path"] = path;
    broadcastJson(obj, true); // managers only
    if (!mode.isEmpty()) m_mvTimeTimer->start();
    else                 m_mvTimeTimer->stop();
}

void WebSocketServer::broadcastMvVideoList(const QStringList &paths)
{
    m_lastMvVideoList = paths;
    QJsonArray arr;
    for (const auto &p : paths) arr.append(p);
    QJsonObject obj;
    obj["type"]   = "mvVideoList";
    obj["videos"] = arr;
    broadcastJson(obj, true);
}

void WebSocketServer::broadcastMvMusicList(const QStringList &paths)
{
    m_lastMvMusicList = paths;
    QJsonArray arr;
    for (const auto &p : paths) arr.append(p);
    QJsonObject obj;
    obj["type"]  = "mvMusicList";
    obj["music"] = arr;
    broadcastJson(obj, true);
}

void WebSocketServer::broadcastAudioBinList(const QStringList &paths)
{
    m_lastAudioBinList = paths;
    QJsonArray arr;
    for (const auto &p : paths) arr.append(p);
    QJsonObject obj;
    obj["type"]  = "audioBinList";
    obj["files"] = arr;
    broadcastJson(obj, true);
}

void WebSocketServer::broadcastAudioBinState(int activeIndex, bool playing)
{
    m_lastAudioBinActive  = activeIndex;
    m_lastAudioBinPlaying = playing;
    QJsonObject obj;
    obj["type"]        = "audioBinState";
    obj["activeIndex"] = activeIndex;
    obj["playing"]     = playing;
    broadcastJson(obj, true);
}

// ── State snapshot for new joiner ────────────────────────────

QJsonObject WebSocketServer::buildStateForRole(const QString &role) const
{
    QJsonObject obj;
    obj["type"]       = "state";
    obj["lyrics"]     = m_lastLyrics;
    obj["section"]    = m_lastSection;
    obj["title"]      = m_lastTitle;
    obj["slideIndex"] = m_lastSlideIndex;

    if (role == "manager") {
        obj["scene"]     = m_lastScene;
        obj["mediaPath"] = m_lastMediaPath;
        obj["streaming"] = obs_frontend_streaming_active();
        obj["recording"] = obs_frontend_recording_active();
    }
    return obj;
}

// ── Send helpers ──────────────────────────────────────────────

void WebSocketServer::sendJson(WsClient *client, const QJsonObject &obj)
{
    if (!client || !client->upgraded) return;
    if (!client->socket || !client->socket->isOpen()) return;
    sendFrame(client->socket,
              QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void WebSocketServer::sendFrame(QTcpSocket *sock, const QByteArray &payload)
{
    if (!sock || !sock->isOpen()) return;
    QByteArray frame;
    frame.append((char)0x81); // FIN + text opcode
    int len = payload.size();
    if (len < 126) {
        frame.append((char)len);
    } else if (len < 65536) {
        frame.append((char)126);
        frame.append((char)((len >> 8) & 0xFF));
        frame.append((char)(len & 0xFF));
    } else {
        // Large frame: 8-byte extended length — must use uint64_t; shifting
        // a 32-bit int by ≥32 bits is UB and wraps mod-32 on x64 MSVC.
        frame.append((char)127);
        uint64_t ulen = (uint64_t)len;
        for (int i = 7; i >= 0; --i)
            frame.append((char)((ulen >> (i*8)) & 0xFF));
    }
    frame.append(payload);
    sock->write(frame);
}

void WebSocketServer::sendPong(QTcpSocket *sock, const QByteArray &pingData)
{
    if (!sock || !sock->isOpen()) return;
    QByteArray frame;
    frame.append((char)0x8A); // FIN + pong opcode (0xA)
    frame.append((char)(pingData.size() & 0x7F)); // no mask, small payload
    frame.append(pingData);
    sock->write(frame);
}

void WebSocketServer::sendPing(QTcpSocket *sock, const QByteArray &payload)
{
    if (!sock || !sock->isOpen()) return;
    QByteArray frame;
    frame.append((char)0x89); // FIN + ping opcode (0x9)
    frame.append((char)(payload.size() & 0x7F)); // no mask, small payload
    frame.append(payload);
    sock->write(frame);
}

void WebSocketServer::broadcastJson(const QJsonObject &obj,
                                     bool managersOnly)
{
    for (auto *c : m_clients) {
        if (!c->upgraded)        continue;
        if (c->role.isEmpty())   continue; // role not yet chosen
        if (managersOnly && c->role != "manager") continue;
        sendJson(c, obj);
    }
}

void WebSocketServer::broadcastPreview(const QByteArray &jpegData)
{
    // Skip expensive base64 encode if nobody is listening
    bool hasClients = false;
    for (auto *c : m_clients)
        if (c->upgraded && c->authed && !c->role.isEmpty()) { hasClients = true; break; }
    if (!hasClients) return;

    // Send as JSON text: {"type":"preview","data":"data:image/jpeg;base64,..."}
    QByteArray b64 = "data:image/jpeg;base64,";
    b64 += jpegData.toBase64();

    // Build frame manually to avoid QJsonDocument overhead on large strings
    QByteArray payload = "{\"type\":\"preview\",\"data\":\"";
    payload += b64;
    payload += "\"}";

    for (auto *c : m_clients) {
        if (!c->upgraded)      continue;
        if (!c->authed)        continue;
        if (c->role.isEmpty()) continue;
        sendFrame(c->socket, payload);
    }
}

// ── Connected clients for settings dialog ─────────────────────

QList<WebSocketServer::ClientInfo> WebSocketServer::connectedClients() const
{
    QList<ClientInfo> list;
    for (auto *c : m_clients) {
        if (!c->upgraded) continue;
        ClientInfo info;
        info.address = c->socket->peerAddress().toString();
        info.role    = c->role.isEmpty() ? "selecting role" : c->role;
        list << info;
    }
    return list;
}

// ── IP address detection ──────────────────────────────────────

QString bestLocalIpAddress()
{
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto &iface : QNetworkInterface::allInterfaces()) {
            if (!iface.flags().testFlag(QNetworkInterface::IsUp))      continue;
            if ( iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
            if (pass == 0 &&
                !iface.flags().testFlag(QNetworkInterface::IsRunning))  continue;
            if (iface.hardwareAddress().isEmpty()) continue;

            QString name = iface.humanReadableName().toLower();
            if (name.contains("virtual"))              continue;
            if (name.contains("vethernet"))            continue;
            if (name.contains("bluetooth"))            continue;
            if (name.contains("tunnel"))               continue;
            if (name.contains("pseudo"))               continue;
            if (name.contains("wi-fi direct"))         continue;
            if (name.contains("hotspot"))              continue;
            if (name.contains("local area connection*")) continue;

            for (const auto &entry : iface.addressEntries()) {
                if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                    continue;
                QString ip = entry.ip().toString();
                if (ip.startsWith("127."))       continue;
                if (ip.startsWith("169.254"))    continue;
                if (ip.startsWith("192.168.137")) continue;
                return ip;
            }
        }
    }
    return "127.0.0.1";
}

// ── Serve HTML page ───────────────────────────────────────────

QByteArray WebSocketServer::serveHtmlPage() const
{
    QString dataPath = lyricsModuleDataPath() + "/remote.html";
    blog(LOG_INFO, "[lyrics-ws] Serving: %s",
         dataPath.toUtf8().constData());
    QFile f(dataPath);
    if (f.open(QIODevice::ReadOnly))
        return f.readAll();

    blog(LOG_WARNING, "[lyrics-ws] remote.html not found at: %s",
         dataPath.toUtf8().constData());
    return QByteArray(
        "<html><body style='background:#111;color:#fff;"
        "font-family:sans-serif;display:flex;align-items:center;"
        "justify-content:center;height:100vh;margin:0;"
        "flex-direction:column;'>"
        "<h2>SSO Remote</h2>"
        "<p>remote.html not found.</p>"
        "<p style='font-size:12px;color:#888'>Expected: "
        + dataPath.toUtf8() + "</p></body></html>");
}
