// plugin-main.cpp  v10.0.0
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
#include <QMetaObject>
#include <cstdio>

OBS_DECLARE_MODULE()
// We implement obs_module_set_locale manually below so we can use the
// absolute data path — OBS may return a relative path from
// obs_get_module_data_path() which fails on systems where the process
// working directory differs from the OBS bin folder.
OBS_MODULE_USE_DEFAULT_LOCALE("obs-lyrics-dock", "en-US")

MODULE_EXPORT const char *obs_module_description()
{
    return "SSO v2.0";
}

static LyricsDock      *s_dock     = nullptr;
static WebSocketServer *s_wsServ   = nullptr;
static QString          s_dataPath;

// ── Hotkey IDs ────────────────────────────────────────────────
static obs_hotkey_id s_hkMedia[6]  = {
    OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID,
    OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID,
    OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID
};
static obs_hotkey_id s_hkNextSlide = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id s_hkPrevSlide = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id s_hkNextSong  = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id s_hkPrevSong  = OBS_INVALID_HOTKEY_ID;

struct MediaHkData { int pos; };
static MediaHkData s_mediaHkData[6] = { {1},{2},{3},{4},{5},{6} };

// ── Hotkey callbacks ──────────────────────────────────────────
static void hkNextSlide(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed || !s_dock) return;
    QMetaObject::invokeMethod(s_dock, "activateNextSlide", Qt::QueuedConnection);
}
static void hkPrevSlide(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed || !s_dock) return;
    QMetaObject::invokeMethod(s_dock, "activatePrevSlide", Qt::QueuedConnection);
}
static void hkNextSong(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed || !s_dock) return;
    QMetaObject::invokeMethod(s_dock, "activateNextSong", Qt::QueuedConnection);
}
static void hkPrevSong(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed || !s_dock) return;
    QMetaObject::invokeMethod(s_dock, "activatePrevSong", Qt::QueuedConnection);
}
static void hkMedia(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed || !s_dock) return;
    int pos = static_cast<MediaHkData*>(data)->pos;
    QMetaObject::invokeMethod(s_dock, "activateGalleryMedia",
                              Qt::QueuedConnection, Q_ARG(int, pos));
}

// ── Hotkey save / load ────────────────────────────────────────
static QString hotkeysFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/obs-lyrics-dock/hotkeys.json";
}

static void saveHotkeyBindings()
{
    obs_data_t *root = obs_data_create();

    auto saveOne = [&](const char *key, obs_hotkey_id id) {
        obs_data_array_t *arr = obs_hotkey_save(id);
        obs_data_set_array(root, key, arr);
        obs_data_array_release(arr);
    };

    char mk[16];
    for (int i = 0; i < 6; ++i) {
        snprintf(mk, sizeof(mk), "media%d", i + 1);
        saveOne(mk, s_hkMedia[i]);
    }
    saveOne("nextSlide", s_hkNextSlide);
    saveOne("prevSlide", s_hkPrevSlide);
    saveOne("nextSong",  s_hkNextSong);
    saveOne("prevSong",  s_hkPrevSong);

    const char *json = obs_data_get_json(root);
    obs_data_release(root);

    if (json) {
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::AppDataLocation)
                      + "/obs-lyrics-dock";
        QDir().mkpath(dir);
        QFile f(hotkeysFilePath());
        if (f.open(QIODevice::WriteOnly))
            f.write(QByteArray(json));
    }
}

