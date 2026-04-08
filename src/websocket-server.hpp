#pragma once
// websocket-server.hpp  v9.5.1

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QStringList>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>

class LyricsDock; // forward declare — avoids circular include

struct WsClient {
    QTcpSocket *socket   = nullptr;
    bool        upgraded = false;
    QString     role;      // "manager" | "viewer" | "" (not set yet)
    QByteArray  rxBuf;
};

class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer() override;

    bool    start(quint16 port);
    void    stop();
    bool    isRunning() const;
    quint16 port() const { return m_port; }

    // Dock reference — lets pushSlide update the slide highlight
    void setDock(LyricsDock *dock) { m_dock = dock; }

    // Broadcast helpers — called by dock/gallery when state changes
    void broadcastSlide(const QString &lyrics, const QString &section,
                        const QString &title, int slideIndex);
    void broadcastClear();
    void broadcastScene(const QString &sceneName);
    void broadcastMediaState(const QString &filePath);
    void broadcastMediaList(const QStringList &paths);
    void broadcastSongs(const QJsonArray &songs);

    struct ClientInfo { QString address; QString role; };
    QList<ClientInfo> connectedClients() const;

signals:
    void clientCountChanged(int total);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    void        handleHttpOrWs(WsClient *client, const QByteArray &data);
    void        handleWsFrame (WsClient *client, const QByteArray &payload);
    void        handleCommand (WsClient *client, const QJsonObject &msg);
    void        sendJson      (WsClient *client, const QJsonObject &obj);
    void        sendFrame     (QTcpSocket *sock, const QByteArray &payload);
    void        sendPong      (QTcpSocket *sock, const QByteArray &payload);
    void        broadcastJson (const QJsonObject &obj, bool managersOnly = false);
    QJsonObject buildStateForRole(const QString &role) const;
    QByteArray  serveHtmlPage() const;

    QTcpServer       *m_server  = nullptr;
    LyricsDock       *m_dock    = nullptr; // weak ref — not owned
    QList<WsClient*>  m_clients;
    quint16           m_port    = 8765;

    // Cached last-known state — sent to new clients after setRole
    QString     m_lastLyrics;
    QString     m_lastSection;
    QString     m_lastTitle;
    int         m_lastSlideIndex = -1;
    QString     m_lastScene;
    QString     m_lastMediaPath;
    QStringList m_lastMediaList;
    QJsonArray  m_lastSongsJson;
};

WebSocketServer  *lyricsWsServer();
void              setLyricsWsServer(WebSocketServer *s);
const QString    &lyricsModuleDataPath();
QString           bestLocalIpAddress();
