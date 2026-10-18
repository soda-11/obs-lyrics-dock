# OBS Lyrics Dock — User Manual
### Version 10.2.0

---

## Table of Contents

1. [Overview](#1-overview)
2. [Installation](#2-installation)
3. [First-Time Setup](#3-first-time-setup)
4. [The Lyrics Tab](#4-the-lyrics-tab)
   - 4.1 [Song Library](#41-song-library)
   - 4.2 [Creating & Editing Songs](#42-creating--editing-songs)
   - 4.3 [Show Sequence](#43-show-sequence)
   - 4.4 [Slide Panel](#44-slide-panel)
   - 4.5 [Background Gallery](#45-background-gallery)
   - 4.6 [Color Presets](#46-color-presets)
   - 4.7 [Navigation Controls](#47-navigation-controls)
5. [The Scripture Tab](#5-the-scripture-tab)
6. [The MV Tab](#6-the-mv-tab)
7. [The WebSocket Tab — Remote Control](#7-the-websocket-tab--remote-control)
   - 7.1 [Connecting Your Phone](#71-connecting-your-phone)
   - 7.2 [PIN Authentication](#72-pin-authentication)
   - 7.3 [Manager Role](#73-manager-role)
   - 7.4 [Viewer — Confidence Monitor](#74-viewer--confidence-monitor)
   - 7.5 [Viewer — Score View](#75-viewer--score-view)
8. [The OBS Source](#8-the-obs-source)
9. [Tips & Best Practices](#9-tips--best-practices)

---

## 1. Overview

**OBS Lyrics Dock** is an OBS Studio plugin for live worship and event production. It lets you display song lyrics, Bible scriptures, and background media on your OBS canvas — all from a dedicated dock panel inside OBS. A built-in WebSocket server lets your team control slides from their phones using any web browser, with no app to install.

**Key capabilities:**

- Song library with full lyrics management
- Ordered show sequence for your service
- Visual slide cards — click to push to OBS instantly
- Background images and video with smooth crossfade
- Chinese trilingual mode (Pinyin + Chinese characters + English translation)
- Bible Scripture display with automatic slide splitting
- Music Video (MV) playback — video and audio
- Wi-Fi remote control for phones and tablets
- Multiple remote roles: Manager (full control), Confidence Monitor, Score View
- PIN-protected access

---

## 2. Installation

### Using the Installer (Recommended)

1. Download **OBS-Lyrics-Dock-v10.2.0-Setup.exe**
2. Close OBS Studio completely
3. Right-click the installer → **Run as administrator**
4. Follow the setup wizard — it will auto-detect your OBS installation
5. Launch OBS Studio

### Manual Installation

Copy the following files into your OBS Studio folder:

| File | Destination |
|------|-------------|
| `obs-lyrics-dock.dll` | `C:\Program Files\obs-studio\obs-plugins\64bit\` |
| `remote.html` | `C:\Program Files\obs-studio\data\obs-plugins\obs-lyrics-dock\` |

> OBS must be closed before copying the DLL.

---

## 3. First-Time Setup

### Step 1 — Open the Dock

In OBS, go to **View → Docks → OBS Lyrics Dock**. The dock panel will appear. You can float it, resize it, or snap it to any side of the OBS window.

### Step 2 — Add the Lyrics Source

1. In your OBS **Sources** panel, click **+** → **Lyrics**
2. Name it (e.g. "Lyrics") and click OK
3. Resize and position the source on your scene — typically full-screen

> You only need to add this source once. It persists across OBS restarts.

### Step 3 — Configure the Source

Right-click the Lyrics source → **Properties** to open the settings panel. Here you can adjust:

- **Canvas size** — match your OBS output resolution (default 1280×720)
- **Bottom bar height** — height of the section/title strip at the bottom
- **Font, size, colour, and outline** for each text layer

---

## 4. The Lyrics Tab

The Lyrics tab is divided into three panels side by side:

```
┌─────────────────┬──────────────────────────────────────┬─────────────────┐
│  Show Sequence  │           Slides                     │   Background    │
│  (song list)    │   (slide cards for selected song)    │   (gallery)     │
└─────────────────┴──────────────────────────────────────┴─────────────────┘
```

---

### 4.1 Song Library

The Song Library is a persistent database of all your songs stored in `%APPDATA%\obs-lyrics-dock\songs.json`.

To open it, click **＋ Manage Songs** at the top of the Show Sequence panel.

The Library window shows:
- **Left panel** — searchable list of all songs (✓ marks songs in the current sequence)
- **Right panel** — lyrics preview for the selected song
- **Add / Remove** — add or remove the selected song from the show sequence
- **New Song** — create a brand-new song
- **Delete** — permanently remove a song from the library (cannot be undone)

You can also **right-click** any song in the list for a context menu with Add, Remove, and Edit options.

---

### 4.2 Creating & Editing Songs

Click **New Song** in the library, or right-click an existing song and choose **Edit Song**, to open the song editor.

**Song fields:**
- **Title** and **Artist** — displayed in the sequence list and on slide cards
- **Sections** — a song is composed of one or more sections (Verse, Chorus, Bridge, Intro, Outro, Pre-Chorus, etc.)

**Each section has:**
- A **type** (e.g. Verse, Chorus) and optional custom **label**
- A **lyrics text box** — type or paste the full lyrics for that section
- An optional **Chinese mode** toggle for trilingual slides (see below)

**Slide splitting:**
Lyrics are automatically split into slides based on the **Lines per Slide** setting (default: 4). A blank line in the lyrics always forces a new slide, regardless of the lines setting.

**Chinese Trilingual Mode:**
Enable this per-section when your lyrics contain Chinese characters. You get three text fields:
- **Pinyin** — romanised pronunciation, displayed above the characters
- **Chinese Characters** — the main character text
- **English Translation** — displayed below

Each content line is placed in its own strip on the OBS canvas so the three layers align correctly.

**Save as New:** When editing an existing song, you can tick "Save as New" to create a copy with a new ID — useful for song variants without overwriting the original.

---

### 4.3 Show Sequence

The Show Sequence is the ordered list of songs for your service. It is saved automatically and restored when OBS reopens.

**Building the sequence:**
1. Click **＋ Manage Songs** → select songs from the library → click **Add**
2. Songs marked with **✓** are already in the sequence

**Reordering:**
- **Drag and drop** rows up or down directly in the sequence list
- Or use the **↑ / ↓ buttons** below the list

**Other actions:**
- **Trash icon** — remove the selected song from the sequence (does not delete it from the library)
- **Pen icon** — edit the selected song's lyrics without leaving the dock
- **Right-click** any song for a context menu

Clicking a song in the sequence loads its slides into the Slide panel.

---

### 4.4 Slide Panel

The Slide panel shows all slides for the currently selected song as a grid of cards. Each card displays:
- **Section label** (top-left, yellow) — e.g. "Verse 1", "Chorus"
- **Slide number** (top-right, cyan)
- **Lyrics text** (body)
- **Chinese slides** show a 中 marker and display characters + English translation

**Clicking a card** immediately pushes that slide's text to the OBS Lyrics source. The active card is highlighted in blue.

**Double-click or right-click a card** to open the song editor focused on that section, for quick on-the-fly edits. Changes apply immediately to the OBS preview.

#### Display Controls (bottom bar)

| Control | Description |
|---------|-------------|
| ◄ / ► buttons | Previous / Next slide |
| Keyboard ← / → | Previous / Next slide (when dock is focused) |
| Slide counter (e.g. 3/12) | Current slide position |
| Zoom slider (25%–200%) | Scale slide card text size |
| W slider (10%–100%) | Card width as a percentage of the panel — narrow for more columns, wide for fewer |
| Font picker | Font shown on the slide cards (display only, does not affect OBS rendering) |
| Lines spinbox | Maximum lines per slide (1–6) |

---

### 4.5 Background Gallery

The Background panel (right side of the Lyrics tab) holds a collection of images and videos to display behind the lyrics on the OBS canvas.

- **Double-click** any item to push it as the background (with a smooth 500 ms crossfade)
- The active background is highlighted
- Click **■ Stop Background Media** to remove the background (fades to black)
- Add items using the **+** button at the bottom of the gallery
- Video backgrounds loop continuously

> **Note:** Playing a background from this gallery keeps MV music playing — the audio continues uninterrupted, only the visual layer changes.

---

### 4.6 Color Presets

Five color preset buttons sit in the Slides panel header:

| Button | Applies to |
|--------|------------|
| **1, 2, 3** | Lyrics text and Chinese characters + English translation |
| **Py: 4, 5** | Pinyin text only |

**To set a preset color:**
- Left-click a button to apply its stored color immediately
- Right-click a button to open the color picker and change its stored color

Colors are saved to disk automatically and restored on next launch.

---

### 4.7 Navigation Controls

| Action | How |
|--------|-----|
| Next slide | Click ► button, or press **→** on keyboard |
| Previous slide | Click ◄ button, or press **←** on keyboard |
| Blackout lyrics | Click **■ Blank** button (top-right of Slides header) |
| Push a specific slide | Click its card directly |
| Edit a slide's section | Right-click its card → Edit this section |

---

## 5. The Scripture Tab

The Scripture tab lets you display Bible verses as a separate overlay — independent of the song lyrics.

**How to use:**

1. Click the **Scripture** tab in the dock
2. Type a reference in the **Title** field (e.g. "John 3:16")
3. Paste the verse text in the **Text** field
4. Click **▶ Apply** — the text is split into slides (4 lines per slide, paragraph-aware)
5. Click any slide to push it to the OBS canvas

**Background Gallery:**
The Scripture tab has its own background gallery, separate from the Lyrics gallery. You can set a different background image specifically for Scripture display.

**Clearing:**
Click **⬛ Clear** to remove the Scripture overlay from the OBS canvas.

**Behaviour with other tabs:**
- Clicking a Scripture slide clears any active lyrics text from the canvas
- Clicking a lyrics slide clears any active scripture text
- The MV tab and Scripture tab are mutually exclusive on the canvas

---

## 6. The MV Tab

The MV (Music Video) tab lets you play video or music files that take over the OBS canvas.

### Video Playback
- Click a video in the **Videos** list to play it full-screen on the OBS canvas
- Lyrics text is hidden while a video plays
- The video plays once and stops

### Music Playback
- Click a track in the **Music** list to start audio playback
- The OBS canvas remains clear (no visual from the audio file — album art is suppressed)
- You can then use the **Lyrics tab** to display lyrics over a background while music plays

### Now Playing Bar
When playback is active, a bar at the top of the MV panel shows the current file name and a **■ Stop** button.

### Managing the MV List
Add video and music files using the **+** buttons at the bottom of each list. File paths are saved to disk and restored on next launch.

> **Tip:** Use Music mode for backing tracks during worship — the audio plays through OBS while you independently control lyrics and background from the Lyrics tab.

---

## 7. The WebSocket Tab — Remote Control

The WebSocket tab enables phone-based remote control over your local Wi-Fi network. Team members can control slides, switch scenes, and monitor lyrics from any phone or tablet — no app to install, just a web browser.

### 7.1 Connecting Your Phone

1. Click the **WebSocket** tab in the dock
2. A **QR code** appears on the OBS canvas (and in the tab)
3. Scan the QR code with your phone camera
4. Tap the link to open the remote web page in Safari or Chrome
5. Enter your PIN if required, then choose your role

The remote page address is also shown as a URL (e.g. `http://192.168.1.x:8765`) — you can type this directly into any browser on the same Wi-Fi network.

> The QR code is visible on the OBS canvas only while the WebSocket tab is active. Switch to another tab to hide it from the stream.

### 7.2 PIN Authentication

To protect access, you can set a PIN in the WebSocket settings:

1. Click **Settings** (gear icon) in the WebSocket tab
2. Enter a 4-digit PIN and save

When a PIN is set, connecting devices will see a PIN entry screen **before** the role selection screen. Incorrect PIN entries are rejected immediately.

To disable PIN protection, clear the PIN field and save.

### 7.3 Manager Role

The Manager role gives full control of the OBS session from a phone:

**Songs tab**
- Browse the full show sequence
- Tap any song to expand its slides
- Tap a slide to push it to OBS instantly
- The currently active slide is highlighted

**Scripture tab**
- Type a reference and verse text
- Tap **Apply** to split into slides
- Tap any slide to display it on the OBS canvas

**Scenes tab**
- View all OBS scenes
- Tap a scene to switch to it immediately

**Media tab**
- Browse the background gallery
- Tap any item to display it as the background behind lyrics
- Tap **⬛ Stop Background Media** to clear

**MV tab**
- Start/stop video and music playback
- View what is currently playing

**Controls tab**
- ⬛ Blackout Lyrics
- ⬛ Stop Background Media
- ⬛ Stop MV
- ▶ Start / ■ Stop Stream
- ⏺ Start / ■ Stop Recording

> Up to 4 simultaneous WebSocket connections per IP address are supported.

### 7.4 Viewer — Confidence Monitor

The Confidence Monitor is a read-only display that always shows the currently active slide in large text. It updates automatically whenever a new slide is pushed from OBS or another remote.

Ideal for:
- A monitor facing the worship leader or musicians
- A tablet at the front of the stage

Font size adjusts automatically to fill the screen.

### 7.5 Viewer — Score View

Score View shows the full lyrics of a selected song, so the user can read ahead.

- Choose a song from the dropdown at the top
- All sections and slides are listed in order
- The currently active slide (as pushed from OBS) is highlighted automatically
- Toggle **Follow** to have the view automatically jump to whichever song is currently active in OBS
- Use **A− / A+** buttons to adjust the font size

Ideal for:
- Musicians who need to see coming sections
- A prompter at the sound desk

---

## 8. The OBS Source

The **Lyrics** source renders all content as layers from bottom to top:

| Layer | Content |
|-------|---------|
| 1 (bottom) | Background image or video (with crossfade transition) |
| 2 | Scripture title (top strip, only in Scripture mode) |
| 3 | Scripture text (below title, only in Scripture mode) |
| 4 | Lyrics text — full canvas (English mode) |
| 4–6 | Pinyin / Chinese characters / English translation (Chinese mode) |
| 5 | Section label (bottom-left) |
| 6 | Song title (bottom-right) |
| 7 (top) | QR code overlay (bottom-right corner, while WebSocket tab is active) |

### Source Properties

Open the source Properties to configure text appearance for each layer:

| Setting | Description |
|---------|-------------|
| Canvas Width / Height | Match your OBS output resolution |
| Bottom bar height | Height of the section + title strip (px) |
| Scripture title strip height | Height of the scripture reference strip |
| Scripture left/right margin | Horizontal inset for scripture text |
| Font, Size, Bold, Italic | Per text-layer font settings |
| Text Colour / Opacity | Colour and transparency of text |
| Outline | Toggle, width, and colour for text outline |
| Background | Optional coloured background behind text |
| Horizontal / Vertical Align | Text alignment within each text box |

**Chinese Interleave Layout** (advanced):
Adjust the Y positions and heights of the Pinyin, Chinese character strips, and English translation layers to fine-tune the trilingual layout for your canvas size.

---

## 9. Tips & Best Practices

**Preparing for a service:**
1. Add all songs to the Song Library before the service
2. Build the Show Sequence in worship order
3. Set lines-per-slide to match your screen layout (4 is standard)
4. Pre-load background images in the Background gallery
5. Test the remote on at least one phone beforehand

**During a service:**
- Use **keyboard ← / →** arrows on the OBS computer for fast slide navigation
- Keep the **Confidence Monitor** on a tablet facing the worship leader
- Use **■ Blank** (Blackout) between songs to clear the screen cleanly
- Right-click any slide card to fix a typo on the fly — changes push instantly

**Remote team setup:**
- Give phone operators the **Manager** role
- Give musicians or singers a **Viewer → Score View** phone on a stand
- The worship leader can use **Viewer → Confidence Monitor** on a tablet

**Multilingual worship:**
- Use **Chinese Trilingual mode** per-section for bilingual songs
- Use **Color Preset buttons Py: 4 / 5** to set a distinct colour for pinyin
- Pinyin spacing (blank lines between lines) is adjustable in Source Properties

**Network:**
- All devices must be on the same Wi-Fi network as the OBS computer
- The default port is **8765** — ensure it is not blocked by a firewall
- If the QR code doesn't scan, type the URL directly: `http://[OBS-computer-IP]:8765`
- The remote page auto-reconnects if the connection drops (e.g. OBS restart)

---

*OBS Lyrics Dock v10.2.0 — built with OBS Studio Plugin SDK and Qt*
