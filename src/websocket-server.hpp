#pragma once
// websocket-server.hpp  v9.5.1

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
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
    bool        authed   = false; // true when PIN verified (or no PIN set)
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
    // tab: "lyrics" | "paragraph" | "scripture"
    void broadcastMediaList(const QString &tab, const QStringList &paths);
    void broadcastSongs(const QJsonArray &songs);
    void broadcastServiceOrder(const QJsonArray &order);
    void broadcastMvState(const QString &mode, const QString &path);
    void broadcastMvVideoList(const QStringList &paths);
    void broadcastMvMusicList(const QStringList &paths);
    // Broadcasts the current slide being shown (for viewer confidence monitor)
    void broadcastScripture(const QString &title, const QString &text);
    // Broadcasts title+raw text so remote clients can rebuild their slide list
    void broadcastScriptureState(const QString &title, const QString &text);
    // Broadcasts Call to Worship full state (title+text) for slide rebuild on remote
    void broadcastCtwState(const QString &title, const QString &text);
    // Broadcasts current PowerPoint slide position to all manager clients
    void broadcastPptState(int current, int total, const QString &title = QString());
    // Broadcasts which presentation type is active: 0=PPT 1=GSlides 2=Canva -1=none
    void broadcastPresentationActive(int type);
    // Broadcasts which content tab is active ("paragraph"|"scr"|"lyr"|"ppt") — used by LP remote
    void broadcastActiveMode(const QString &mode);
    // Broadcasts color preset colors + active index to all manager clients
    void broadcastColorPresets(const QJsonArray &presets, int activeIdx);
    // Broadcasts AudioBin file list to all manager clients
    void broadcastAudioBinList(const QStringList &paths);
    // Broadcasts AudioBin play/stop state (index=-1 means stopped)
    void broadcastAudioBinState(int activeIndex, bool playing);
    // Broadcasts a JPEG preview frame to all authed clients (for remote.html preview tab)
    void broadcastPreview(const QByteArray &jpegData);
    void setPin(const QString &pin) { m_pin = pin; }
    QString pin() const { return m_pin; }

    struct ClientInfo { QString address; QString role; };
    QList<ClientInfo> connectedClients() const;

signals:
    void clientCountChanged(int total);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void sendPingAll();   // periodic keepalive ping to all connected clients
    void onMvTimeTick();  // 500 ms broadcast of video/music playback position

private:
    void        handleHttpOrWs(WsClient *client, const QByteArray &data);
    void        handleWsFrame (WsClient *client, const QByteArray &payload);
    void        handleCommand (WsClient *client, const QJsonObject &msg);
    void        sendJson      (WsClient *client, const QJsonObject &obj);
    void        sendFrame     (QTcpSocket *sock, const QByteArray &payload);
    void        sendPong      (QTcpSocket *sock, const QByteArray &payload);
    void        sendPing      (QTcpSocket *sock, const QByteArray &payload);
    void        broadcastJson (const QJsonObject &obj, bool managersOnly = false);
    QJsonObject buildStateForRole(const QString &role) const;
    QByteArray  serveHtmlPage() const;

    QTimer           *m_mvTimeTimer = nullptr; // 500 ms position broadcast when MV playing
    QTcpServer       *m_server    = nullptr;
    LyricsDock       *m_dock      = nullptr; // weak ref — not owned
    QList<WsClient*>  m_clients;
    quint16           m_port      = 8765;
    QTimer           *m_pingTimer = nullptr; // 20-second keepalive ping

    // Cached last-known state — sent to new clients after setRole
    QString     m_lastLyrics;
    QString     m_lastSection;
    QString     m_lastTitle;
    int         m_lastSlideIndex = -1;
    QString     m_lastScene;
    QString     m_lastMediaPath;
    QStringList m_lastLyricsMediaList;
    QStringList m_lastParagraphMediaList;
    QStringList m_lastScriptureMediaList;
    QJsonArray  m_lastSongsJson;
    QJsonArray  m_lastServiceOrderJson;
    QString     m_pin;             // empty = no auth required
    QString     m_lastMvMode;      // "video" | "music" | ""
    QString     m_lastMvPath;
    QStringList m_lastMvVideoList;
    QStringList m_lastMvMusicList;
    QString     m_lastScripTitle;    // currently displayed slide title
    QString     m_lastScripText;     // currently displayed slide text
    QString     m_lastScripFullTitle;// full title from Text panel
    QString     m_lastScripFullText; // full raw text from Text panel
    QString     m_lastParagraphTitle;      // CTW full title (for new-connection replay)
    QString     m_lastParagraphText;       // CTW full raw text
    QJsonArray  m_lastColorPresets;  // array of {font, outline} per preset
    int         m_lastActivePreset = -1; // which preset is currently active
    QStringList m_lastAudioBinList;
    int         m_lastAudioBinActive  = -1;
    bool        m_lastAudioBinPlaying = false;
};

WebSocketServer  *lyricsWsServer();
void              setLyricsWsServer(WebSocketServer *s);
const QString    &lyricsModuleDataPath();
QString           bestLocalIpAddress();
