# obs-lyrics-dock — CLAUDE.md

## Build Commands

**Prerequisites:** OBS Studio SDK, Qt6 (or Qt5), CMake 3.16+, Visual Studio 2019/2022

```bash
# Configure (from repo root)
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/path/to/obs-studio" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Install (copies DLL + data into OBS plugin dirs)
cmake --install build --config Release --prefix "C:/Program Files/obs-studio"
```

**Output:** `build/Release/obs-lyrics-dock.dll`

**Install paths (manual copy if needed):**
- DLL → `obs-plugins/64bit/obs-lyrics-dock.dll`
- Data → `data/obs-plugins/obs-lyrics-dock/` (contains `locale/en-US.ini`, `remote.html`)

---

## Architecture

### Entry Point
`plugin-main.cpp` — OBS module lifecycle (`obs_module_load` / `obs_module_unload`).
Owns the two global singletons: `LyricsDock*` and `WebSocketServer*`.

### Components

| File | Role |
|------|------|
| `lyrics-dock.cpp/hpp` | Main OBS dock widget. Manages the song sequence list, slide grid, playback controls, zoom, and blackout. |
| `song-manager.cpp/hpp` | Singleton. Loads/saves song library to `AppData/obs-lyrics-dock/songs.json`. Songs split into `LyricsSection`s, each section split into 4-line slides. |
| `lyrics-renderer.cpp/hpp` | Registers the `obs-lyrics-source` OBS source. Handles text rendering and media playback with crossfade. |
| `websocket-server.cpp/hpp` | Raw TCP WebSocket server (no third-party lib). Serves `remote.html` and accepts manager/viewer clients. Port default: **8765**. |
| `media-gallery.cpp/hpp` | Media asset panel within the dock. |
| `settings-dialog.cpp/hpp` | Settings UI for WebSocket enable/port. Reads/writes `AppData/obs-lyrics-dock/settings.json`. |
| `song-edit-dialog.cpp/hpp` | Dialog for creating/editing songs and sections. |

### Data Flow
```
LyricsDock (user clicks slide)
  → lyrics_renderer_push()     — updates OBS source (what viewers see)
  → WebSocketServer::broadcastSlide()  — notifies remote clients
```

Remote clients (via `remote.html`) send `pushSlide` commands:
```
WebSocketServer → LyricsDock::pushSlideFromRemote() → lyrics_renderer_push()
```

### Persistence
- Song library: `%APPDATA%/obs-lyrics-dock/songs.json`
- Settings (WS port, enabled): `%APPDATA%/obs-lyrics-dock/settings.json`
- Dock layout: saved via `LyricsDock::saveState()` on OBS exit

---

## Known Pitfalls

### 1. WebSocketServer ownership — do NOT parent to Qt hierarchy
`s_wsServ` is created with `nullptr` parent intentionally. If parented to the main window, Qt destroys it at an unpredictable time during shutdown, causing crashes. It is stopped in the `OBS_FRONTEND_EVENT_EXIT` callback and intentionally leaked (not deleted) in `obs_module_unload()` because Qt's event loop may no longer be running at that point.

### 2. Do NOT touch `s_dock` in `obs_module_unload()`
The dock widget is parented to OBS's main window and destroyed by Qt before `obs_module_unload()` runs. It is set to `nullptr` in the EXIT event callback. Any access after that will crash.

### 3. `obs_current_module()` is only valid during `obs_module_load()`
The module data path is captured into `s_dataPath` at load time for this reason.

### 4. WebSocket is hand-rolled (no Qt WebSockets / libwebsockets)
The server handles HTTP upgrade, framing, masking, and ping/pong manually in `websocket-server.cpp`. When modifying, preserve the frame parsing state machine carefully — partial frames are buffered in `WsClient::rxBuf`.

### 5. Slide indexing is global across the sequence
`m_currentSongSlideOffset` tracks how many slides precede the current song. Remote `pushSlide` commands use global slide indices. Keep this in sync when songs are reordered.

### 6. Qt version compatibility
CMake detects Qt6 first, falls back to Qt5. Do not use Qt6-only APIs without the `QT_VERSION` guard already in `CMakeLists.txt`.

### 7. `autoStartWsServer()` reads settings independently
It parses `settings.json` directly (not via `SettingsDialog`) because the dialog widget doesn't exist yet at load time. If the settings format changes, update both places.
