# obs-lyrics-dock — CLAUDE.md

## Terminology (fixed — use these words in code, comments, UI and chat)

```
Service  →  Segment[]  →  Slide cards[]
```

| Term | Meaning | Code |
|------|---------|------|
| **Service** | The whole event: everything projected, in order. | `m_segments` + `saveServiceOrder()` |
| **Segment** | One entry in the service. 4 categories: Lyrics, Paragraphs, Media, Presentation. | `struct Segment`, `Segment::Type` |
| **Slide card** | One thing that goes on air (a slide button / media card / presentation card). | `SlideButton`, `MediaSlideCard`, `PptLiveCard` |
| **Section** | A song-level part (Verse, Chorus…). Exists only inside a Lyrics segment's song. | `LyricsSection`, `SectionType` |
| **Song library** | Separate library of songs. Adding a song to the service **copies** it; the segment is then independent and only remembers the library song id (for "save back to library"). | `SongManager`, `Song` |

Rules:
- Never call a service entry a "section" and never call a song's Verse/Chorus a "segment".
- The remote WebSocket protocol is a separate contract with `data/remote.html`: its message keys (e.g. `serviceIdx`, `items`) are NOT renamed with the code. Change them only together with `remote.html`.
- Service files on disk list segments under `"segments"`; older files used `"items"` and must keep loading.
- `CHANGELOG.md` records what/why; update it only when the owner asks.
- **ALWAYS keep the card queue in step** (see "STANDING RULE" at the top of `CHANGELOG.md`): `Segment::cards` must always match the segment's slides/files. After ANY change to a segment's text, sections, paragraph text or media list call `Segment::syncCards()` (or `syncAllCards()` for lines-per-slide); never create/reorder cards by hand; store anything per slide card ON the `SlideCard`, not in an index-keyed map.
- **Selecting a segment must NEVER change what is on air.** `loadSegmentContent` only updates the panel (card label colour, highlighted preset button). A segment's colour preset, layout and alternate-sentence colour reach the renderer when one of ITS slides is pushed (`applySegmentPreset`, `applyParagraphStyle`, `pushCurrentSlide`, `pushSlideFromRemote`). The colour setters take `repush=false` to store colours without re-rendering. After an edit, push what the card NOW contains (look it up after `syncCards()`), never the raw text that was typed.
- Built (v12.29+): every segment has a unique uuid; a Lyrics segment keeps the library song id in `sourceSongId`; Lyrics/Paragraph/Media segments have `cards[]` (each `SlideCard` has a permanent id; backgrounds belong to cards); user imports always mint new ids, loading the app's own saved service keeps them. After ANY change to a segment's text, sections or media list call `Segment::syncCards()`.
- Each paragraph segment OWNS a complete layout (`Segment::paragraphLayout`), copied from the default layout when the segment is created. The default is edited ONLY in the OBS source's Properties (right-click the source); the Layout button at the top of the slide panel edits the SELECTED paragraph segment's own layout (Lyrics: the lyrics layout). The default is a template for NEW segments only; changing it or another segment never changes an existing segment. Lyrics layout stays global.
- On-air state is tracked BY ID (v12.31): the on-air slide card (`m_onAirSegId`/`m_onAirCardId`, set in `setActiveSlide` / `rememberOnAir`), the live presentation card (`m_activePptSegId`) and the playing media card (`m_liveMediaSegId`/`m_liveMediaCardId`). After an edit, find the on-air card with `onAirIndexIn(seg)` BEFORE the grid is rebuilt (the rebuild resets `m_activeSlide`). `m_activeSlide` itself is still a working index. The remote protocol is still position-based; it is to be reworked around card ids together with `data/remote.html` (owner will provide the procedures).

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

### 5b. Live Preview remote: the PLUGIN is the source of truth
- The remote page keeps no navigation state of its own. The plugin sends `onAir` (`segIdx`, `segId`, `slideIdx`, `cardId`, `kind`) whenever what is on air changes, from ANY source (`rememberOnAir` / `publishOnAir`), and to every new client on connect. A showing presentation takes precedence.
- The queue = `serviceOrder` items, each with `id` and `cards` (card ids). It is re-sent to managers AND the Live Preview remote after every `saveServiceOrder()`, so it is never stale.
- The remote's arrows only send `lpNext` / `lpPrev`; the plugin walks the queue by id (`navigateQueue`: next card, then the next segment's first card, skipping empty segments). A presentation on air keeps the arrows for itself (PowerPoint slides) and NEVER moves to the next segment — the operator selects the next segment by hand.
- **A slide with no background shows no background.** Every slide change goes through `showBackground()` (own background, else the segment default, else blank). Blank clears whatever media is up — it checks both `m_currentBgPath` AND the renderer (`lyrics_renderer_media_background_active()`), because several panels reset `m_currentBgPath` while their media stays on air. A music-video (MV tab) video is the exception: lyrics are meant to sit over it. `showBackground` also tells the remote (`broadcastMediaState`). The remote's preview is a capture of OBS's own output, so it always shows exactly what is on air.
- **The remote only puts a card on air** (`activateSlide`, `lpNext`, `lpPrev`). It must never send text, backgrounds, colours, layout or an Audio Bin stop for a service card: the plugin applies the segment's own saved settings. Selecting a segment in the remote must not push anything (`selectServiceItem`). Explicit user actions (media strip, colour preset buttons, blackout) are the only exceptions.
- **Live Preview rule (owner): the chip of the on-air segment must ALWAYS be highlighted** (visible, centred in `#lpSection1`, same height as Prev/Next). Every way a card goes on air must call `rememberOnAir()`, and never call `setActiveSlide(-1)` after recording it (it wipes the record). Full rule: STANDING RULE in `CHANGELOG.md`.
- Anything new that changes what is on air must call `rememberOnAir()` (or `publishOnAir()`), otherwise the remote will not follow.

### 5a. Two different "global slide indexes" exist — do not mix them
- **Live grid numbering:** every segment's slides counted in service order (`buildAllLiveSlides`, `onSlideClicked` in live mode). Use `liveGlobalIndexOnAir()` (found from the on-air card id) for the current position.
- **Remote / song numbering:** lyric slides counted across SONGS ONLY (`buildSequenceJson`, `computeGlobalSlideOffset`). This is what `broadcastSlide` must send for lyrics.
- `m_currentSongSlideOffset` is a songs-only offset in some paths and a live-grid offset in others: never use it to find the current live position (that made the remote's Next jump to segment 1 when paragraphs preceded the song).

### 5. Slide indexing is global across the sequence
`m_currentSongSlideOffset` tracks how many slides precede the current song. Remote `pushSlide` commands use global slide indices. Keep this in sync when songs are reordered.

### 6. Qt version compatibility
CMake detects Qt6 first, falls back to Qt5. Do not use Qt6-only APIs without the `QT_VERSION` guard already in `CMakeLists.txt`.

### 7. `autoStartWsServer()` reads settings independently
It parses `settings.json` directly (not via `SettingsDialog`) because the dialog widget doesn't exist yet at load time. If the settings format changes, update both places.