static void loadHotkeyBindings()
{
    QFile f(hotkeysFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray raw = f.readAll();

    obs_data_t *root = obs_data_create_from_json(raw.constData());
    if (!root) return;

    auto loadOne = [&](const char *key, obs_hotkey_id id) {
        obs_data_array_t *arr = obs_data_get_array(root, key);
        if (arr) {
            obs_hotkey_load(id, arr);
            obs_data_array_release(arr);
        }
    };

    char mk[16];
    for (int i = 0; i < 6; ++i) {
        snprintf(mk, sizeof(mk), "media%d", i + 1);
        loadOne(mk, s_hkMedia[i]);
    }
    loadOne("nextSlide", s_hkNextSlide);
    loadOne("prevSlide", s_hkPrevSlide);
    loadOne("nextSong",  s_hkNextSong);
    loadOne("prevSong",  s_hkPrevSong);

    obs_data_release(root);
}

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

// Returns true if the scene (or any nested scene) contains an obs-lyrics-source.
static bool sceneHasLyricsSource(obs_scene_t *scene)
{
    bool found = false;
    obs_scene_enum_items(scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
            bool *found = static_cast<bool *>(param);
            obs_source_t *src = obs_sceneitem_get_source(item);
            if (!src) return true;

            const char *id = obs_source_get_id(src);
            if (id && strcmp(id, LYRICS_SOURCE_ID) == 0) {
                *found = true;
                return false; // stop enumeration
            }
            // Recurse into nested scenes
            obs_scene_t *nested = obs_scene_from_source(src);
            if (nested && sceneHasLyricsSource(nested)) {
                *found = true;
                return false;
            }
            return true;
        },
        &found);
    return found;
}

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
    if (event == OBS_FRONTEND_EVENT_EXIT) {
        saveHotkeyBindings();
        if (s_dock) {
            // Stop all media (incl. any OBS sources) before OBS tears down its
            // audio context — prevents mp_media_thread / copy_audio_data crashes.
            s_dock->stopAllMedia();
            s_dock->saveState();
            s_dock = nullptr;
        }
        // Clear dock reference in server before Qt destroys the dock widget
        if (s_wsServ) s_wsServ->setDock(nullptr);
        SongManager::instance().save();
        if (s_wsServ) s_wsServ->stop();
        return;
    }

    if (event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
        if (!s_dock) return;
        obs_source_t *sceneSrc = obs_frontend_get_current_scene();
        if (!sceneSrc) return;
        obs_scene_t *scene = obs_scene_from_source(sceneSrc);
        bool hasSource = scene && sceneHasLyricsSource(scene);
        obs_source_release(sceneSrc);
        if (!hasSource)
            s_dock->stopAllMedia();
    }
}

MODULE_EXPORT bool obs_module_load()
{
    blog(LOG_INFO, "[obs-lyrics-dock] Loading v10.0.0");

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

    obs_frontend_add_dock_by_id("obs-lyrics-dock", "SSO v2.0", s_dock);

    // ── Register OBS hotkeys ──────────────────────────────────
    {
        char name[64], desc[64];
        for (int i = 0; i < 6; ++i) {
            snprintf(name, sizeof(name), "obs_lyrics_media_%d", i + 1);
            snprintf(desc, sizeof(desc), "Lyrics: Background Media %d", i + 1);
            s_hkMedia[i] = obs_hotkey_register_frontend(
                name, desc, hkMedia, &s_mediaHkData[i]);
        }
        s_hkNextSlide = obs_hotkey_register_frontend(
            "obs_lyrics_next_slide", "Lyrics: Next Slide",
            hkNextSlide, nullptr);
        s_hkPrevSlide = obs_hotkey_register_frontend(
            "obs_lyrics_prev_slide", "Lyrics: Previous Slide",
            hkPrevSlide, nullptr);
        s_hkNextSong = obs_hotkey_register_frontend(
            "obs_lyrics_next_song", "Lyrics: Next Song",
            hkNextSong, nullptr);
        s_hkPrevSong = obs_hotkey_register_frontend(
            "obs_lyrics_prev_song", "Lyrics: Previous Song",
            hkPrevSong, nullptr);
        loadHotkeyBindings();
    }

    blog(LOG_INFO, "[obs-lyrics-dock] Registered v10.0.0");
    return true;
}

MODULE_EXPORT void obs_module_unload()
{
    blog(LOG_INFO, "[obs-lyrics-dock] Unloading v10.0.0");

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
