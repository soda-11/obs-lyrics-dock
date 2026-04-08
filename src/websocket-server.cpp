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
    return true;
}

void WebSocketServer::stop()
{
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

        QByteArray resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

        client->socket->write(resp);
        client->upgraded = true;
        client->rxBuf.clear();

        blog(LOG_INFO, "[lyrics-ws] WebSocket upgraded: %s",
             client->socket->peerAddress().toString().toUtf8().constData());

        // Do NOT send state here — wait for client to send setRole
        return;
    }

    // ── Serve HTML page on plain HTTP GET / ──────────────────
    if (req.startsWith("GET /")) {
        QByteArray html = serveHtmlPage();
        QByteArray resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " + QByteArray::number(html.size()) + "\r\n"
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

    // Role selection — always allowed before and after role is set
    if (type == "setRole") {
        QString role = msg["role"].toString();
        if (role == "manager" || role == "viewer") {
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

            // Manager-only: media list and scenes
            if (role == "manager") {
                if (!m_lastMediaList.isEmpty()) {
                    QJsonArray arr;
                    for (const auto &p : m_lastMediaList) arr.append(p);
                    QJsonObject mediaMsg;
                    mediaMsg["type"]  = "mediaList";
                    mediaMsg["items"] = arr;
                    sendJson(client, mediaMsg);
                }

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
            }

            emit clientCountChanged(m_clients.size());
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

    if (type == "getSongs") {
        // Send cached sequence-based song list
        if (!m_lastSongsJson.isEmpty()) {
            QJsonObject resp;
            resp["type"]  = "songs";
            resp["songs"] = m_lastSongsJson;
            sendJson(client, resp);
        }
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
        QString path   = msg["path"].toString();
        int     fadeMs = msg["fadeMs"].toInt(500);
        QMetaObject::invokeMethod(qApp, [=]() {
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

void WebSocketServer::broadcastMediaList(const QStringList &paths)
{
    m_lastMediaList = paths;
    QJsonArray arr;
    for (const auto &p : paths) arr.append(p);
    QJsonObject obj;
    obj["type"]  = "mediaList";
    obj["items"] = arr;
    broadcastJson(obj, true); // managers only
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
        // Large frame: 8-byte extended length
        frame.append((char)127);
        for (int i = 7; i >= 0; --i)
            frame.append((char)((len >> (i*8)) & 0xFF));
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
        "<h2>OBS Lyrics Remote</h2>"
        "<p>remote.html not found.</p>"
        "<p style='font-size:12px;color:#888'>Expected: "
        + dataPath.toUtf8() + "</p></body></html>");
}
