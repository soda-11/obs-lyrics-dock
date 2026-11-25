# obs-lyrics-dock — Change Log

Purpose: record **what** changed and **why the code works the way it does**, so
future-us can answer "why/how is this done?" without re-deriving it.

Rules for this file
- Updated **only when the owner asks** — not on every build or version bump.
- Each entry gives: behaviour, reasons, data model, edge cases, known limits, files.
- Newest entry first.

---

## STANDING RULE — the card queue must always match its segment  (follow this every time)

**What it is.** Every Lyrics, Paragraphs and Media segment keeps an ordered list of slide cards,
`Segment::cards` (the segment's *card queue*): exactly one card per slide (or per media file), in
on-air order. Each card has a permanent id and its own data (today: its background). Presentation
segments have no card queue. The service itself is the ordered list of segments (`m_segments`).

**The invariant.** `cards` always has the same number of entries, in the same order, as the
slides/files the segment currently produces. A card's id and data follow the card through edits;
they are never tied to a position.

**The process — always:**
1. Anything that changes a segment's slide text, sections, paragraph text or media list must call
   `Segment::syncCards()` **immediately afterwards** (for a global change that alters slide counts,
   such as lines-per-slide, call `syncAllCards()`).
2. Never create, delete, reorder or renumber cards by hand. Cards are only created and carried over
   by `syncCards()` (it matches unchanged slides by content, pairs edited slides in order, and
   mints ids only for genuinely new slides).
3. Never read or write a card by position without being sure it is in step: call `syncCards()`
   first (it is cheap and changes nothing when already in step).
4. User imports mint new segment and card ids (`remintIds()`); loading the app's own saved
   service keeps them.
5. A new feature that stores something per slide card stores it **on the `SlideCard`** — not in a
   map keyed by slide index.

**Where `syncCards()` is called today** (all places that change content): loading any service
file; user imports; adding a song from the library; editing a song in the dialog; editing a single
slide (lyrics or paragraph); editing paragraph text in the Edit dialog; changing the media list;
changing lines-per-slide. **Safeguards** (v12.30.0): the queue is also re-aligned whenever a segment
is opened for display and before a card background is assigned or cleared, so it cannot be out of
step when a card is touched.

**Why this matters.** Before card ids, a card was only a position, so inserting or deleting a slide
shifted every later card's background onto the wrong card. The queue + ids are what prevent that.
Skipping the sync after a text/media change brings that bug back (and a background assigned while
the queue is out of step can be silently dropped).

**Review checklist for any change:** search the change for writes to `.text`, `.sections`,
`mediaPaths`, or for new per-slide data, and confirm each is followed by `syncCards()` / stored on
the card.

---

## STANDING RULE — the Live Preview always highlights the on-air segment's chip  (follow this every time)

**Requirement (owner).** In the remote's Live Preview, the chip (button) of the segment that is on air
must **always be highlighted**, whatever put the slide on air.

**What "highlighted" means**
- It is clearly visible: an accent-coloured outline and a stronger fill (not a subtle tint).
- It is **kept in view around the middle of `#lpSection1`**: whenever the highlighted chip changes, the
  strip scrolls smoothly to centre it (it does not move again until the highlighted chip changes, so the
  operator can still scroll the strip by hand).
- The chips are the **same height as the Prev / Next buttons**.
- The highlighted chip also shows the slide on air and the segment's slide count (e.g. "3/8").

**Every way a card can go on air must keep the chip right.** The highlight comes from the plugin's `onAir`
message (`publishOnAir()`), which is sent from `rememberOnAir()`. These all do it today:
a click on a lyrics / paragraph slide (edit mode and live mode), a media card (live-mode card, or a file
started from the Media tab in edit mode), a presentation starting / stopping / blanking, a slide chosen
on the remote (`activateSlide`), Next / Previous, and blackout (nothing on air).

**Process for any change that puts something on air**
1. Call `rememberOnAir(segment, cardIndex)` (or `publishOnAir()`) for it.
2. Do **not** call `setActiveSlide(-1)` *after* recording it: that clears the on-air record and tells the
   remote that nothing is on air (this is exactly what broke the highlight for media cards, v12.40.0).
   Record the card after any such call.
3. Test in the Live Preview: the right chip lights up, moves to the middle of the strip, and shows n/m.

---

## FEATURE REFERENCE — Audio Bin  (unique feature, WORKING — do not break)

**What it is.** A background-music playlist in the bottom of the right-hand panel ("AUDIO BIN"):
a list of audio files, single-click to play, "+ Add", "■ Stop", right-click → Play / Remove, a
status line with the playing file. Files: mp3, wav, aac, m4a, ogg, flac, wma. When a track ends
the next one starts; at the end of the list it stops. It can also be driven from the remote.
It is independent of slides, video and presentations: music keeps playing while cards change.

**How the audio gets into OBS (the channel).**
1. The plugin's own OBS source (the "Lyrics" source, id `obs-lyrics-source`) is registered with
   `OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW`. Because it is an *audio* source,
   OBS gives it a mixer channel — the Audio Bin's sound shows up under the Lyrics source in the OBS
   mixer, with a level meter, volume, mute, monitoring and track routing.
2. Playing a file calls `lyrics_renderer_audio_push(path)`, which creates a **private
   `ffmpeg_source` child** named `lyrics_audio_bin` (`loadAudioSlot`) — audio-only, not looping,
   no hardware decode, not restarted on activate. It is attached as an *active child* of the Lyrics
   source (`obs_source_add_active_child` + `obs_source_inc_active`) so OBS decodes and plays it
   even though nothing renders it on a scene.
3. An **audio-capture callback** (`obs_source_add_audio_capture_callback` → `mediaAudioCb`)
   receives that child's decoded audio and re-emits it as the Lyrics source's own audio with
   `obs_source_output_audio`, using OBS's current sample rate / speaker layout
   (`audio_output_get_info`). That is the single door by which every plugin sound reaches OBS.
4. Stopping calls `lyrics_renderer_audio_clear()` → `destroyAudioSlot`: blank the file first (so
   decoding stops at once), stop the media, remove the callback, release the child.

**One slot of its own.** The Audio Bin uses `inst->audioSrc`, separate from the background layers
(`bgSlots`, which carry video audio) and from the MV music source (`musicSrc`). That is why music
and a background video can play at the same time.

**Priority rule — the Audio Bin wins.** When the Audio Bin starts, `lyrics_renderer_mute_media_audio(true)`
sets a flag read inside `mediaAudioCb`: the callback drops audio from every source except
`audioSrc`, so video/MV sound is silenced but the picture keeps playing. When the Audio Bin stops
(`playbackStopped`) the flag is cleared and video audio comes back. *Why:* worship music must not be
talked over by a clip's soundtrack, and the operator should not have to mute the clip by hand.

**Auto-next.** A 500 ms timer asks the renderer for the Audio Bin source state
(`lyrics_renderer_audio_get_state()`); when it is `OBS_MEDIA_STATE_ENDED` (6) the next file plays.

**Lifecycle / safety.**
- Starting a track always stops the previous one first (one track at a time).
- `stopAllMedia()` stops the Audio Bin too.
- If the Lyrics source stops being active in OBS (hidden / scene switched away), `src_deactivate`
  destroys the audio slot, so music does not keep playing in the background; `src_activate` also
  clears any leftover. **For the Audio Bin to be heard, the Lyrics source must be in the live scene.**
- Missing files are skipped when a list is imported (`setFiles`).

**Saved state.** `audio-bin.json` → `{ "files": [paths] }` (beside `service.json`), written on add,
remove and import. The list is also part of the service export (`audioBin`).

**Remote.** The server broadcasts `audioBinList` (the files) and `audioBinState` (active index +
playing); the remote sends `audioBinPlay` (index) and `audioBinStop`.

**History.** v12.7.0 audio/video mutual exclusion → v12.13.0 audio and video made independent →
v12.14.0 Audio Bin takes priority over video/media audio → v12.15.0 fix video stop on audio play +
auto-next-track. It is working well as of v12.30.0.

**Do not change casually (regression guard).** The private ffmpeg child + audio-capture callback
re-emit path, the `s_muteMediaAudio` check (`src != audioSrc`), the separate `audioSrc` slot, and the
source activate/deactivate cleanup. Anything touching the renderer's audio or media slots must be
re-tested with: music + video together, music only, music stop, a track ending, switching scenes.

**Known limits (not bugs, just how it is).** There is no volume control inside the plugin (use the OBS
mixer on the Lyrics source). The list is identified by position and by file path (the same file
cannot be added twice). The `videoActivated` signal on the gallery is no longer connected to anything
(video no longer stops the Audio Bin since v12.13.0); its old comment is stale.

**Files.** `src/audio-bin.cpp/.hpp` (list, playback control, persistence, auto-next),
`src/lyrics-renderer.cpp` (`loadAudioSlot`, `destroyAudioSlot`, `mediaAudioCb`,
`lyrics_renderer_audio_push/clear/get_state`, `lyrics_renderer_mute_media_audio`),
`src/lyrics-dock.cpp` (wiring + remote entry points), `src/websocket-server.cpp` (remote messages).

---

## v12.44.0 — Export / Import service: what is in the file and how each import path behaves  [DONE]

Audit of Export Service / Import Service (File menu), the web-editor import and the remote import.

### EXPORTED (`buildFullServiceExport`, file `version` 2)
**Service-wide**
- `linesPerSlide`, `globalFadeMs`
- `colorPresets` — the 6 presets (font + outline colour)
- `sourceSettings` — the OBS Lyrics source settings as a JSON string: the global **lyrics layout**, the
  **default paragraph layout** (template for new segments), fonts / sizes
- `gallery` — Graphics Bin file paths, in order
- `audioBin` — Audio Bin file paths, in playlist order
- `segments` — see below

**Each segment** (Lyrics, Paragraphs, Media; Presentation segments are NOT exported)
- always: `type` (`song` / `paragraphs` / `media`), `id`, `title`, `artist`, `text`, `filePath`
- Lyrics: `sourceSongId` (library song id, always written), `sections` (type, label, text, isChinese,
  showPinyin, showTranslation, pinyin, english), `songKey`, `songTempo`, `songCopyright`
- Media: `mediaPaths`
- `cards` — the slide-card queue: `id`, `key` (content fingerprint), `bg` (the card's own background)
- `defaultBackground`, `colorPresetIdx`, `altSentenceColor`, `altSentencePreset`
- Paragraphs: `layout` — the segment's complete own layout. A paragraph never opened yet (still following
  the default) is written with the layout it would use now, so EVERY paragraph carries its layout.

**Not in the file:** Presentation segments (window binding / COM), the song library itself, media files
(paths only — files must exist at the same place), what is on air, Audio Bin playback settings (none exist),
confidence-monitor text size (kept in the remote's browser).

### IMPORT — Import Service (`onImportService` → `applyFullServiceImport`)
- Asks: **Replace All** (also restores colour presets, source/layout settings, Graphics Bin, Audio Bin,
  lines per slide, fade) or **Keep My Layout** (segments only).
- Always replaces every non-Presentation segment; existing Presentation segments are kept.
- Every imported segment and slide card gets NEW ids (`remintIds`), so ids in the file can't clash.
- A Lyrics segment with no embedded sections is filled from the library by `sourceSongId`.
- Legacy: `"items"` key; bare top-level array; old type names (`scripture`, `paragraph`, `video`, `music`;
  a missing/unknown type = song); old Lyrics id → `sourceSongId`; `slideBgs` / section backgrounds →
  card backgrounds; `windowExe` → `windowTitle`; `sourceSettings` as string or object.
  A file with no `version` (v1) has all backgrounds and colour presets of segments stripped.
- The count shown counts every entry except presentations.
- After import: selection and slide grid reset, queue re-sent, `publishOnAir()` so the remote drops the
  old highlight.

### IMPORT — web editor (`onImportFromWeb`)
- A service file (`segments`/`items`) REPLACES the whole service (presentations too); ids re-minted; songs
  missing from the library are added to it from the embedded sections. A bare array of songs is added
  to / updated in the library and appended to the service. Now also resets the selection / grid and
  publishes the on-air state (it did not before).

### IMPORT — from the remote (`importSegmentsFromRemote`)
- Replaces Lyrics and Paragraph segments only; backgrounds are stripped, colour preset 0, ids re-minted;
  now also publishes the on-air state.

**Files:** `src/lyrics-dock.cpp`.

---

## v12.40.0 — Media cards clicked in the plugin highlight their chip  [DONE]

**Bug:** in the Live Preview, the chip was not highlighted when the operator clicked a slide card of a
**media** segment in the plugin. **Cause:** in live mode `onLiveMediaCardClicked` ended with
`setActiveSlide(-1)`, which clears the on-air record and publishes "nothing on air"; the media card itself
was never recorded. In edit mode the Media tab (`MvVideoPanel::playVideoAt`) played the file but told the
plugin nothing about which file. (Media cards activated *from the remote* already worked.)

**Fix:** the media card is recorded as on air (`rememberOnAir`) right **after** the `setActiveSlide(-1)`
call; in edit mode the Media tab now reports the file it started (`MvVideoPanel::videoStartedAt` →
`VideoTab::mediaActivated`) and the dock records it. See the standing rule above.
**Files:** `src/lyrics-dock.cpp`, `src/mv-tab.cpp/.hpp`.

---

## v12.39.0 — Live Preview chips: visible highlight, centred, same height as Prev/Next  [DONE]

- The on-air chip had only a faint tint and the strip never scrolled to it, so with many segments it was
  usually off-screen and looked un-highlighted. It now has a bright accent outline and fill, and the strip
  scrolls it to the middle of `#lpSection1` when it changes (`lpCenterActiveChip`, only when the
  highlighted chip changes).
- The chips stretch to the full height of the strip (`#lpSection1 { align-items: stretch }`), the same height
  as the Prev / Next buttons.
- **Files:** `data/remote.html` (Live Preview only).

---

## v12.38.0 — A slide with no background shows no background  [DONE]

### The rule
Every slide change goes through one function, `LyricsDock::showBackground()`. The new slide's
background is its own, else the segment's default, else **none**. "None" takes the previous
background off air; if the new slide uses the **same file**, it simply stays (no reload, no flicker).
Used by: clicks in edit mode and live mode (lyrics and paragraphs), selecting a slide on the remote,
Next / Previous, and editing a background while its slide is on air.

### What "off air" means
- A **video** fades out over the global fade setting and is then unloaded; a **photo** is removed
  immediately (the renderer's procedure: pictures never fade — see "How the photo / video fade works").
- OBS and the remote's Live Preview show the same thing: the remote's preview image is a capture of
  OBS's own output (`ws-tab.cpp`), so it has no background logic of its own. The remote's media
  highlight is updated too: `showBackground` now calls `broadcastMediaState` (the path when a
  background is put up, empty when it is cleared).

### The bug that was fixed
Several panels reset the plugin's record of the background on air (`m_currentBgPath`) while their
photo/video stayed on screen: the Media tab's `playbackStarted`, toggling live mode, and starting a live
media card. A slide with no background only cleared when that record was non-empty, so the old media
stayed behind it. Now a blank slide also clears when the renderer reports a media background is up
(`lyrics_renderer_media_background_active()`: a media background was pushed and it is not an MV video).
The two inline copies of the logic in the live-mode click code were replaced by `showBackground`.

### Decisions and exceptions (owner)
- A **music-video (MV tab) video** is not cleared by a blank slide: lyrics are meant to sit over it.
- **Photos do not fade** — kept as is. Photo → photo is an instant cut.
- **Manual typed-text pushes** (the remote's own Paragraph / Scripture tabs) and the **Blank** button
  change or clear the text only, never the background — kept as is.

### Files
`src/lyrics-dock.cpp` (`showBackground`, live-mode blocks), `src/lyrics-renderer.cpp/.hpp`
(`lyrics_renderer_media_background_active`).

---

## v12.37.0 — The remote only puts a card on air  [DONE]

**Rule:** the remote asks for ONE thing — `activateSlide` (or `lpNext` / `lpPrev`). The background, font
colour, layout and audio all come from the plugin's saved segment settings, so the remote can never
request a different one. Selecting a segment in the remote must not change what is on air.

**Removed from `data/remote.html`:**
1. the background push in `selectServiceItem` (selecting a paragraph segment pushed its first slide's
   background);
2. the dead `pushParagraphSlide(serviceIdx, slideIdx)` — never called, shadowed by a second function of
   the same name, and it sent the paragraph text and a background itself (using only the card's own
   background, ignoring the segment default);
3. the `audioBinStop` that `activateSlide` sent when a video card was activated — the Audio Bin and
   video are independent (v12.13.0), so putting a video on air must not stop the music.

**Left alone (explicit user actions):** the media-strip thumbnails (`pushMedia`), the colour preset
buttons, blackout. **Files:** `data/remote.html`.

---

## v12.36.0 — Live Preview follows the plugin  [DONE]

### The problem
The Live Preview's Next / Previous were computed **inside `remote.html`** from a local note of which
segment was active (`localActiveSvc`). `updateSlideActive` resets that note to −1 whenever a lyrics
slide becomes active, so Next counted from segment 0, slide 0 — "segment 1 slide 2". Also the service
list was sent only to managers, so the Live Preview's queue was frozen at connect time.

### The design — the plugin is the source of truth
- **`onAir` message** (`type, segIdx, segId, slideIdx, cardId, kind`) sent to managers and the Live
  Preview whenever what is on air changes, from any source (`rememberOnAir` / `publishOnAir`: clicks in
  the plugin, remote selections, Next/Previous, blackout, a presentation starting/stopping/blanking), only
  when it actually changed, and cached so a client that connects later receives it at once. A showing
  presentation takes precedence.
- **The queue is always current:** `serviceOrder` items now carry the segment `id` and `cards` (card ids),
  and the list is re-sent to managers **and** the Live Preview after every `saveServiceOrder()`.
- **The remote only follows.** It highlights the on-air segment (chip) and shows the slide on air and
  the segment's slide count ("3/8"); if the list changes it finds the on-air segment again by id. Its
  arrows send only `lpNext` / `lpPrev`.
- **The plugin decides what comes next** (`navigateQueue`): from the on-air card (found by id) to the
  next card, then to the first card of the next segment that has cards (Previous: the last card of the
  previous one); a Presentation counts as one stop. Activation goes through `activateSlideFromRemote`.
- **Presentation limitation (owner):** while a presentation is on air the arrows drive the presentation
  (PowerPoint slides) and **never** move to the next segment — the operator selects the next segment by
  hand.
- Activating any other segment from the remote now stops a running presentation, and paragraph / media
  activations are recorded as on air.

### Scope and known limits
Only the Live Preview part of `remote.html` was changed. The rest of the remote is still position-based;
the larger rework by card id is waiting for the owner's procedures. In edit mode a paragraph activated from
the remote does not move the plugin's own highlight.

### Files
`src/lyrics-dock.cpp/.hpp`, `src/websocket-server.cpp/.hpp`, `data/remote.html`.

---

## v12.35.0 — Live-mode Next/Previous used the wrong slide number  [DONE]
In live mode the plugin worked out the current position as `m_currentSongSlideOffset + m_activeSlide`.
After a remote activation that offset counted **songs only**, while the live grid numbers **every
segment's slides**, so with paragraph segments before the song Next jumped to an unrelated slide.
Fix: `liveGlobalIndexOnAir()` finds the position from the on-air card's id, and a live lyrics click now
broadcasts the remote's own (songs-only) slide number. The two numberings are written down in
`CLAUDE.md` (5a). **Note:** the Live Preview's arrows no longer use this path since v12.36.0 (the plugin
walks the queue itself), so the real cure for the remote symptom is v12.36.0.
**Files:** `src/lyrics-dock.cpp/.hpp`.

---

## v12.34.0 — Edit Slide "Apply" and selecting a segment  [DONE]

**Bug 1 — Edit Slide Apply (column 3) pushed the raw typed text.** Adding an extra blank line inside a
paragraph slide splits it into two cards (two blank lines start a new slide). Column 2 correctly showed
the first part, but OBS received everything that was typed. Apply now looks the on-air card up by id
after the edit and pushes what that card **now contains** (the first part); if the edit deleted the
card, nothing is pushed. Rule: after an edit, push what the card now contains, never the raw text.

**Bug 2 — selecting a segment recoloured the on-air slide.** `loadSegmentContent` put the segment's
colour preset into the renderer immediately, and the colour setters re-render what is on air. Now
selecting a segment only updates the panel (card label colour, highlighted preset button). The preset
reaches the renderer when one of that segment's slides is pushed (`applySegmentPreset`, called from
`applyParagraphStyle`, `pushCurrentSlide` and `pushSlideFromRemote`), and the three colour setters
(`lyrics_renderer_set_text_color`, `_set_paragraph_text_color`, `_set_chinese_color`) take
`repush=false` to store colours without re-rendering. Rule: **selecting a segment must never change
what is on air.**

**Files:** `src/lyrics-dock.cpp/.hpp`, `src/lyrics-renderer.cpp/.hpp`.

---

## v12.33.0 — The Layout button edits the segment; the default layout lives in OBS Properties  [DONE]

**What changed**
- The **Layout** button at the top of column 2 now opens **the selected paragraph segment's own
  layout** (the same dialog as "Layout for this segment…" / "Segment layout…"; it does nothing if no
  paragraph segment is loaded). With a Lyrics segment selected it still opens the lyrics layout,
  unchanged.
- The **default paragraph layout** (the template copied into each new paragraph segment) is now edited
  **only in the OBS source's Properties** (right-click the Lyrics source in OBS → Properties). Its
  groups are labelled "4. Paragraph — Title … [DEFAULT for NEW paragraph segments]" and "5. Paragraph —
  Text … [DEFAULT for NEW paragraph segments]", and the three numeric fields read "Paragraph DEFAULT:
  title strip height / left-right margin / body text top offset". The separate default Paragraph
  Layout dialog is gone from the dock.
- This replaces the v12.32.0 statement that the default is edited with the main-panel Layout button.

**Why:** the button on the panel is what the operator uses every day, and what they want to change
there is the segment they are working on; the default is a set-up-once template and belongs with the
source's settings, out of the way. Nothing else about the layout rules changed: segments still own a
complete copy, editing the default (in Properties) never changes an existing segment, and editing one
segment never changes another.

**Files:** `src/lyrics-dock.cpp/.hpp` (button handler, tooltips, removed the default dialog),
`src/lyrics-renderer.cpp` (Properties labels), `src/layout-dialog.cpp`.

---

## v12.32.0 — Paragraph layout: the default is a template, every segment owns its layout  [DONE]

### The behaviour (owner's specification)
- **The default is a template.**
  - The main-panel Layout button is renamed **"Paragraph Layout - DEFAULT copied into NEW
    paragraph segments"**.
  - When a paragraph segment is created, it gets a **complete copy** of the default as it is at
    that moment.
- **Segments are independent.**
  - Changing segment A's layout never changes segment B.
  - Changing the default never changes a segment that already exists.

### Why it was redone
v12.30.0 stored only the settings a segment had changed and let everything else follow the global
layout live. So a change made through the main Layout button reached every segment that had not
overridden that setting — the opposite of "A must not affect B". The default is now a *reference to
copy from*, not a live parent.

### What a segment's layout contains
`segments[].layout` (Paragraphs only), the same keys as the OBS source settings: for the title strip
and for the body text — font, size, bold, italic, horizontal and vertical alignment, opacity,
outline on/off, outline width, background box on/off, its colour and opacity — plus the title
strip's text and outline colour, the title-strip height, the left/right margin and the body-text top
offset. **Body-text colour and body outline colour are not part of it:** they always come from the
segment's colour preset, so the two features never fight.

### How the copy is made and kept
- `lyrics_renderer_paragraph_layout_snapshot()` reads the current default (the OBS source settings
  edited by the default Layout dialog) as a complete JSON object.
- `LyricsDock::ensureParagraphLayout(segment)` copies in whatever the segment lacks. It runs when a
  paragraph segment is **created**, when it is **opened**, and when its slide is **shown**.
- **Segments from before this change** (and imported ones without a layout) therefore receive their
  copy the first time they are opened or shown, taken from the default at that time; they then
  look as they do today and are independent from then on. A segment holding only the partial layout
  from v12.30.0 is completed the same way for the keys it lacks. The result is saved immediately.
- The renderer still merges the segment's layout over the settings into a temporary copy when
  rendering (`lyrics_renderer_set_paragraph_layout` → `updateScriptureChildren`); the global settings
  are never modified. Because a segment's layout is complete, later changes to the default cannot
  reach it.

### The dialogs
- **Default (main-panel Layout button):** edits the template. Applying it changes nothing that is
  already on air or in an existing segment.
- **Segment layout** (right-click the slide area of a paragraph segment → "Layout for this
  segment…", or "Segment layout…" in the Edit Paragraphs dialog): the same dialog in segment mode.
  "Apply to This Segment" stores the **full** layout. "Load Default Layout" only fills the form with
  the current default (it no longer closes the dialog); the segment keeps its own layout until
  Apply is pressed.

### Known limits
- A legacy paragraph segment that has never been opened or shown still follows the default until its
  first open/show (then it takes its copy).
- Lyrics layout is still global (not per segment) — a separate decision.

### Files
`src/lyrics-renderer.cpp/.hpp` (snapshot), `src/layout-dialog.cpp/.hpp` (segment mode stores all keys,
"Load Default Layout", titles), `src/lyrics-dock.cpp/.hpp` (`ensureParagraphLayout`,
`Segment::paragraphLayout`, creation / open / show hooks).

---

## v12.31.0 — On-air tracking by id  [DONE]

### What changed
What is on air is now remembered **by id, not by position**:
- the slide card on air — `m_onAirSegId` + `m_onAirCardId` (recorded in `setActiveSlide` and where a
  live slide is pushed, via `rememberOnAir`);
- the live presentation card — `m_activePptSegId`;
- the playing / paused live media card — `m_liveMediaSegId` + `m_liveMediaCardId`
  (`isLiveMedia`, `setLiveMedia`, `clearLiveMedia`, `liveMediaPath`; presentation: `isLivePpt`,
  `setLivePpt`).

### Why
A position number is only right while nothing before it changes. After an edit that inserts or
removes a slide, the old "slide number 3" is a different card. The edit paths used to re-show that
position, so the wrong text could go live. The plugin now looks the on-air card up by its id.

### How it is used
- `onAirIndexIn(segment)` returns where the on-air card sits **now** (−1 = nothing on air in this view,
  or the card was deleted). It must be called **before the slide grid is rebuilt**, because the rebuild
  resets `m_activeSlide`.
- Used by: editing a song (dialog), editing a single slide, editing paragraph text, and changing
  lines-per-slide (which restores the highlight). If the on-air card was deleted by the edit, nothing
  is re-pushed — the screen is left as it is instead of showing another card.
- Blackout clears the remembered card, so a later edit cannot bring the slide back.
- **Bug fixed on the way:** after editing paragraph text / the alternate-colour settings the on-air
  slide was supposed to be re-pushed, but the check ran after the grid rebuild had reset the active
  slide, so it never fired. It now uses the id found before the rebuild.

### What stays positional
- `m_activeSlide` is still the working index inside the active segment (display, next/previous);
  the id decides which card it points at after edits.
- The remote protocol is still position-based. **Direction from the owner:** `remote.html` needs a
  major rework tracked by card id — so the remote knows what is on air, what is being edited and
  must be updated, and can push what is selected on the remote to the plugin. The owner will provide
  the procedures; nothing in the remote was changed.
- The slide-grid rebuild still clears the live media / presentation tracking, exactly as before.

### Files
`src/lyrics-dock.cpp/.hpp`.

---

## v12.30.0 — What a segment stores; per-segment Paragraph layout; Media card ids  [DONE]

### What each segment type stores (service file, `segments[]`)
Common to every segment: `type`, `id` (unique uuid), `title` (the segment name).

| Type | Stored | Notes / why |
|---|---|---|
| **Paragraphs** | `text` (the body; slides are split on two blank lines), `colorPresetIdx`, `altSentenceColor` + `altSentencePreset`, `defaultBackground`, `cards[]`, `layout` | `colorPresetIdx` is only an index into the six colour presets; the colours themselves live in the dock's presets. `layout` = this segment's own complete layout, copied from the default layout when the segment was created (below). |
| **Lyrics** | `sourceSongId` (library song id), `artist`, `songKey`, `songTempo`, `songCopyright`, `sections[]` (type, label, text, Chinese flags, pinyin, translation), `colorPresetIdx`, `defaultBackground`, `cards[]` | The lyrics are copied into the segment (it is independent of the library); `sourceSongId` is only used to save back. Layout is global for Lyrics (decision: Lyrics layout is complex and was not asked to be per segment). |
| **Presentation** | `windowTitle`, `windowProc` (exe), `windowCls` (window class), `pptComMode` (-1 auto, 0 off, 1 on) | "Is PowerPoint" is not stored: it is derived from `windowProc == POWERPNT.EXE`; `pptComMode` is the user's override. The window handle is never stored (changes every run). |
| **Media** | `mediaPaths[]` (photos and videos), `cards[]` | One card per file. Playing / paused is runtime state. |

`cards[]` (Lyrics, Paragraphs, Media): `{ id, key, bg }` — the card's permanent id, a content
fingerprint used to follow the card through text edits, and its own background.

**Kept outside segments (service-wide):** the six colour-preset definitions, global fade length,
lines-per-slide, the default layouts (font/size/margins per category; the paragraph default is copied into each new paragraph segment), gallery and audio bin, and
which segment was last active.

### Per-segment Paragraph layout  [DONE — semantics corrected in v12.32.0]
- **Each paragraph segment owns a COMPLETE layout** (`segments[].layout`: font, size, bold/italic,
  alignment, opacity, outline, background box and the title colours for the title strip and the body
  text, plus title-strip height, left/right margin and body top offset). Changing segment A's layout
  never changes segment B, and changing the default never changes an existing segment.
- **The default layout is a template, not a live parent.** The Layout button on the main panel
  ("Paragraph Layout - DEFAULT copied into NEW paragraph segments") edits the default. When a new
  paragraph segment is created, the default at that moment is **copied** into the segment. *Why:* the
  owner wants segments to be independent; a live "inherit from global" made every segment change
  whenever the global changed (this is what the first version of the feature did, and it was wrong).
- **Segments created before this feature** (or imported without a layout) get their copy the first
  time they are opened or shown, taken from the default at that time. A segment that still has only a
  partial layout from v12.30.0 is completed the same way for the keys it lacks.
- **Open a segment's layout:** right-click the slide area of a paragraph segment → "Layout for this
  segment…", or the "Segment layout…" button in the Edit Paragraphs dialog. Same dialog as the default,
  in segment mode: "Apply to This Segment" stores the full layout; "Load Default Layout" fills the form
  with the current default (press Apply to keep it).
- **Body-text colour and outline colour are not part of a segment's layout** — they always come from the
  segment's colour preset, so the two features never fight. (The title strip's colour is part of it.)
- **How it is applied:** before each paragraph slide is pushed the dock calls `applyParagraphStyle()`
  (the segment's layout + its alternate-sentence colour). The renderer merges the segment's layout
  over the settings into a temporary copy (`lyrics_renderer_set_paragraph_layout` →
  `updateScriptureChildren`); the global settings are never modified. The default is read with
  `lyrics_renderer_paragraph_layout_snapshot()`. The slide is re-rendered only when the layout changed.
- Imports/exports carry each segment's layout with the segment.

### Media card ids  [DONE]
Each file of a Media segment now has a card (`SlideCard`) with a permanent id, kept in sync with
the media list (`syncCards()` after every list change). *Why:* same identity rules as Lyrics and
Paragraph cards. The live media cards and the "which media is playing" tracking still use the
file's position in the list; moving them to ids is part of the remaining index-based tracking.
The same file listed twice in one segment cannot be told apart when one is removed.

### Files
`src/lyrics-dock.hpp/.cpp` (`Segment::layoutOverride`, `applyParagraphStyle`,
`showSegmentLayoutDialog`, media cards), `src/layout-dialog.hpp/.cpp` (segment mode, `LayoutSink`),
`src/lyrics-renderer.hpp/.cpp` (`lyrics_renderer_set_paragraph_layout`, merged settings).

---

## v12.29.0 — Segment ids, slide-card ids and card queues  [DONE]

### The problem
Slide cards were only positions. A card's own background was stored by slide index
(`slideBgs[2] = …`), so inserting or deleting a slide shifted every later card's background onto
the wrong card, and a Lyrics segment's id was the library song id (so a segment could not be
told apart from its song).

### The model now
```
Service  (m_segments = the segment queue, in service order)
 └─ Segment  (id = unique uuid, every type)
     └─ cards[]  (the card queue: SlideCard {id, key, background}, in on-air order)
```
- **Every segment has a unique `id` (uuid)** — Lyrics included. A Lyrics segment keeps the
  library song's id separately in **`sourceSongId`**; the segment is an independent copy and the
  id is used only to save back to the library. The song edit dialog and "Save to Library"
  work with `sourceSongId`; "Save as New Song" changes only `sourceSongId` (the segment keeps its
  own id). `m_sequenceSongIds` is now a list of Lyrics *segment* ids.
- **Every slide card of a Lyrics/Paragraph segment has a permanent id** (`SlideCard::id`, uuid)
  plus its own `background`. Backgrounds are keyed by card id, not by position, so they stay with
  their card when slides are inserted, removed or the text is edited. A card's effective
  background is still its own, else the segment's `defaultBackground` (v12.28.0 rules).
- **Why the slide TEXT is still derived, not stored on the card:** every renderer, remote, edit
  and live path reads text from the segment's sections / paragraph text. Moving the text into
  cards would have meant rewriting all of them at once. The id layer fixes the real bug now and
  leaves storing text on cards possible later.
- Lookup helpers: `Segment::cardIdAt(index)`, `Segment::cardIndexById(id)`,
  `LyricsDock::segmentIndexById(id)`.

### How the cards follow the text (`Segment::syncCards`)
Each card keeps a fingerprint (`key`) of its content. After ANY change to a segment's text or
sections the cards are re-aligned with the slides the text now produces:
1. Cards whose content is unchanged are matched with a longest-common-subsequence over the
   fingerprints — inserting or deleting a slide does not disturb the others.
2. Between two matched cards, leftover old and new cards are paired in order — an **edited** card
   keeps its id and background.
3. Surplus new slides get fresh ids; surplus old cards are dropped.
*Why a diff and not "match by position":* position matching would shift every id after an
insertion — the same bug again. `syncCards` runs after: editing a song in the dialog, editing a
single slide, editing paragraph text, adding a song from the library, loading any file, and when
the lines-per-slide setting changes (`syncAllCards`).

### Saving, loading and importing
- Service file: `segments[]` now carry `sourceSongId` (Lyrics) and
  `cards: [{id, key, bg}]`; the file `version` is 2.
- **Loading the app's own saved service keeps all ids.** *Why:* the remote and the "current card"
  rely on ids staying the same across restarts.
- **Old files are migrated on first load:** the original is copied once to
  `service.v1.bak.json`, a Lyrics segment's old id (the library song id) becomes `sourceSongId`
  and it gets a new segment id, cards get ids, index-keyed backgrounds (`slideBgs`) and the older
  section backgrounds are moved onto the matching cards, and the new form is saved immediately
  so the ids minted are the ones every later run sees.
- **Every user-initiated import mints new segment and card ids** (export-file import, web-editor
  import, remote import). *Why:* the file's ids may already exist in the current service.
  Backgrounds, text and library links come across unchanged.

### Library and duplicates
- Deleting a library song, adding the same song twice, and renaming no longer affect segment
  identity. The library popup still offers each song once (it works in library song ids), so the
  UI does not yet let you add the same song twice, but the data model allows it.
- The popup's delete warning now says segments already in the service keep their own copy.

### Remote / wire format  [unchanged on purpose]
The remote protocol and `remote.html` were not touched. The wire still sends the library song id
as a Lyrics song's `id`, and per-slide backgrounds as `slideBgs` (index → path), now derived from
the cards. Moving the remote to card ids is a later change, to be done together with `remote.html`.

### Known limits
- The current slide on air and the live-card tracking (`m_activeSlide`, `m_activePptSegIdx`)
  are still indexes; they are not yet followed by id.
- A card's text is derived, so a card whose text is rewritten completely AND moved in the same
  edit may be treated as a new card (it loses its background).
- Several identical slides in one segment (for example a repeated chorus slide) cannot be told
  apart; deleting one may drop a different one's background.
- Changing lines-per-slide regroups the slides; ids are carried over in order for the slides
  whose content changed, so a card's background may land on its neighbour in that case.

### Files
`src/lyrics-dock.hpp/.cpp` (`SlideCard`, `Segment` ids / cards / `syncCards` / `remintIds`,
JSON, import paths, library link, edit paths, migration), `src/song-manager.hpp` (comment).

---

## v12.28.0 — Segment default background and slide-card background inheritance

### How the photo / video fade works today (reference)  [existing behaviour, documented]
Where: `applyBgPath()` in `lyrics-renderer.cpp` (the renderer), called through
`lyrics_renderer_media_push(path, fadeMs)` / `lyrics_renderer_media_clear(fadeMs)`.
The dock decides *when* to call it (`LyricsDock::showBackground`); the renderer decides
*how* to change.

- **Four layers**, drawn bottom to top: slots 0 and 1 = pictures (also presentation stills),
  slots 2 and 3 = videos. Each slot has a state: INACTIVE, WAITING (source created, not ready
  yet), ACTIVE, FADING_IN, FADING_OUT.
- **Fade length** = the global "fade" setting (spin box, 0–3000 ms, step 50, default 500 ms,
  saved with the service as `globalFadeMs`), read through `MediaGallery::fadeDurationMs()`.
  `0` means every change is instant. While a fade runs the layer is drawn at
  `alpha = elapsed / fadeMs` through an offscreen texture and an opacity shader.
- **WAITING:** a new source becomes visible only when it is ready (it reports a width) or
  after 300 ms (`BG_READY_TIMEOUT_MS`). Until then the old background stays on screen, so
  there is no black flash.
- **Transition rules:**

| From → To | What happens | Why |
|---|---|---|
| nothing → picture | appears instantly (slot 1) | nothing underneath to cross-fade with |
| picture → picture | instant swap: the new picture loads into the other picture slot, the old one is dropped | picture slots never fade |
| nothing/picture → video | video loads on top (slot 3) and **fades in**; pictures stay visible underneath and are removed when the fade finishes | videos are the only layers that cross-fade |
| video (top) → video | new video appears instantly in slot 2 *under* the old one, then the old one (slot 3) **fades out**, revealing it | new video must already be playing when revealed |
| video (slot 2) → video | new video loads in slot 3 and **fades in** on top; slot 2 is removed afterwards | same, other direction |
| video → picture | picture loads in slot 1 (WAITING until ready), appears instantly, and **triggers the video to fade out** over it | picture must be ready before the video leaves |
| anything → blank (clear) | videos fade out; pictures disappear instantly | |
| same file already showing | ignored (no restart, no re-fade) | avoids flicker/restarting a video |

- **Consequence worth knowing:** a *photo → photo* change is an instant cut, not a
  cross-fade; only changes that involve a video fade. This is how the renderer is built, and
  slide-card background changes below simply use it.
- **Dock guard:** `m_currentBgPath` remembers what is on air. `showBackground(path)` clears
  when the path is blank, does nothing when it is the same as on air, otherwise calls
  `lyrics_renderer_media_push(path, fadeMs)`.

### Background rules for Lyrics and Paragraph segments  [DONE]
- A **segment** has a **default background** (`Segment::defaultBackground`). Blank by
  default — nothing is shown unless someone assigns one.
- A **slide card** may have its **own background** (`Segment::slideBgs[cardIndex]`).
- **Effective background of a card = its own, otherwise the segment's default**
  (`Segment::backgroundFor(index)`). *Why:* one click sets the look for a whole song or text
  block, and only the cards that differ need their own.
- **"Apply current background to this segment (default)"** saves the gallery's current
  selection into the segment's default. It does **not** overwrite cards that already have
  their own background. (Before this version the same menu item copied the background onto
  every card — that made it impossible to tell a deliberate card background from a
  segment-wide one.)
- **"Apply current background to this slide card"** saves it into that card only.
- **"Clear this segment's default background"** removes the default; cards with their own
  keep it. **"Clear this slide card's own background"** removes the card's own entry, so
  the card inherits the segment default again.
- **When the card on air changes** the effective backgrounds are compared: same file → no
  change at all; different → `showBackground()` hands it to the fade rules above (so a
  segment with a default photo where one card has its own video fades that video in when the
  card is selected, and fades it out again when moving to an inheriting card); blank →
  cleared with the fade rules. Assigning or clearing a background while a card is on air
  re-evaluates that card immediately.
- The same rules are used in edit mode, live mode and slide-card thumbnails (a thumbnail shows
  the effective background). Paragraph cards used to keep the previous background when they had
  none; they now follow the same blank-clears rule as lyrics.
- Wording: menus and tooltips now say **segment** where they meant a service entry
  ("Apply … to this segment", "Rename Segment", "all segments").
- Media and Presentation segments have no default background (not applicable).

### What a segment and a slide card contain (Lyrics and Paragraph)
**Segment — Lyrics** (`Segment`, saved in the service file under `segments[]`):
`id` (today still the library song id; becomes a fresh uuid + `sourceSongId` in the id step),
`type` = song, `title`, `artist`, `songKey`, `songTempo`, `songCopyright` (copied from the
library so Save to Library can write the whole song back), `sections[]` (each: `type`,
`label`, `text`, `isChinese`, `showPinyin`, `showTranslation`, `pinyin`, `english`),
`defaultBackground`, `slideBgs` (card index → path), `colorPresetIdx`.

**Segment — Paragraphs:** `id` (uuid), `type` = paragraphs, `title`, `text` (one string;
slides are separated by two blank lines), `defaultBackground`, `slideBgs`, `colorPresetIdx`,
`altSentenceColor`, `altSentencePreset`.

**Slide card — today** (computed from the segment every time, not stored; identified only by
position):
- Lyrics, English: section label + the slide's text. Lyrics, Chinese: text + pinyin +
  translation + `isChinese`. (Slides come from the section text and the global
  lines-per-slide setting.)
- Paragraph: that slice of the body text; the title is shown from the segment.
- Position: index inside the segment (`slideIndex`) and a global index across the live
  sequence. Background: effective background as above (own or inherited).

**Slide card — planned (id step):** a stored object with `id` (service-wide uuid), `kind`
(text), the text (+ pinyin/translation for Chinese), the section tag (label, type, number,
Chinese flags — so a write-back can rebuild the song's Verse/Chorus sections), and
`background` (own; blank = inherit the segment default). Until then a card's own background
is keyed by its index, so inserting or removing a slide in the middle of a segment shifts the
backgrounds that were assigned to later cards — exactly the problem card ids will remove.

### Files
`src/lyrics-dock.hpp/.cpp` (`Segment::defaultBackground`/`backgroundFor`, `showBackground`,
`refreshAfterBackgroundChange`, assign/clear handlers, thumbnails, menu wording).

---

## v12.27.0 — Song library v2 and independent segments  [DONE; built in this version]
Implements the library items of v12.26.0 below:
- `songs.json` is `{ "version": 2, "songs": [...] }` without backgrounds; the first
  "add lyrics" upgrades an old file once and keeps `songs.v1.bak.json`.
- Backgrounds were removed from the song/section model; old section backgrounds found in
  saved services are moved onto the slide cards when the service loads (so nothing is lost).
- Segments are independent copies: editing a segment no longer writes to the library, and
  deleting a library song no longer removes it from the service. The segment edit dialog has
  "Apply to This Segment", "Save to Library" (asks first; re-creates the song under the same id
  if it was deleted) and "Save as New Song".
- A segment carries the song's key/tempo/copyright. *Why:* before this, saving an edit from a
  segment replaced the library song with a copy that had lost those fields.
- Still open from v12.26.0: segment id vs `sourceSongId`, uuids/queues for segments and cards.

---

## v12.26.0 — Segment / Section / Song library terminology, and the agreed id model

Status key: **[DONE]** built in this version · **[PLANNED]** design agreed with the
owner, not implemented yet (recorded here so the reasons are not lost).

### Terminology  [DONE]
```
Service  →  Segment[]  →  Slide cards[]
```
| Term | Meaning |
|---|---|
| **Service** | The whole event, in order. |
| **Segment** | One entry in the service. Four categories: Lyrics, Paragraphs, Media, Presentation. (Was `ServiceItem`.) |
| **Slide card** | One thing that goes on air. Kinds: text (optional background picture/video), media (picture or video), screen capture. |
| **Section** | A song-level part (Verse, Chorus…). Exists only inside a Lyrics segment's song. (`LyricsSection`) |
| **Song library** | A separate library of songs. Not the same thing as the service. |

*Why:* "section" meant two different things (a service entry and a song's Verse/Chorus).
That made every discussion and every piece of code ambiguous, so the service-level word
became **Segment** and "Section" is kept only for songs. The glossary is also in `CLAUDE.md`.

What the rename changed in code  [DONE]
- `ServiceItem`→`Segment`, `m_serviceItems`→`m_segments`, `m_serviceList`→`m_segmentList`,
  `m_activeServiceIdx`→`m_activeSegIdx`, `svcIdx`/`serviceIdx`→`segIdx`, plus the matching
  slots/helpers (`onAddSegment`, `loadSegmentContent`, `rebuildSegmentList`, …).
- Names meaning "the whole service" are unchanged (`saveServiceOrder`, `broadcastServiceOrder`).
- Service files are written with a `"segments"` key; every reader still accepts the old
  `"items"` key (autosave, export, both import paths).
- **The remote protocol was deliberately NOT renamed.** Its message keys (`serviceIdx`,
  `items`, …) are a contract with `data/remote.html`; they change only together with that
  file. (The rename script initially rewrote one such key, `msg["serviceIdx"]`; it was caught
  and restored.)

### Song library and segments  [PLANNED unless marked]
- **Adding a song to a segment copies it.** After that the segment is independent of the
  library. *Why:* a service must not change under the operator because the library was
  edited, and the library must not lose songs because a service was edited.
- **The segment remembers only the library song id (`sourceSongId`).** Its own id is a
  separate, fresh segment id. *Why:* today `Segment::id` is the library song id, which is
  why the same song cannot be added twice and why `m_sequenceSongIds`/`broadcastSequence`
  key on it. Separating them allows duplicates and removes the coupling.
- **Deleting a song from the library does NOT remove it from the service.** They are two
  different libraries (songs vs. what is projected). The segment simply keeps a
  `sourceSongId` that no longer resolves. (Today the code removes it — to be changed.)
- **Save to library** replaces that library song's content (title, artist, sections and
  their lyrics, tempo, key, copyright…) with the segment's. Everything **except background**.
  The library id is unchanged. If the id no longer exists in the library, it is re-created
  with the same stored id. **Save as new song** makes a new library id and updates the
  segment's `sourceSongId`. A confirmation is shown before overwriting.
  *Why:* the segment is where lyrics get corrected during real use; this is how the fix
  gets back to the library.
- **Cards carry a section tag** (label, type, section number, Chinese/pinyin flags).
  *Why:* a write-back must be able to rebuild the song's Verse/Chorus sections from a flat
  list of cards; the section number keeps two consecutive "Chorus" sections apart.
- **Write-back freezes the auto-split.** Cards are joined with a blank line, so splits that
  the global lines-per-slide setting used to make at display time become explicit.
  Slides look identical; a later change to lines-per-slide will not re-flow those songs.
- **The library has no backgrounds.** Backgrounds belong to segments/cards only.
  *Why:* a background is a decision about one service, not about the song. The song-level
  and section-level `backgroundPath` fields leave the library format.
  Consequence to handle: the song edit dialog is used for both the library and a segment's
  copy, so its background controls must show only when editing a segment.
- **Library file version.** `songs.json` becomes `{ "version": 2, "songs": [...] }` without
  background fields. Today it is a bare array with no version, so old/new cannot be told
  apart. The first time the user chooses "add lyrics", the library is checked once; if old,
  it is upgraded, the original is kept as `songs.v1.bak.json` (never overwritten, so the
  old backgrounds are not lost), and the new file is saved. After that the check passes
  immediately. *Why lazy and once:* the library is only needed when songs are added, and
  re-checking would add cost for nothing. Older plugin builds cannot read the new format;
  that is accepted (the library is not shared between PCs and users only move forward).

### Ids, queues and cards  [PLANNED]
- **Every segment and every slide card gets a service-wide uuid** (not a counter).
  *Why:* today a card is only a position (`slideIndex`, a global index, `(segIdx, fileIdx)`
  for media) and per-card data is keyed by that position (`slideBgs` is keyed by index).
  Inserting or reordering text shifts everything, and live-card tracking
  (`m_activePptSegIdx`) goes stale. Uuids cannot collide when services are imported.
- **The order is the structure.** `Service → segments[] → cards[]`, order = array order,
  in memory and in JSON. A *segment queue* (service level) and a *card queue* (per segment)
  are those same lists — not a second parallel structure. The lookup `id → (segment, card)`
  and the flat "next/previous across segments" order are derived and rebuilt on change.
  *Why:* two sources of truth (cards stored in one place, their order in another) is the
  classic way these drift apart. Nested arrays also mean the file has no id references to
  remap on import. Raw pointers into the lists must not be kept (they go stale on insert).
- **The current position is a card id**, so insert/remove/move elsewhere cannot move it.
- **Loading the app's own saved service keeps ids; a user-initiated import always mints new
  segment and card ids.** *Why:* the remote and saved "current card" rely on stable ids
  across restarts, while an imported file may contain ids that already exist.
- **Library → segment mints fresh ids** for the segment and every card each time, so the
  same song can be added twice. The library itself needs no card ids.
- **Editing a segment's text reconciles cards with a sequence diff** (not by position):
  unchanged blocks keep their ids, an edited block inside a gap keeps its id, only new
  blocks get new ids. A Chinese slide (text + pinyin + translation) is one unit.
  *Why:* matching by position would shift every id after an insertion and lose each card's
  background. The lines-per-slide setting becomes an explicit "re-split this segment" action.
- **Duplicate/copy of a card or segment mints new ids.** On load, "every id appears exactly
  once" is checked and duplicates are repaired.
- **Card kinds:** text (optional background picture/video), media (picture or video, told
  apart by file extension), capture (window + optional PowerPoint COM). "Playing/paused" is
  runtime state, not a kind. The four segment categories are unchanged.
- **Service-file migration:** a schema version on the service file; on load, an old file is
  copied to a backup, then migrated and saved immediately (old Lyrics segment id →
  `sourceSongId` + a new segment id; card ids minted).
- **Remote protocol:** moves from `(serviceIdx, slideIdx)` to ids, changed together with
  `data/remote.html` in the same release.

### Open decisions
- Where a segment's default background lives once the library has none (proposal: each card
  holds its own background, with a bulk "apply to all cards in this section" action).
- Order of work: (1) library version + upgrade + deletion no longer removing the segment,
  (2) ids/queues/service-file migration, (3) save-to-library buttons.

---

## v12.25.0 — Presentation items own their capture window and COM setting

### The problem this fixes
There is **one** OBS `window_capture` source (`SSO - PowerPoint`), **one** window
combo and **one** `PptTab`, shared by every Presentation service item. Before this
version the only per-item memory was a single string, and several code paths ignored
it. Symptom: with two Presentation items P1 and P2, changing the source on P1 made
P2 show P1's window (and vice versa).

Root causes found in the code:
1. Live-mode cards (`onLivePptActivate/Prev/Next`) called `requestShow(0)` with **no**
   window, and returned early if a presentation was already active — the capture was
   never retargeted when switching cards.
2. The "window changed" signal was saved to `m_activeServiceIdx` (the selected list
   row), but in live mode the item on air is `m_activePptSvcIdx` — the window got
   written onto the wrong item.
3. An item with no saved window kept whatever the previous item had selected
   (`requestShow` skipped the rescan; the combo's previous selection was restored).
4. COM was one global flag. After P1 (PowerPoint) connected, P2 (e.g. Google Slides)
   kept polling PowerPoint and reporting its slide counts; `gotoSlide` had no
   "is this a PowerPoint window" guard.
5. COM drove the *first* deck that had a slide show, not the deck being captured —
   with P1.pptx and P2.pptx both open, Next/Prev went to P1 while P2 was on screen.

### How presentations are handled now
**Model: one capture source, many items (1 → many).** Each Presentation service item
stores *its own* window; selecting an item loads that window into the shared capture.

**Single entry point.** Every path that shows a Presentation item goes through
`LyricsDock::bindPptItem(svcIdx)` → `PptTab::bindItem()`:
edit-mode list selection, `activateSlideFromRemote`, and the three live-card slots.
*Why:* previously each path did its own thing and two of them forgot the window; one
function means the rule cannot be bypassed.

`bindItem()` always: resets slide counter state → re-enumerates windows →
resolves the item's saved window → selects it → sets COM on/off → retargets the
capture source → shows.

**The item's saved window is the only source of truth.** The previous item's
selection is never inherited. *Why:* inheriting is exactly the bug.

**Saved per item** (`ServiceItem`, JSON keys in brackets):
| Field | JSON key | Meaning |
|---|---|---|
| `windowTitle` | `windowTitle` | exact window title when chosen |
| `windowProc` | `windowProc` | owning exe, e.g. `POWERPNT.EXE` |
| `windowCls` | `windowCls` | window class |
| `pptComMode` | `pptComMode` | -1 auto, 0 off, 1 on (omitted when -1) |

*Why three window fields:* a title alone is a weak key — PowerPoint titles change
between edit / slide show / presenter view, and Chrome tab titles (Google Slides,
Canva) change as you navigate. The **class** is what tells PowerPoint's slide show,
presenter view and editor apart even though all three are `POWERPNT.EXE`.

**Window matching (`PptTab::findWindowFor`)**, in order:
1. exact title (and same exe when known);
2. same exe + class, picking the best title similarity (shared prefix + suffix). With
   several look-alike windows a zero-similarity guess is rejected — wrong window is
   worse than none;
3. legacy items from before v12.23.0 that saved only an exe name.

**Window not open → blank, warn, retry.** If the saved window can't be found the
capture is cleared (`hideFromScene`), the status line shows "Window not found",
and a 2.5 s poll re-scans and attaches when the window appears (e.g. slide show
started later). *Why:* showing the previous item's window would be a silent wrong
answer on a live stream. A brand-new item with nothing saved gets one fresh
auto-detection (never an inherited one).

**Only user actions save.** The combo handler ignores programmatic selection
(`m_programmatic` counter) and emits `windowChosen` only when the user picks a window.
The dock stores it on the item identified by `m_pptBoundItemId` (a uuid), **not** on
the selected row. *Why:* fixes root cause 2, and a uuid survives list reordering.
`comModeChanged` works the same way for the COM button.

### COM (PowerPoint automation) is per item
- `bindItem` decides: COM wanted = window is `POWERPNT.EXE` **and** `pptComMode != 0`.
  Connects when wanted, disconnects otherwise (so a Google Slides item no longer
  inherits PowerPoint polling).
- If PowerPoint isn't reachable yet, `m_comWanted` stays set and the 5 s poll retries.
- Choosing a window by hand in auto mode connects/disconnects to match the window.
- The COM button is the per-item override: clicking it stores 1 (on) or 0 (off).
- Next/Prev/Goto and the slide counter only use COM when the captured window is a
  PowerPoint window; otherwise Next/Prev fall back to keystrokes.

**COM targets the captured deck** (`findPptForWindow`): pass 1 matches the slide show
window's `HWND` to the captured `HWND`; pass 2 matches the presentation file name
inside the captured window's title (edit view / presenter view). If a deck matches but
is not in slide show, the code does **not** drive some other deck — Next/Prev fall
back to keystrokes to the captured window. Legacy "first deck with a slide show" is
used only when no deck matches at all.

### Other behaviour changes
- Auto-scan every 5 s only runs when **no item is bound**; a bound item never gets
  auto-detected over.
- Scan button on a bound item re-resolves the item's window instead of auto-picking.
- Startup restore (`setBinding`) records the saved item's window without touching OBS;
  the first real show resolves it.
- `requestShow(type)` no longer takes a window argument; `requestShow(0)` re-shows the
  currently bound item (used by remote next/prev auto-activate).
- Old service files keep working: `windowExe` is read as the title (legacy exe-only
  values still match by exe). Only the new keys are written.
- Presentation items are still **not** exported/imported with the service JSON —
  window names are machine-specific.

### Known limitations
- `m_activePptSvcIdx` (live-card tracking) is still a list index; reordering or
  deleting items while a presentation card is live can leave it stale. Window/COM
  ownership already uses the item uuid and is not affected.
- Two windows with identical titles, class and exe are indistinguishable.
- COM deck matching by file name can false-match if one file name contains another
  (HWND match is tried first and is exact).
- Retargeting the capture can flash for a frame while OBS reattaches.

### Files
`src/ppt-tab.cpp/.hpp` (PptBinding, bindItem/applyBinding, window matching, per-item
COM, deck-aware COM), `src/lyrics-dock.cpp/.hpp` (ServiceItem fields + JSON,
`bindPptItem`, `serviceIndexById`, signal wiring, live-card slots, startup restore).

---

## Earlier entries (presentation-related, summarised)

### v12.23.0
- `ServiceItem::windowExe` started storing the full window **title** instead of the
  exe, and `autoSelectWindow` matched title-first. *Why:* PowerPoint's slide show,
  presenter view and editor all share `POWERPNT.EXE`, so exe matching always picked
  the first one and overrode the user's choice. (Superseded in v12.25.0 by
  title + exe + class and the bind model above.)
