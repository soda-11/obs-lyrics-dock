// plugin-main.cpp  v9.8.1
#include "lyrics-dock.hpp"
#include "song-manager.hpp"
#include "lyrics-renderer.hpp"
#include "websocket-server.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

OBS_DECLARE_MODULE()
// We implement obs_module_set_locale manually below so we can use the
// absolute data path — OBS may return a relative path from
// obs_get_module_data_path() which fails on systems where the process
// working directory differs from the OBS bin folder.
OBS_MODULE_USE_DEFAULT_LOCALE("obs-lyrics-dock", "en-US")

MODULE_EXPORT const char *obs_module_description()
{
    return "Lyrics Dock v9.8.1";
}

static LyricsDock      *s_dock     = nullptr;
static WebSocketServer *s_wsServ   = nullptr;
static QString          s_dataPath;

const QString &lyricsModuleDataPath() { return s_dataPath; }

// ── Read WS settings without needing the dialog widget ────────
static void autoStartWsServer()
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QFile f(dir + "/settings.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull()) return;
    auto obj = doc.object();
    bool enabled = obj["wsEnabled"].toBool(false);
    int  port    = obj["wsPort"].toInt(8765);
    QString pin = obj["wsPin"].toString();
    if (s_wsServ) s_wsServ->setPin(pin);
    if (enabled && s_wsServ)
        s_wsServ->start((quint16)port);
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        if (s_dock) {
            s_dock->saveState();
            s_dock = nullptr;
        }
        // Clear dock reference in server before Qt destroys the dock widget
        if (s_wsServ) s_wsServ->setDock(nullptr);
        SongManager::instance().save();
        if (s_wsServ) s_wsServ->stop();
    }
}

MODULE_EXPORT bool obs_module_load()
{
    blog(LOG_INFO, "[obs-lyrics-dock] Loading v9.8.1");

    // Capture module data path while obs_current_module() is valid.
    // obs_get_module_data_path() can return a relative path (e.g.
    // "../../data/obs-plugins/obs-lyrics-dock") on some OBS builds.
    // Convert to absolute using the DLL's own location so remote.html
    // serving works regardless of the process working directory.
    {
        const char *binPath =
            obs_get_module_binary_path(obs_current_module());
        if (binPath && *binPath) {
            // DLL is at <obs-root>/obs-plugins/64bit/obs-lyrics-dock.dll
            // Data is at <obs-root>/data/obs-plugins/obs-lyrics-dock/
            QDir binDir = QFileInfo(QString::fromUtf8(binPath)).absoluteDir();
            s_dataPath  = QDir::cleanPath(
                binDir.absolutePath() +
                "/../../data/obs-plugins/obs-lyrics-dock");
        } else {
            // Fallback: resolve obs_get_module_data_path against CWD
            QString raw = QString::fromUtf8(
                obs_get_module_data_path(obs_current_module()));
            s_dataPath = QDir::isRelativePath(raw)
                         ? QDir::cleanPath(QDir::currentPath() + "/" + raw)
                         : raw;
        }
    }
    blog(LOG_INFO, "[obs-lyrics-dock] Data path: %s",
         s_dataPath.toUtf8().constData());

    // Verify locale file is reachable and warn explicitly if not.
    {
        QString localePath = s_dataPath + "/locale/en-US.ini";
        if (QFile::exists(localePath))
            blog(LOG_INFO,  "[obs-lyrics-dock] Locale file OK: %s",
                 localePath.toUtf8().constData());
        else
            blog(LOG_WARNING, "[obs-lyrics-dock] Locale file NOT found: %s"
                 " — install the plugin via the installer, not by copying "
                 "the DLL alone.", localePath.toUtf8().constData());
    }

    lyrics_renderer_register();
    blog(LOG_INFO, "[obs-lyrics-dock] 'Lyrics' source registered");

    SongManager::instance().load();
    obs_frontend_add_event_callback(onFrontendEvent, nullptr);

    const auto mw = static_cast<QMainWindow*>(
        obs_frontend_get_main_window());
    if (!mw) {
        blog(LOG_ERROR, "[obs-lyrics-dock] No main window");
        return false;
    }

    // Create WS server — owned by us (nullptr parent), not by Qt hierarchy.
    // This prevents Qt from deleting it during main-window destruction
    // at an unpredictable time. We delete it explicitly in obs_module_unload().
    s_wsServ = new WebSocketServer(nullptr);
    setLyricsWsServer(s_wsServ);

    // Auto-start server if it was enabled in the last session
    autoStartWsServer();

    // Create dock (parented to main window — Qt owns and destroys it)
    s_dock = new LyricsDock(mw);
    s_dock->setWsServer(s_wsServ);
    s_wsServ->setDock(s_dock);

    // Populate m_lastSongsJson immediately so any client that connects
    // after this receives the current sequence without needing a sequence
    // change event to trigger it first.
    s_dock->broadcastSequence();

    obs_frontend_add_dock_by_id("obs-lyrics-dock", "Lyrics", s_dock);

    blog(LOG_INFO, "[obs-lyrics-dock] Registered v9.8.1");
    return true;
}

MODULE_EXPORT void obs_module_unload()
{
    blog(LOG_INFO, "[obs-lyrics-dock] Unloading v9.8.1");

    // s_dock is already nullptr — cleared in EXIT callback before Qt
    // destroyed the widget. Do NOT touch it here.

    obs_frontend_remove_event_callback(onFrontendEvent, nullptr);

    // stop() was already called in the EXIT callback.
    // Do NOT call delete here — obs_module_unload() runs while OBS is
    // tearing down Qt. Deleting Qt objects (QTcpServer children) at this
    // point can hang if the Qt event loop is no longer processing.
    // The OS reclaims all memory when the process exits anyway.
    if (s_wsServ) {
        s_wsServ->stop(); // idempotent — safe if already stopped
        s_wsServ = nullptr;
        setLyricsWsServer(nullptr);
        // Intentional leak: do not delete. OBS process exit cleans up.
    }
}
