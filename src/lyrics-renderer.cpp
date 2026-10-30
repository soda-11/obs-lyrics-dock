// lyrics-renderer.cpp  v10.0
//
// Render order (bottom → top):
//   1. Media layer  — image_source or ffmpeg_source (scaled to fit canvas)
//   2. Lyrics text  — text_gdiplus, full canvas
//   3. Section label — text_gdiplus, bottom-left
//   4. Song title   — text_gdiplus, bottom-right
//
// Crossfade: when switching media, old source fades out and new fades in
// over the requested duration using gs_texrender_t offscreen compositing.

#include "lyrics-renderer.hpp"
#include <obs-module.h>
#include <obs.h>
#include <media-io/audio-io.h>
#include <graphics/graphics.h>
#include <graphics/math-defs.h>
#include <util/platform.h>
#include <QMutex>
#include <QMutexLocker>
#include <QList>
#include <QString>
#include <QFileInfo>
#include <QSet>
#include <string>
#include <vector>
#include <algorithm>
#include <windows.h>

// ── Shared state ──────────────────────────────────────────────
static QMutex  s_mu;
static QString s_lyrics;
static QString s_section;
static QString s_title;
static QString s_mediaPath;
static QString s_audioPath;  // AudioBin dedicated audio slot

// MV mode: set when MV tab owns the media source.
static bool    s_mvMode      = false;
// Music-only MV mode: when true the media source carries audio only
// (e.g. mp3 with embedded album art).  We skip rendering the transition
// source so album-art frames never appear on the OBS canvas.
static bool    s_mvMusicOnly = false;

// Debug: split the 4 BG slots into separate 854×480 quadrants on the OBS canvas
// so their layering and fade states are directly visible.  Never affects the
// dock's own col-2 preview.  Toggle via lyrics_renderer_set_bg_debug().
static bool    s_bgDebugLayout = false;

// Chinese mode: set when a Chinese (trilingual) slide is active.
// In this mode srcLyrics carries Chinese characters, srcPinyin carries
// pinyin (top), and srcEnglish carries the English translation (bottom).
static bool    s_chineseMode = false;
static QString s_pinyin;    // cached pinyin for re-apply on settings change
static QString s_english;   // cached English translation

// Scripture/CTW mode: set when either tab is displaying content.
// s_paragraphMode=true means CTW is active (uses ctw_ prefixes instead of scr_).
static bool    s_scriptureMode = false;
static bool    s_paragraphMode       = false;
static QString s_scripTitle;
static QString s_scripText;

// QR overlay: path to the 80×80 PNG, visible flag
static bool    s_qrVisible = false;
static QString s_qrPath;

// ── Defaults ──────────────────────────────────────────────────
#define DEF_CANVAS_W      1280
#define DEF_CANVAS_H       720
#define DEF_BOTTOM_BAR_H    80
#define DEF_SCRIP_TITLE_H   70   // height of scripture title strip (px)
#define DEF_SCRIP_H_MARGIN 100   // left/right horizontal margin (px)
// Chinese interleave layout.
// Pinyin: ONE textbox at chPinyinY, height = canvasH - chPinyinY (no line limit).
//   Blank lines are inserted between content lines (chPinyinSpacing) so they
//   sit in the gaps between the Chinese character strips below.
// Chinese characters: up to MAX_CH_STRIPS individual strips.
//   Strip i is rendered at: chChineseY + i * chChineseInterval
//   Each strip has height chChineseStripH.
//   Setting chChineseInterval = 0 stacks all strips at the same Y.
// English translation: single textbox at chEnglishY.
#define DEF_CH_PINYIN_Y          0   // Y of pinyin textbox
#define DEF_CH_PINYIN_SPACING    2   // blank lines between pinyin content lines
#define DEF_CH_CHINESE_Y        34   // Y of Chinese char strip 0
#define DEF_CH_CHINESE_STRIP_H 100   // height of each Chinese char strip (px)
#define DEF_CH_CHINESE_INTERVAL 100  // Y increment between strips (px)
#define DEF_CH_ENGLISH_Y       280   // Y of English translation textbox
static constexpr int MAX_CH_STRIPS = 5;

// ── File-type helpers ─────────────────────────────────────────
static const QSet<QString> s_videoExts = {
    "mp4","mov","avi","mkv","webm","flv","wmv","m4v","ts","mts","m2ts"
};
static bool isVideoFile(const QString &p)
{
    return s_videoExts.contains(QFileInfo(p).suffix().toLower());
}

// ── Scripture text preprocessor ──────────────────────────────────────────────
// Rules:
//   • Digits (0-9) outside [...] are replaced with Unicode superscript equivalents.
//   • Text inside [...] is emitted as-is with the brackets stripped — digits
//     inside brackets are NOT superscripted (e.g. "[John 3:16]" → "John 3:16").
static QString applySuperscript(const QString &raw)
{
    // Unicode superscript digits ⁰¹²³⁴⁵⁶⁷⁸⁹
    static const QChar sup[10] = {
        QChar(0x2070), QChar(0x00B9), QChar(0x00B2), QChar(0x00B3), QChar(0x2074),
        QChar(0x2075), QChar(0x2076), QChar(0x2077), QChar(0x2078), QChar(0x2079)
    };
    QString out;
    out.reserve(raw.size());
    bool inBracket = false;
    for (const QChar c : raw) {
        if (c == QLatin1Char('[')) {
            inBracket = true;     // start of escape — drop the '['
        } else if (c == QLatin1Char(']')) {
            inBracket = false;    // end of escape — drop the ']'
        } else if (!inBracket && c.isDigit()) {
            out += sup[c.digitValue()];
        } else {
            out += c;
        }
    }
    return out;
}

// ── Background layer state machine ──────────────────────────
// 4 slots:  index 0/1 = image layers 1/2 (bottom),  2/3 = video layers 3/4 (top).
// Layer 4 (index 3) always fades in and always fades out.
// Layers 1-3 appear/disappear instantly; only layer 4 uses FADING_IN/FADING_OUT.
// Layer 3 (index 2) fades out only when going to image layers or clear.
enum class BgState { INACTIVE, WAITING, ACTIVE, FADING_IN, FADING_OUT };

struct BgSlot {
    obs_source_t *src          = nullptr;
    bool          isVideo      = false;
    bool          isMvSlot     = false;  // true = MV tab video; gets audio callback
    bool          looping      = true;
    QString       path;
    BgState       state        = BgState::INACTIVE;
    uint64_t      stateStartNs = 0;      // start time for WAITING timeout or fade alpha
    uint32_t      fadeDurMs    = 500;
    // Actions on WAITING complete:
    bool     waitGoFadeIn        = false; // false→ACTIVE instantly, true→FADING_IN
    int      triggerFadeOut      = -1;   // slot index to set FADING_OUT when WAITING→ACTIVE
    uint32_t triggerFadeOutDurMs = 500;
    // Action on FADING_IN complete:
    uint8_t  clearOnFadeIn       = 0;   // bitmask: slots to destroyBgSlot when FADING_IN→ACTIVE
};

// ── Per-instance ──────────────────────────────────────────────
struct LyricsInstance {
    obs_source_t *self          = nullptr;

    // Background layers: [0]=layer1 (image bottom) … [3]=layer4 (video top)
    BgSlot          bgSlots[4];
    gs_texrender_t *bgTexRender  = nullptr;  // shared texrender for alpha fades
    gs_effect_t    *bgFadeEffect = nullptr;  // lazily compiled opacity shader

    // Dedicated music slot (VLC or ffmpeg audio; independent of bgSlots)
    obs_source_t *musicSrc      = nullptr;
    bool          musicActive   = false;

    // Dedicated AudioBin slot — audio-only ffmpeg_source, independent of bgSlots/musicSrc
    obs_source_t *audioSrc      = nullptr;

    // Lyrics text children (English mode)
    obs_source_t *srcLyrics  = nullptr;
    obs_source_t *srcSection = nullptr;
    obs_source_t *srcTitle   = nullptr;

    // Chinese mode — single pinyin textbox + up to MAX_CH_STRIPS character strips.
    // srcPinyin: one textbox with blank-line spacing between content lines.
    // srcChineseStrips[i]: one strip per content line, rendered at
    //   chChineseY + i * chChineseInterval
    obs_source_t *srcPinyin = nullptr;
    obs_source_t *srcChineseStrips[MAX_CH_STRIPS] = {};
    obs_source_t *srcEnglish = nullptr;   // translation block (Chinese mode)

    // Scripture text children
    obs_source_t *srcScripTitle = nullptr;
    obs_source_t *srcScripText  = nullptr;

    // QR code overlay (image_source, 80×80 px, bottom-right)
    obs_source_t *srcQr = nullptr;

    // Canvas
    int canvasW      = DEF_CANVAS_W;
    int canvasH      = DEF_CANVAS_H;
    int bottomBarH   = DEF_BOTTOM_BAR_H;
    int scripTitleH  = DEF_SCRIP_TITLE_H;
    int scripHMargin = DEF_SCRIP_H_MARGIN;
    int paragraphTitleH    = DEF_SCRIP_TITLE_H;
    int paragraphHMargin   = DEF_SCRIP_H_MARGIN;

    // English lyrics vertical padding (px) — shrinks the lyrics textbox
    int lyrTopPadding  = 0;
    int lyrBotPadding  = 0;

    // Chinese mode layout
    int    chPinyinY         = DEF_CH_PINYIN_Y;
    int    chPinyinSpacing   = DEF_CH_PINYIN_SPACING;
    int    chChineseY        = DEF_CH_CHINESE_Y;
    int chChineseStripH   = DEF_CH_CHINESE_STRIP_H;
    int chChineseInterval = DEF_CH_CHINESE_INTERVAL;
    int chEnglishY        = DEF_CH_ENGLISH_Y;

    // Per-instance Chinese mode flag (set alongside the text children update).
    // Read by src_render() on the render thread without locking.
    bool chineseMode = false;
};

static QList<LyricsInstance*> s_instances;

// ── Helpers ───────────────────────────────────────────────────
static std::string pk(const char *prefix, const char *field)
{ return std::string(prefix) + field; }

static const char *hStr(int v)
{ return v==0?"left":v==2?"right":"center"; }
static const char *vStr(int v)
{ return v==0?"top":v==2?"bottom":"center"; }

// ── Text child push ───────────────────────────────────────────
static void pushTextChild(obs_source_t *child, obs_data_t *s,
                          const char *prefix,
                          const QString &text, int extW, int extH,
                          int lineSpacingPx = 0)
{
    if (!child) return;
    obs_data_t *gdi = obs_data_create();
    obs_data_set_string(gdi,"text",text.toUtf8().constData());

    obs_data_t *font = obs_data_create();
    const char *face = obs_data_get_string(s,pk(prefix,"font_face").c_str());
    obs_data_set_string(font,"face",(face&&face[0])?face:"Arial");
    int sz=(int)obs_data_get_int(s,pk(prefix,"font_size").c_str());
    obs_data_set_int(font,"size",sz>0?sz:48);
    int flags=0;
    if(obs_data_get_bool(s,pk(prefix,"bold").c_str()))   flags|=1;
    if(obs_data_get_bool(s,pk(prefix,"italic").c_str())) flags|=2;
    obs_data_set_int(font,"flags",flags);
    obs_data_set_obj(gdi,"font",font);
    obs_data_release(font);

    uint32_t col=(uint32_t)obs_data_get_int(s,pk(prefix,"color").c_str());
    obs_data_set_int(gdi,"color",(long long)(col&0x00FFFFFF));
    obs_data_set_int(gdi,"opacity",
        (int)obs_data_get_int(s,pk(prefix,"opacity").c_str()));

    bool outline=obs_data_get_bool(s,pk(prefix,"outline").c_str());
    obs_data_set_bool(gdi,"outline",outline);
    if(outline){
        obs_data_set_double(gdi,"outline_size",
            (double)obs_data_get_int(s,pk(prefix,"outline_size").c_str()));
        uint32_t oc=(uint32_t)obs_data_get_int(
            s,pk(prefix,"outline_color").c_str());
        obs_data_set_int(gdi,"outline_color",(long long)(oc&0x00FFFFFF));
        obs_data_set_int(gdi,"outline_opacity",100);
    }

    bool bg=obs_data_get_bool(s,pk(prefix,"bg").c_str());
    if(bg){
        uint32_t bc=(uint32_t)obs_data_get_int(
            s,pk(prefix,"bg_color").c_str());
        obs_data_set_int(gdi,"bk_color",(long long)(bc&0x00FFFFFF));
        obs_data_set_int(gdi,"bk_opacity",
            (int)obs_data_get_int(s,pk(prefix,"bg_opacity").c_str()));
    } else {
        obs_data_set_int(gdi,"bk_color",0LL);
        obs_data_set_int(gdi,"bk_opacity",0);
    }

    obs_data_set_string(gdi,"align",
        hStr((int)obs_data_get_int(s,pk(prefix,"align").c_str())));
    obs_data_set_string(gdi,"valign",
        vStr((int)obs_data_get_int(s,pk(prefix,"valign").c_str())));
    obs_data_set_bool(gdi,"word_wrap",   true);
    obs_data_set_bool(gdi,"extents",     true);
    obs_data_set_bool(gdi,"extents_wrap",true);
    obs_data_set_int (gdi,"extents_cx",  extW);
    obs_data_set_int (gdi,"extents_cy",  extH);
    if (lineSpacingPx > 0)
        obs_data_set_int(gdi,"line_spacing", lineSpacingPx);

    obs_source_update(child,gdi);
    obs_data_release(gdi);
}

static void updateTextChildren(LyricsInstance *inst, obs_data_t *s,
                               const QString &lyrics,
                               const QString &section,
                               const QString &title)
{
    int mainH=inst->canvasH-inst->bottomBarH;
    int halfW=inst->canvasW/2;
    int barH =inst->bottomBarH;
    int lyrH = qMax(1, mainH - inst->lyrTopPadding - inst->lyrBotPadding);
    pushTextChild(inst->srcLyrics, s,"lyrics_", lyrics, inst->canvasW, lyrH);
    pushTextChild(inst->srcSection,s,"section_",section,halfW,        barH);
    pushTextChild(inst->srcTitle,  s,"title_",  title,  halfW,        barH);
}

// isParagraph=true → use ctw_title_/ctw_text_ prefixes and CTW layout values.
// isParagraph=false (default) → use scr_title_/scr_text_ and Scripture layout values.
static void updateScriptureChildren(LyricsInstance *inst, obs_data_t *s,
                                    const QString &title, const QString &text,
                                    bool isParagraph = false)
{
    const char *titlePfx = isParagraph ? "paragraph_title_" : "scr_title_";
    const char *textPfx  = isParagraph ? "paragraph_text_"  : "scr_text_";
    int titleH  = isParagraph ? inst->paragraphTitleH  : inst->scripTitleH;
    int hMargin = isParagraph ? inst->paragraphHMargin : inst->scripHMargin;
    int textW   = qMax(64, inst->canvasW - 2 * hMargin);
    pushTextChild(inst->srcScripTitle, s, titlePfx, title, textW, titleH);
    pushTextChild(inst->srcScripText,  s, textPfx,  text,  textW,
                  inst->canvasH - titleH);
}

// ── Chinese slide child push ──────────────────────────────────────────────────
// Pinyin: single textbox.  Blank lines are inserted between content lines so
//   they visually align with the Chinese character strips below.
// Chinese chars: up to MAX_CH_STRIPS individual strips.
//   Strip i is rendered at chChineseY + i * chChineseInterval.
static void updateChineseChildren(LyricsInstance *inst, obs_data_t *s,
                                  const QString &pinyin,
                                  const QString &chinese,
                                  const QString &english)
{
    // Pinyin: insert blank lines between content lines for spacing
    auto addSpacing = [](const QString &text, int sp) -> QString {
        if (sp <= 0 || text.isEmpty()) return text;
        QStringList lines = text.split('\n');
        QStringList result;
        for (int i = 0; i < lines.size(); ++i) {
            result << lines[i];
            if (i < lines.size() - 1)
                for (int k = 0; k < sp; ++k) result << QString();
        }
        return result.join('\n');
    };
    int pinyinH = qMax(64, inst->canvasH - inst->chPinyinY);
    pushTextChild(inst->srcPinyin, s, "ch_pinyin_",
                  addSpacing(pinyin, inst->chPinyinSpacing),
                  inst->canvasW, pinyinH);

    // Chinese char strips — one per content line
    QStringList zhLines = chinese.isEmpty() ? QStringList() : chinese.split('\n');
    for (int i = 0; i < MAX_CH_STRIPS; ++i) {
        QString zhText = (i < zhLines.size()) ? zhLines[i] : QString();
        pushTextChild(inst->srcChineseStrips[i], s, "ch_chinese_",
                      zhText, inst->canvasW, inst->chChineseStripH);
    }

    // English translation
    int englishH = qMax(0, inst->canvasH - inst->bottomBarH - inst->chEnglishY);
    pushTextChild(inst->srcEnglish, s, "ch_english_", english,
                  inst->canvasW, englishH);
    inst->chineseMode = true;
}

// Clears all Chinese sources (call when switching to English / clearing).
static void clearChineseChildren(LyricsInstance *inst, obs_data_t *s)
{
    int pinyinH = qMax(64, inst->canvasH - inst->chPinyinY);
    pushTextChild(inst->srcPinyin, s, "ch_pinyin_",
                  QString(), inst->canvasW, pinyinH);
    for (int i = 0; i < MAX_CH_STRIPS; ++i)
        pushTextChild(inst->srcChineseStrips[i], s, "ch_chinese_",
                      QString(), inst->canvasW, inst->chChineseStripH);
    int englishH = qMax(0, inst->canvasH - inst->bottomBarH - inst->chEnglishY);
    pushTextChild(inst->srcEnglish, s, "ch_english_",
                  QString(), inst->canvasW, englishH);
    inst->chineseMode = false;
}

// ── Audio capture callback ────────────────────────────────────
static void mediaAudioCb(void *param, obs_source_t *,
                          const struct audio_data *audio, bool muted)
{
    auto *inst=static_cast<LyricsInstance*>(param);
    if(!inst||!inst->self||muted) return;
    const struct audio_output_info *aoi=
        audio_output_get_info(obs_get_audio());
    if(!aoi) return;
    struct obs_source_audio out={};
    for(int i=0;i<MAX_AV_PLANES;i++) out.data[i]=audio->data[i];
    out.frames          = audio->frames;
    out.format          = aoi->format;
    out.samples_per_sec = aoi->samples_per_sec;
    out.speakers        = aoi->speakers;
    out.timestamp       = audio->timestamp;
    obs_source_output_audio(inst->self,&out);
}

// ── Background slot management ────────────────────────────────

static void destroyBgSlot(LyricsInstance *inst, int i)
{
    auto &sl = inst->bgSlots[i];
    if (!sl.src) return;
    if (sl.isVideo)
        obs_source_remove_audio_capture_callback(sl.src, mediaAudioCb, inst);
    obs_source_dec_active(sl.src);
    obs_source_remove_active_child(inst->self, sl.src);
    obs_source_release(sl.src);
    sl = BgSlot{};
}

static void startBgFadeOut(LyricsInstance *inst, int i, uint32_t durMs)
{
    auto &sl = inst->bgSlots[i];
    if (sl.state == BgState::INACTIVE) return;
    sl.fadeDurMs    = durMs;
    sl.state        = BgState::FADING_OUT;
    sl.stateStartNs = os_gettime_ns();
}

// Creates a source into slot i. startWaiting=true → WAITING state (ready poll);
// false → ACTIVE immediately (instant appear, no wait).
static void loadBgSlot(LyricsInstance *inst, int i, const QString &path,
                       uint32_t fadeDurMs, bool looping, bool isMvSlot, bool startWaiting)
{
    destroyBgSlot(inst, i);
    auto &sl = inst->bgSlots[i];

    bool vid = isVideoFile(path);
    obs_data_t *d = obs_data_create();
    sl.src = obs_source_create_private(
        vid ? "ffmpeg_source" : "image_source", "lyrics_bg", d);
    obs_data_release(d);
    if (!sl.src) return;

    obs_source_add_active_child(inst->self, sl.src);
    obs_source_inc_active(sl.src);
    if (vid)
        obs_source_add_audio_capture_callback(sl.src, mediaAudioCb, inst);

    obs_data_t *cfg = obs_data_create();
    if (!vid) {
        obs_data_set_string(cfg, "file",   path.toUtf8().constData());
        obs_data_set_bool  (cfg, "unload", false);
    } else {
        obs_data_set_string(cfg, "local_file",           path.toUtf8().constData());
        obs_data_set_bool  (cfg, "is_local_file",        true);
        obs_data_set_bool  (cfg, "looping",              looping);
        obs_data_set_bool  (cfg, "hw_decode",            true);
        obs_data_set_bool  (cfg, "restart_on_activate",  false);
        obs_data_set_int   (cfg, "buffering_mb",         2);
    }
    obs_source_update(sl.src, cfg);
    obs_data_release(cfg);

    sl.isVideo      = vid;
    sl.isMvSlot     = isMvSlot;
    sl.looping      = looping;
    sl.path         = path;
    sl.fadeDurMs    = fadeDurMs;
    sl.state        = startWaiting ? BgState::WAITING : BgState::ACTIVE;
    sl.stateStartNs = os_gettime_ns();
    // Caller sets waitGoFadeIn, triggerFadeOut, clearOnFadeIn as needed
}

// Tears down the dedicated music child source (independent of bgSlots).
static void destroyMusicChild(LyricsInstance *inst)
{
    if (!inst->musicSrc) return;
    obs_source_remove_audio_capture_callback(inst->musicSrc, mediaAudioCb, inst);
    obs_source_dec_active(inst->musicSrc);
    obs_source_remove_active_child(inst->self, inst->musicSrc);
    obs_source_release(inst->musicSrc);
    inst->musicSrc    = nullptr;
    inst->musicActive = false;
}

// ── AudioBin dedicated audio slot ────────────────────────────
static void destroyAudioSlot(LyricsInstance *inst)
{
    if (!inst->audioSrc) return;
    // Blank the file path first so ffmpeg_source stops decoding immediately
    obs_data_t *blank = obs_data_create();
    obs_data_set_string(blank, "local_file", "");
    obs_data_set_bool  (blank, "is_local_file", true);
    obs_source_update(inst->audioSrc, blank);
    obs_data_release(blank);
    obs_source_media_stop(inst->audioSrc);
    obs_source_remove_audio_capture_callback(inst->audioSrc, mediaAudioCb, inst);
    obs_source_dec_active(inst->audioSrc);
    obs_source_remove_active_child(inst->self, inst->audioSrc);
    obs_source_release(inst->audioSrc);
    inst->audioSrc = nullptr;
}

static void loadAudioSlot(LyricsInstance *inst, const QString &path)
{
    destroyAudioSlot(inst);
    obs_data_t *d = obs_data_create();
    inst->audioSrc = obs_source_create_private("ffmpeg_source", "lyrics_audio_bin", d);
    obs_data_release(d);
    if (!inst->audioSrc) return;
    obs_source_add_active_child(inst->self, inst->audioSrc);
    obs_source_inc_active(inst->audioSrc);
    obs_source_add_audio_capture_callback(inst->audioSrc, mediaAudioCb, inst);
    obs_data_t *cfg = obs_data_create();
    obs_data_set_string(cfg, "local_file", path.toUtf8().constData());
    obs_data_set_bool(cfg, "is_local_file", true);
    obs_data_set_bool(cfg, "looping", false);
    obs_data_set_bool(cfg, "hw_decode", false);
    obs_data_set_bool(cfg, "restart_on_activate", false);
    obs_source_update(inst->audioSrc, cfg);
    obs_data_release(cfg);
}

// ── 4-layer background transition ─────────────────────────────
// Indices 0/1 = image layers 1/2 (pictures & presentations).
// Indices 2/3 = video layers 3/4.
// Layer 4 (index 3) always fades in and always fades out.
// Layer 3 (index 2) fades out only when switching to image layers or clearing.
// Layers 0-2 appear/disappear instantly when loaded/removed.
static constexpr uint32_t BG_READY_TIMEOUT_MS = 300;

static void applyBgPath(LyricsInstance *inst, const QString &path,
                        int fadeMs, bool looping = true, bool isMvSlot = false)
{
    const uint32_t fdMs = (fadeMs > 0) ? (uint32_t)fadeMs : 0u;

    // ── Empty path: clear all slots ──────────────────────────
    if (path.isEmpty()) {
        // Videos fade out (if fadeMs > 0); images disappear instantly.
        for (int i = 2; i <= 3; i++) {
            if (inst->bgSlots[i].state != BgState::INACTIVE) {
                if (fdMs > 0) startBgFadeOut(inst, i, fdMs);
                else          destroyBgSlot(inst, i);
            }
        }
        for (int i = 0; i <= 1; i++) destroyBgSlot(inst, i);
        return;
    }

    bool vid = isVideoFile(path);

    // ── Same path → do nothing ────────────────────────────────
    for (int i = 0; i < 4; i++) {
        auto &sl = inst->bgSlots[i];
        if (sl.path == path &&
            sl.state != BgState::INACTIVE && sl.state != BgState::FADING_OUT)
            return;
    }

    // ── Find current primary slots ────────────────────────────
    // ACTIVE, WAITING, or FADING_IN qualifies; FADING_OUT does not.
    int activeVid = -1;
    for (int i = 3; i >= 2; i--) {
        BgState st = inst->bgSlots[i].state;
        if (st == BgState::ACTIVE || st == BgState::WAITING || st == BgState::FADING_IN)
        { activeVid = i; break; }
    }
    int activeImg = -1;
    for (int i = 1; i >= 0; i--) {
        BgState st = inst->bgSlots[i].state;
        if (st == BgState::ACTIVE || st == BgState::WAITING || st == BgState::FADING_IN)
        { activeImg = i; break; }
    }

    if (vid) {
        // ── Video transitions ─────────────────────────────────
        if (activeVid >= 0) {
            if (activeVid == 3) {
                // Video at slot 3 (top) → new Video:
                // New video appears instantly at slot 2. Old fades out at slot 3, revealing new below.
                destroyBgSlot(inst, 2);
                loadBgSlot(inst, 2, path, fdMs, looping, isMvSlot, false); // ACTIVE — no fade in
                startBgFadeOut(inst, 3, fdMs);
            } else {
                // Video at slot 2 (below) → new Video:
                // New video fades in at slot 3 (on top of slot 2). Slot 2 unloads instantly when done.
                destroyBgSlot(inst, 3);
                loadBgSlot(inst, 3, path, fdMs, looping, isMvSlot, true); // WAITING → FADING_IN
                inst->bgSlots[3].waitGoFadeIn  = true;
                inst->bgSlots[3].clearOnFadeIn = (1u << 2); // unload slot 2 when fade-in completes
            }
        } else {
            // No video (or only pictures) → Video: new video FADES IN at slot 3 (top).
            // Any pictures at slots 0/1 show through during fade; cleared when done.
            destroyBgSlot(inst, 2);
            destroyBgSlot(inst, 3);
            loadBgSlot(inst, 3, path, fdMs, looping, isMvSlot, false);
            inst->bgSlots[3].state        = BgState::FADING_IN;
            inst->bgSlots[3].stateStartNs = os_gettime_ns();
            uint8_t mask = 0;
            for (int i = 0; i <= 1; i++)
                if (inst->bgSlots[i].state != BgState::INACTIVE)
                    mask |= (1u << i);
            inst->bgSlots[3].clearOnFadeIn = mask;
        }
    } else {
        // ── Picture / presentation transitions ────────────────
        if (activeVid >= 0) {
            // Rule 6: video active → picture → load at layer 2 (index 1).
            // Keep WAITING for images — we want the picture ready before fading video.
            destroyBgSlot(inst, 1);
            loadBgSlot(inst, 1, path, fdMs, false, isMvSlot, true);
            inst->bgSlots[1].waitGoFadeIn        = false;
            inst->bgSlots[1].triggerFadeOut      = activeVid;
            inst->bgSlots[1].triggerFadeOutDurMs = fdMs;
            // Clear any other non-primary video slot immediately
            for (int i = 2; i <= 3; i++)
                if (i != activeVid && inst->bgSlots[i].state != BgState::INACTIVE)
                    destroyBgSlot(inst, i);
        } else if (activeImg == 1) {
            // Rule 7: picture at layer 2 → new picture → load at layer 1; layer 2 gone.
            loadBgSlot(inst, 0, path, 0, false, isMvSlot, false);
            destroyBgSlot(inst, 1);
        } else if (activeImg == 0) {
            // Rule 8: picture at layer 1 → new picture → load at layer 2; layer 1 gone.
            loadBgSlot(inst, 1, path, 0, false, isMvSlot, false);
            destroyBgSlot(inst, 0);
        } else {
            // Rule 2: nothing → picture → instant appear at layer 2.
            loadBgSlot(inst, 1, path, 0, false, isMvSlot, false);
        }
    }
}

static const char *src_name(void*){ return "Lyrics"; }

static void *src_create(obs_data_t *settings, obs_source_t *source)
{
    auto *inst  = new LyricsInstance;
    inst->self  = source;
    inst->canvasW     =(int)obs_data_get_int(settings,"canvas_w");
    inst->canvasH     =(int)obs_data_get_int(settings,"canvas_h");
    inst->bottomBarH  =(int)obs_data_get_int(settings,"bottom_bar_h");
    inst->scripTitleH =(int)obs_data_get_int(settings,"scrip_title_h");
    inst->scripHMargin=(int)obs_data_get_int(settings,"scrip_h_margin");
    if(!inst->canvasW)     inst->canvasW    =DEF_CANVAS_W;
    if(!inst->canvasH)     inst->canvasH    =DEF_CANVAS_H;
    if(!inst->bottomBarH)  inst->bottomBarH =DEF_BOTTOM_BAR_H;
    if(!inst->scripTitleH) inst->scripTitleH=DEF_SCRIP_TITLE_H;
    // Y and height values may legitimately be 0 — use has_user_value
    auto readOrDef = [&](const char *key, int def) -> int {
        return obs_data_has_user_value(settings,key)
               ? (int)obs_data_get_int(settings,key) : def;
    };
    inst->lyrTopPadding = readOrDef("lyrics_top_padding",    0);
    inst->lyrBotPadding = readOrDef("lyrics_bottom_padding", 0);
    inst->chPinyinY     = readOrDef("ch_pinyin_y",          DEF_CH_PINYIN_Y);
    inst->chPinyinSpacing   = readOrDef("ch_pinyin_spacing",    DEF_CH_PINYIN_SPACING);
    inst->chChineseY        = readOrDef("ch_chinese_y",         DEF_CH_CHINESE_Y);
    inst->chChineseStripH   = readOrDef("ch_chinese_strip_h",   DEF_CH_CHINESE_STRIP_H);
    inst->chChineseInterval = readOrDef("ch_chinese_interval",  DEF_CH_CHINESE_INTERVAL);
    inst->chEnglishY        = readOrDef("ch_english_y",         DEF_CH_ENGLISH_Y);
    if (!obs_data_has_user_value(settings, "scrip_h_margin"))
        inst->scripHMargin = DEF_SCRIP_H_MARGIN;
    inst->paragraphTitleH  = obs_data_has_user_value(settings,"paragraph_title_h")
                       ? (int)obs_data_get_int(settings,"paragraph_title_h") : DEF_SCRIP_TITLE_H;
    inst->paragraphHMargin = obs_data_has_user_value(settings,"paragraph_h_margin")
                       ? (int)obs_data_get_int(settings,"paragraph_h_margin") : DEF_SCRIP_H_MARGIN;

    auto makeGdi=[](const char *name)->obs_source_t*{
        obs_data_t *d=obs_data_create();
        obs_data_set_string(d,"text","");
        obs_data_set_bool(d,"word_wrap",true);
        auto *s=obs_source_create_private("text_gdiplus",name,d);
        obs_data_release(d);
        return s;
    };
    inst->srcLyrics     = makeGdi("lyrics_main");
    inst->srcSection    = makeGdi("lyrics_section");
    inst->srcTitle      = makeGdi("lyrics_title");
    inst->srcPinyin     = makeGdi("ch_pinyin");
    inst->srcEnglish    = makeGdi("ch_english");
    inst->srcScripTitle = makeGdi("scripture_title");
    inst->srcScripText  = makeGdi("scripture_text");
    for (int i = 0; i < MAX_CH_STRIPS; ++i)
        inst->srcChineseStrips[i] = makeGdi(
            ("ch_zh_" + std::to_string(i)).c_str());

    // QR overlay image source (empty path = hidden)
    {
        obs_data_t *qd = obs_data_create();
        obs_data_set_string(qd, "file", "");
        inst->srcQr = obs_source_create_private("image_source", "lyrics_qr_overlay", qd);
        obs_data_release(qd);
    }

    { QMutexLocker lk(&s_mu); s_instances<<inst; }
    { QMutexLocker lk(&s_mu);
      QString med=s_mediaPath, ly=s_lyrics,
              sec=s_section, ttl=s_title;
      QString stit=s_scripTitle, stxt=s_scripText;
      lk.unlock();
      applyBgPath(inst,med,0); // no fade on first load
      updateTextChildren(inst,settings,ly,sec,ttl);
      updateScriptureChildren(inst,settings,
          stit, applySuperscript(stxt)); }
    return inst;
}

static void src_destroy(void *data)
{
    auto *inst=static_cast<LyricsInstance*>(data);
    { QMutexLocker lk(&s_mu); s_instances.removeAll(inst); }
    destroyMusicChild(inst);
    destroyAudioSlot(inst);
    for (int i = 0; i < 4; i++) destroyBgSlot(inst, i);
    obs_enter_graphics();
    if (inst->bgTexRender) {
        gs_texrender_destroy(inst->bgTexRender);
        inst->bgTexRender = nullptr;
    }
    if (inst->bgFadeEffect) {
        gs_effect_destroy(inst->bgFadeEffect);
        inst->bgFadeEffect = nullptr;
    }
    obs_leave_graphics();
    obs_source_release(inst->srcLyrics);
    obs_source_release(inst->srcSection);
    obs_source_release(inst->srcTitle);
    obs_source_release(inst->srcPinyin);
    obs_source_release(inst->srcEnglish);
    obs_source_release(inst->srcScripTitle);
    obs_source_release(inst->srcScripText);
    for (int i = 0; i < MAX_CH_STRIPS; ++i)
        obs_source_release(inst->srcChineseStrips[i]);
    obs_source_release(inst->srcQr);
    delete inst;
}

static void src_defaults(obs_data_t *s)
{
    obs_data_set_default_int(s,"canvas_w",      DEF_CANVAS_W);
    obs_data_set_default_int(s,"canvas_h",      DEF_CANVAS_H);
    obs_data_set_default_int(s,"bottom_bar_h",  DEF_BOTTOM_BAR_H);
    obs_data_set_default_int(s,"scrip_title_h",  DEF_SCRIP_TITLE_H);
    obs_data_set_default_int(s,"scrip_h_margin", DEF_SCRIP_H_MARGIN);
    obs_data_set_default_int(s,"paragraph_title_h",    DEF_SCRIP_TITLE_H);
    obs_data_set_default_int(s,"paragraph_h_margin",   DEF_SCRIP_H_MARGIN);
    obs_data_set_default_int   (s,"lyrics_top_padding",    0);
    obs_data_set_default_int   (s,"lyrics_bottom_padding", 0);
    obs_data_set_default_int   (s,"ch_pinyin_y",           DEF_CH_PINYIN_Y);
    obs_data_set_default_int   (s,"ch_pinyin_spacing",     DEF_CH_PINYIN_SPACING);
    obs_data_set_default_int(s,"ch_chinese_y",        DEF_CH_CHINESE_Y);
    obs_data_set_default_int(s,"ch_chinese_strip_h",  DEF_CH_CHINESE_STRIP_H);
    obs_data_set_default_int(s,"ch_chinese_interval", DEF_CH_CHINESE_INTERVAL);
    obs_data_set_default_int(s,"ch_english_y",        DEF_CH_ENGLISH_Y);

    uint32_t white=0x00FFFFFFu,black=0x00000000u;
    uint32_t lgrey=0x00CCCCCCu,mgrey=0x00AAAAAAu;
    uint32_t ygold=0x0077FFFFu; // golden-yellow (#ffff77) in 0xBBGGRR

    auto setBox=[&](const char *p,int fs,int ha,int va,uint32_t col){
        obs_data_set_default_string(s,pk(p,"font_face").c_str(),"Arial");
        obs_data_set_default_int   (s,pk(p,"font_size").c_str(),fs);
        obs_data_set_default_bool  (s,pk(p,"bold").c_str(),true);
        obs_data_set_default_bool  (s,pk(p,"italic").c_str(),false);
        obs_data_set_default_int   (s,pk(p,"color").c_str(),(long long)col);
        obs_data_set_default_int   (s,pk(p,"opacity").c_str(),100);
        obs_data_set_default_bool  (s,pk(p,"outline").c_str(),true);
        obs_data_set_default_int   (s,pk(p,"outline_size").c_str(),3);
        obs_data_set_default_int   (s,pk(p,"outline_color").c_str(),(long long)black);
        obs_data_set_default_bool  (s,pk(p,"bg").c_str(),false);
        obs_data_set_default_int   (s,pk(p,"bg_color").c_str(),(long long)black);
        obs_data_set_default_int   (s,pk(p,"bg_opacity").c_str(),50);
        obs_data_set_default_int   (s,pk(p,"align").c_str(),ha);
        obs_data_set_default_int   (s,pk(p,"valign").c_str(),va);
    };
    // English lyrics boxes
    setBox("lyrics_", 48,1,1,white);
    setBox("section_",22,0,1,lgrey);
    setBox("title_",  22,2,1,mgrey);
    // Scripture boxes: title centred+top, text centred+middle
    setBox("scr_title_",32,1,0,ygold);
    setBox("scr_text_", 48,1,1,white);
    // CTW boxes: independent defaults matching Scripture
    setBox("paragraph_title_",32,1,0,ygold);
    setBox("paragraph_text_", 48,1,1,white);
    // Chinese trilingual boxes (pinyin top, Chinese chars middle, English bottom)
    setBox("ch_pinyin_",  24,1,1,ygold);   // pinyin: smaller, gold-yellow
    setBox("ch_chinese_", 52,1,1,white);   // Chinese: large, white
    setBox("ch_english_", 28,1,1,lgrey);   // English: medium, light grey
    (void)ygold; // suppress unused warning if needed
}

// ── Font enumeration ──────────────────────────────────────────
static std::vector<std::string> s_systemFonts;
static bool s_fontsEnumerated=false;

static int CALLBACK enumFontProc(const LOGFONTA *lf,
                                  const TEXTMETRICA*,
                                  DWORD,LPARAM lParam)
{
    auto *v=reinterpret_cast<std::vector<std::string>*>(lParam);
    if(lf->lfFaceName[0]!='@') v->push_back(lf->lfFaceName);
    return 1;
}
static void ensureFontsEnumerated()
{
    if(s_fontsEnumerated) return;
    s_fontsEnumerated=true;
    HDC hdc=GetDC(nullptr); if(!hdc) return;
    LOGFONTA lf={}; lf.lfCharSet=DEFAULT_CHARSET;
    EnumFontFamiliesExA(hdc,&lf,enumFontProc,
        reinterpret_cast<LPARAM>(&s_systemFonts),0);
    ReleaseDC(nullptr,hdc);
    std::sort(s_systemFonts.begin(),s_systemFonts.end(),
        [](const std::string&a,const std::string&b){
            return _stricmp(a.c_str(),b.c_str())<0;});
    s_systemFonts.erase(
        std::unique(s_systemFonts.begin(),s_systemFonts.end(),
            [](const std::string&a,const std::string&b){
                return _stricmp(a.c_str(),b.c_str())==0;}),
        s_systemFonts.end());
}

static void addBoxGroup(obs_properties_t *props,
                        const char *prefix,const char *label)
{
    obs_properties_t *grp=obs_properties_create();
    ensureFontsEnumerated();
    obs_property_t *ff=obs_properties_add_list(grp,
        pk(prefix,"font_face").c_str(),"Font",
        OBS_COMBO_TYPE_EDITABLE,OBS_COMBO_FORMAT_STRING);
    for(const auto &n:s_systemFonts)
        obs_property_list_add_string(ff,n.c_str(),n.c_str());
    obs_properties_add_int(grp,
        pk(prefix,"font_size").c_str(),"Size (pt)",6,300,1);
    obs_properties_add_bool(grp,pk(prefix,"bold").c_str(),  "Bold");
    obs_properties_add_bool(grp,pk(prefix,"italic").c_str(),"Italic");
    obs_properties_add_color(grp,pk(prefix,"color").c_str(),"Text Colour");
    obs_properties_add_int(grp,
        pk(prefix,"opacity").c_str(),"Text Opacity (%)",0,100,1);
    obs_properties_add_bool(grp,pk(prefix,"outline").c_str(),"Outline");
    obs_properties_add_int(grp,
        pk(prefix,"outline_size").c_str(),"Outline Width (px)",1,20,1);
    obs_properties_add_color(grp,
        pk(prefix,"outline_color").c_str(),"Outline Colour");
    obs_properties_add_bool(grp,pk(prefix,"bg").c_str(),"Background");
    obs_properties_add_color(grp,
        pk(prefix,"bg_color").c_str(),"Background Colour");
    obs_properties_add_int(grp,
        pk(prefix,"bg_opacity").c_str(),"Background Opacity (%)",0,100,1);
    obs_property_t *ha=obs_properties_add_list(grp,
        pk(prefix,"align").c_str(),"Horizontal Align",
        OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(ha,"Left",  0);
    obs_property_list_add_int(ha,"Center",1);
    obs_property_list_add_int(ha,"Right", 2);
    obs_property_t *va=obs_properties_add_list(grp,
        pk(prefix,"valign").c_str(),"Vertical Align",
        OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(va,"Top",   0);
    obs_property_list_add_int(va,"Center",1);
    obs_property_list_add_int(va,"Bottom",2);
    obs_properties_add_group(props,
        pk(prefix,"grp").c_str(),label,OBS_GROUP_NORMAL,grp);
}

static obs_properties_t *src_props(void *)
{
    obs_properties_t *props=obs_properties_create();
    obs_properties_t *cgrp=obs_properties_create();
    obs_properties_add_int(cgrp,"canvas_w",      "Width (px)",                       320,7680,1);
    obs_properties_add_int(cgrp,"canvas_h",      "Height (px)",                      180,4320,1);
    obs_properties_add_int(cgrp,"bottom_bar_h",         "Lyrics: bottom bar height (px)",      10, 400,1);
    obs_properties_add_int(cgrp,"lyrics_top_padding",    "Lyrics: top padding (px)",            0, 1080,1);
    obs_properties_add_int(cgrp,"lyrics_bottom_padding", "Lyrics: bottom padding (px)",         0, 1080,1);
    obs_properties_add_int(cgrp,"paragraph_title_h",  "Paragraph: title strip height (px)",  10, 400,1);
    obs_properties_add_int(cgrp,"paragraph_h_margin", "Paragraph: left/right margin (px)",   0, 400,1);

    // ── Chinese interleave layout sub-group ──────────────────────────────────
    obs_properties_t *chgrp = obs_properties_create();
    obs_properties_add_int(chgrp,"ch_pinyin_y",
        "Pinyin starting Y position (px)", 0, 4320, 1);
    obs_properties_add_int(chgrp,"ch_pinyin_spacing",
        "Pinyin line spacing (blank lines between lines)", 0, 10, 1);
    obs_properties_add_int(chgrp,"ch_chinese_y",
        "Characters starting Y position (px)", 0, 4320, 1);
    obs_properties_add_int(chgrp,"ch_chinese_strip_h",
        "Characters strip height (px)", 10, 400, 1);
    obs_properties_add_int(chgrp,"ch_chinese_interval",
        "Characters strip interval — Y step between strips (px)", 0, 400, 1);
    obs_properties_add_int(chgrp,"ch_english_y",
        "English translation starting Y position (px)", 0, 4320, 1);
    obs_properties_add_group(cgrp,"ch_layout_grp",
        "Chinese Interleave Layout", OBS_GROUP_NORMAL, chgrp);
    obs_properties_add_text(cgrp, "_info",
        "Render order (bottom to top):\n"
        "  1. Media layer  (background images/videos)\n"
        "  English lyrics mode:\n"
        "    2. Lyrics text  (full canvas minus bottom bar)\n"
        "    3. Section label (bottom-left)\n"
        "    4. Song title   (bottom-right)\n"
        "  Chinese trilingual mode:\n"
        "    2. Pinyin (top strip)\n"
        "    3. Chinese characters (middle strip)\n"
        "    4. English translation (lower strip)\n"
        "    5. Section label (bottom-left)\n"
        "    6. Song title   (bottom-right)\n"
        "  Paragraph mode:\n"
        "    2. Paragraph title (top strip)\n"
        "    3. Paragraph text  (main area below title)",
        OBS_TEXT_INFO);
    obs_properties_add_group(props,"canvas_grp",
        "Canvas & Layout",OBS_GROUP_NORMAL,cgrp);
    addBoxGroup(props,"lyrics_", "1.  English Lyrics — Main Text");
    addBoxGroup(props,"section_","2.  Lyrics — Section Label  (bottom-left)");
    addBoxGroup(props,"title_",  "3.  Lyrics — Song Title  (bottom-right)");
    addBoxGroup(props,"paragraph_title_","4.  Paragraph — Title  (top strip)");
    addBoxGroup(props,"paragraph_text_", "5.  Paragraph — Text  (main area)");
    addBoxGroup(props,"ch_pinyin_", "6.  Chinese — Pinyin  (top strip)");
    addBoxGroup(props,"ch_chinese_","7.  Chinese — Characters  (middle)");
    addBoxGroup(props,"ch_english_","8.  Chinese — English Translation  (bottom)");
    return props;
}

static void src_update(void *data, obs_data_t *settings)
{
    auto *inst=static_cast<LyricsInstance*>(data);
    inst->canvasW     =(int)obs_data_get_int(settings,"canvas_w");
    inst->canvasH     =(int)obs_data_get_int(settings,"canvas_h");
    inst->bottomBarH  =(int)obs_data_get_int(settings,"bottom_bar_h");
    inst->scripTitleH  =(int)obs_data_get_int(settings,"scrip_title_h");
    inst->scripHMargin =(int)obs_data_get_int(settings,"scrip_h_margin");
    if(!inst->canvasW)     inst->canvasW    =DEF_CANVAS_W;
    if(!inst->canvasH)     inst->canvasH    =DEF_CANVAS_H;
    if(!inst->bottomBarH)  inst->bottomBarH =DEF_BOTTOM_BAR_H;
    if(!inst->scripTitleH) inst->scripTitleH=DEF_SCRIP_TITLE_H;
    auto readOrDef2 = [&](const char *key, int def) -> int {
        return obs_data_has_user_value(settings,key)
               ? (int)obs_data_get_int(settings,key) : def;
    };
    inst->lyrTopPadding = readOrDef2("lyrics_top_padding",    0);
    inst->lyrBotPadding = readOrDef2("lyrics_bottom_padding", 0);
    inst->chPinyinY     = readOrDef2("ch_pinyin_y",          DEF_CH_PINYIN_Y);
    inst->chPinyinSpacing   = readOrDef2("ch_pinyin_spacing",   DEF_CH_PINYIN_SPACING);
    inst->chChineseY        = readOrDef2("ch_chinese_y",        DEF_CH_CHINESE_Y);
    inst->chChineseStripH   = readOrDef2("ch_chinese_strip_h",  DEF_CH_CHINESE_STRIP_H);
    inst->chChineseInterval = readOrDef2("ch_chinese_interval", DEF_CH_CHINESE_INTERVAL);
    inst->chEnglishY        = readOrDef2("ch_english_y",        DEF_CH_ENGLISH_Y);
    if(!obs_data_has_user_value(settings,"scrip_h_margin"))
        inst->scripHMargin = DEF_SCRIP_H_MARGIN;
    inst->paragraphTitleH  = obs_data_has_user_value(settings,"paragraph_title_h")
                       ? (int)obs_data_get_int(settings,"paragraph_title_h") : DEF_SCRIP_TITLE_H;
    inst->paragraphHMargin = obs_data_has_user_value(settings,"paragraph_h_margin")
                       ? (int)obs_data_get_int(settings,"paragraph_h_margin") : DEF_SCRIP_H_MARGIN;
    QMutexLocker lk(&s_mu);
    // Re-apply whichever mode is currently active so text re-layouts
    // correctly after a canvas size / font change.
    if (s_scriptureMode) {
        clearChineseChildren(inst, settings);
        updateTextChildren(inst,settings,QString(),QString(),QString());
        updateScriptureChildren(inst,settings,
            s_scripTitle, applySuperscript(s_scripText), s_paragraphMode);
    } else if (s_chineseMode) {
        updateScriptureChildren(inst, settings, QString(), QString());
        updateChineseChildren(inst, settings, s_pinyin, s_lyrics, s_english);
        int halfW = inst->canvasW / 2, barH = inst->bottomBarH;
        pushTextChild(inst->srcSection, settings, "section_", s_section, halfW, barH);
        pushTextChild(inst->srcTitle,   settings, "title_",   s_title,   halfW, barH);
    } else {
        updateScriptureChildren(inst,settings,QString(),QString());
        clearChineseChildren(inst, settings);
        updateTextChildren(inst,settings,s_lyrics,s_section,s_title);
    }
}

static uint32_t src_width (void *d)
{ return (uint32_t)static_cast<LyricsInstance*>(d)->canvasW; }
static uint32_t src_height(void *d)
{ return (uint32_t)static_cast<LyricsInstance*>(d)->canvasH; }

// ── Background render helpers ──────────────────────────────────

// Scale-to-cover render of a source onto the current GS target.
static void renderBgSourceFull(obs_source_t *src, int cW, int cH)
{
    uint32_t mw = obs_source_get_width(src);
    uint32_t mh = obs_source_get_height(src);
    if (mw > 0 && mh > 0 && ((int)mw != cW || (int)mh != cH)) {
        float sx = (float)cW/(float)mw, sy = (float)cH/(float)mh;
        float sc = sx > sy ? sx : sy;
        float ox = ((float)cW - (float)mw*sc)*0.5f;
        float oy = ((float)cH - (float)mh*sc)*0.5f;
        gs_matrix_push();
        gs_matrix_translate3f(ox, oy, 0.f);
        gs_matrix_scale3f(sc, sc, 1.f);
        obs_source_video_render(src);
        gs_matrix_pop();
    } else {
        obs_source_video_render(src);
    }
}

// Render slot at given alpha [0..1) via texrender + opacity shader.
static void renderBgSlotAlpha(LyricsInstance *inst, int slotIdx, int cW, int cH, float alpha)
{
    obs_source_t *src = inst->bgSlots[slotIdx].src;
    if (!src || alpha <= 0.f) return;
    if (alpha >= 1.f) { renderBgSourceFull(src, cW, cH); return; }

    if (!inst->bgTexRender)
        inst->bgTexRender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_texrender_reset(inst->bgTexRender);
    if (!gs_texrender_begin(inst->bgTexRender, (uint32_t)cW, (uint32_t)cH)) return;
    vec4 zero; vec4_zero(&zero);
    gs_clear(GS_CLEAR_COLOR, &zero, 1.f, 0);
    renderBgSourceFull(src, cW, cH);
    gs_texrender_end(inst->bgTexRender);

    gs_texture_t *tex = gs_texrender_get_texture(inst->bgTexRender);
    if (!tex) return;

    if (!inst->bgFadeEffect) {
        static const char *SRC =
            "uniform float4x4 ViewProj;\n"
            "uniform texture2d image;\n"
            "uniform float opacity;\n"
            "sampler_state smp{Filter=Linear;AddressU=Clamp;AddressV=Clamp;};\n"
            "struct VD{float4 pos:POSITION;float2 uv:TEXCOORD0;};\n"
            "VD VS(VD v){VD o;o.pos=mul(float4(v.pos.xyz,1.0),ViewProj);o.uv=v.uv;return o;}\n"
            "float4 PS(VD v):TARGET{float4 c=image.Sample(smp,v.uv);c.rgba*=opacity;return c;}\n"
            "technique Draw{pass{vertex_shader=VS(v);pixel_shader=PS(v);}}\n";
        char *err = nullptr;
        inst->bgFadeEffect = gs_effect_create(SRC, nullptr, &err);
        if (err) bfree(err);
    }
    if (!inst->bgFadeEffect) return;

    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    gs_effect_set_texture(gs_effect_get_param_by_name(inst->bgFadeEffect, "image"), tex);
    gs_effect_set_float  (gs_effect_get_param_by_name(inst->bgFadeEffect, "opacity"), alpha);
    while (gs_effect_loop(inst->bgFadeEffect, "Draw"))
        gs_draw_sprite(tex, 0, (uint32_t)cW, (uint32_t)cH);
    gs_blend_state_pop();
}

static void src_render(void *data, gs_effect_t *effect)
{
    auto *inst=static_cast<LyricsInstance*>(data);
    int cW=inst->canvasW, cH=inst->canvasH;
    int mainH=cH-inst->bottomBarH;
    int halfW=cW/2;

    // ── 1. Background layers 0→3 (bottom to top) ─────────────
    // Skip when s_mvMusicOnly (music-only MV: no video to render).
    if (!s_mvMusicOnly) {
        for (int i = 0; i < 4; i++) {
            auto &sl = inst->bgSlots[i];
            if (sl.state == BgState::INACTIVE || !sl.src) continue;

            // Debug layout: render each slot in its own 854×480 quadrant.
            // RAII so gs_matrix_pop() fires automatically on any continue/exit.
            struct DbgScope {
                bool on;
                DbgScope(bool enable, int slot, int cW, int cH) : on(enable) {
                    if (!on) return;
                    static constexpr float kAX[4] = {  0.f,   0.f, 360.f, 360.f };
                    static constexpr float kAY[4] = {  0.f, 640.f,   0.f, 640.f };
                    gs_matrix_push();
                    gs_matrix_translate3f(kAX[slot], kAY[slot], 0.f);
                    gs_matrix_scale3f(854.f / (float)cW, 480.f / (float)cH, 1.f);
                }
                ~DbgScope() { if (on) gs_matrix_pop(); }
            } dbgScope(s_bgDebugLayout, i, cW, cH);

            if (sl.state == BgState::WAITING) {
                uint64_t elMs = (os_gettime_ns() - sl.stateStartNs) / 1000000ULL;
                bool ready = elMs >= BG_READY_TIMEOUT_MS ||
                             obs_source_get_width(sl.src) > 0;
                if (!ready) continue; // still loading; keep old content visible
                if (sl.waitGoFadeIn) {
                    sl.state        = BgState::FADING_IN;
                    sl.stateStartNs = os_gettime_ns();
                    continue; // alpha=0 this first frame; skip render
                } else {
                    // Instant ACTIVE — trigger fade-out of another slot if requested
                    int j           = sl.triggerFadeOut;
                    uint32_t jDur   = sl.triggerFadeOutDurMs;
                    sl.state        = BgState::ACTIVE;
                    sl.stateStartNs = 0;
                    sl.triggerFadeOut = -1;
                    if (j >= 0 && j < 4 && inst->bgSlots[j].state != BgState::INACTIVE)
                        startBgFadeOut(inst, j, jDur);
                    renderBgSourceFull(sl.src, cW, cH);
                    continue;
                }
            }

            if (sl.state == BgState::ACTIVE) {
                renderBgSourceFull(sl.src, cW, cH);

            } else if (sl.state == BgState::FADING_IN) {
                uint64_t elMs = (os_gettime_ns() - sl.stateStartNs) / 1000000ULL;
                float alpha = sl.fadeDurMs > 0
                    ? (float)elMs / (float)sl.fadeDurMs : 1.f;
                if (alpha >= 1.f) {
                    uint8_t mask    = sl.clearOnFadeIn;
                    sl.state        = BgState::ACTIVE;
                    sl.clearOnFadeIn = 0;
                    for (int j = 0; j < 4; j++)
                        if ((mask >> j) & 1u) destroyBgSlot(inst, j);
                    renderBgSourceFull(sl.src, cW, cH);
                } else {
                    renderBgSlotAlpha(inst, i, cW, cH, alpha);
                }

            } else if (sl.state == BgState::FADING_OUT) {
                uint64_t elMs = (os_gettime_ns() - sl.stateStartNs) / 1000000ULL;
                float alpha = sl.fadeDurMs > 0
                    ? 1.f - (float)elMs / (float)sl.fadeDurMs : 0.f;
                if (alpha <= 0.f)
                    destroyBgSlot(inst, i);
                else
                    renderBgSlotAlpha(inst, i, cW, cH, alpha);
            }
        }
    }

    // ── 2. Scripture title — top strip, inset by scripHMargin ──
    // Empty when in lyrics mode; non-empty only when Scripture tab is active.
    gs_matrix_push();
    gs_matrix_translate3f((float)inst->scripHMargin, 0.f, 0.f);
    obs_source_video_render(inst->srcScripTitle);
    gs_matrix_pop();

    // ── 3. Scripture text — main area below title, same x inset ─
    gs_matrix_push();
    gs_matrix_translate3f((float)inst->scripHMargin, (float)inst->scripTitleH, 0.f);
    obs_source_video_render(inst->srcScripText);
    gs_matrix_pop();

    if (inst->chineseMode) {
        // ── Chinese trilingual layout ──
        // Pinyin: single textbox with blank-line spacing
        gs_matrix_push();
        gs_matrix_translate3f(0.f, (float)inst->chPinyinY, 0.f);
        obs_source_video_render(inst->srcPinyin);
        gs_matrix_pop();

        // Chinese char strips: strip i at chChineseY + i * chChineseInterval
        for (int i = 0; i < MAX_CH_STRIPS; ++i) {
            float zhY = (float)(inst->chChineseY + i * inst->chChineseInterval);
            gs_matrix_push();
            gs_matrix_translate3f(0.f, zhY, 0.f);
            obs_source_video_render(inst->srcChineseStrips[i]);
            gs_matrix_pop();
        }

        // English translation
        gs_matrix_push();
        gs_matrix_translate3f(0.f, (float)inst->chEnglishY, 0.f);
        obs_source_video_render(inst->srcEnglish);
        gs_matrix_pop();
    } else {
        // ── 4. Lyrics text — offset by top padding ────────────
        gs_matrix_push();
        gs_matrix_translate3f(0.f, (float)inst->lyrTopPadding, 0.f);
        obs_source_video_render(inst->srcLyrics);
        gs_matrix_pop();
    }

    // ── 5. Section label — bottom-left ───────────────────────
    gs_matrix_push();
    gs_matrix_translate3f(0.f,(float)mainH,0.f);
    obs_source_video_render(inst->srcSection);
    gs_matrix_pop();

    // ── 6. Song title — bottom-right ─────────────────────────
    gs_matrix_push();
    gs_matrix_translate3f((float)halfW,(float)mainH,0.f);
    obs_source_video_render(inst->srcTitle);
    gs_matrix_pop();

    // ── 7. QR overlay — bottom-right corner (200×200 px) ─────
    if (s_qrVisible && inst->srcQr) {
        const int QR_SIZE = 200;
        const int QR_MARGIN = 10;
        float qrX = (float)(cW - QR_SIZE - QR_MARGIN);
        float qrY = (float)(cH - QR_SIZE - QR_MARGIN);
        gs_matrix_push();
        gs_matrix_translate3f(qrX, qrY, 0.f);
        obs_source_video_render(inst->srcQr);
        gs_matrix_pop();
    }

    UNUSED_PARAMETER(effect);
}

// ── Source activate / deactivate ─────────────────────────────
// When the Lyrics source goes inactive (hidden/removed from scene),
// stop all media so nothing plays in the background.
// When it becomes active again, ensure a clean stopped state.
static void src_deactivate(void *data)
{
    QMutexLocker lk(&s_mu);
    // If another instance is still active, leave media alone
    for (auto *inst : s_instances) {
        if (inst != static_cast<LyricsInstance*>(data) &&
            obs_source_active(inst->self)) return;
    }
    s_mvMode = false; s_mvMusicOnly = false;
    s_mediaPath.clear();
    s_audioPath.clear();
    for (auto *inst : s_instances) {
        destroyMusicChild(inst);
        destroyAudioSlot(inst);
        applyBgPath(inst, QString(), 0);
    }
}

static void src_activate(void *data)
{
    (void)data;
    QMutexLocker lk(&s_mu);
    if (s_mvMode || !s_mediaPath.isEmpty() || !s_audioPath.isEmpty()) {
        s_mvMode = false; s_mvMusicOnly = false;
        s_mediaPath.clear();
        s_audioPath.clear();
        for (auto *inst : s_instances) {
            destroyMusicChild(inst);
            destroyAudioSlot(inst);
            applyBgPath(inst, QString(), 0);
        }
    }
}

// ── Registration ──────────────────────────────────────────────
void lyrics_renderer_register()
{
    struct obs_source_info info={};
    info.id             = LYRICS_SOURCE_ID;
    info.type           = OBS_SOURCE_TYPE_INPUT;
    info.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO |
                          OBS_SOURCE_CUSTOM_DRAW;
    info.get_name       = src_name;
    info.create         = src_create;
    info.destroy        = src_destroy;
    info.get_defaults   = src_defaults;
    info.get_properties = src_props;
    info.update         = src_update;
    info.get_width      = src_width;
    info.get_height     = src_height;
    info.video_render   = src_render;
    info.activate       = src_activate;
    info.deactivate     = src_deactivate;
    obs_register_source(&info);
}

// ── Public API ────────────────────────────────────────────────
void lyrics_renderer_push(const QString &lyrics,
                          const QString &section,
                          const QString &title)
{
    QMutexLocker lk(&s_mu);
    s_scriptureMode=false;
    s_scripTitle.clear(); s_scripText.clear();
    s_chineseMode=false;
    s_pinyin.clear(); s_english.clear();
    s_lyrics=lyrics; s_section=section; s_title=title;
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateScriptureChildren(inst,s,QString(),QString()); // clear scripture
        clearChineseChildren(inst,s);                        // clear Chinese layers
        updateTextChildren(inst,s,s_lyrics,s_section,s_title);
        obs_data_release(s);
    }
}

void lyrics_renderer_clear()
{
    QMutexLocker lk(&s_mu);
    s_scriptureMode=false;
    s_scripTitle.clear(); s_scripText.clear();
    s_chineseMode=false;
    s_pinyin.clear(); s_english.clear();
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateScriptureChildren(inst,s,QString(),QString());
        clearChineseChildren(inst,s);
        updateTextChildren(inst,s,QString(),QString(),QString());
        obs_data_release(s);
    }
}

void lyrics_renderer_push_chinese(const QString &chinese,
                                  const QString &pinyin,
                                  const QString &english,
                                  const QString &section,
                                  const QString &title)
{
    QMutexLocker lk(&s_mu);
    s_scriptureMode=false;
    s_scripTitle.clear(); s_scripText.clear();
    s_chineseMode=true;
    s_lyrics =chinese; // srcLyrics is re-used for Chinese characters
    s_pinyin =pinyin;
    s_english=english;
    s_section=section;
    s_title  =title;
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateScriptureChildren(inst,s,QString(),QString());
        updateChineseChildren(inst,s,pinyin,chinese,english);
        int halfW=inst->canvasW/2, barH=inst->bottomBarH;
        pushTextChild(inst->srcSection,s,"section_",section,halfW,barH);
        pushTextChild(inst->srcTitle,  s,"title_",  title,  halfW,barH);
        obs_data_release(s);
    }
}

// Returns the active non-MV background video source (for scrubber/pause controls).
static obs_source_t *getBgVideoSrc(LyricsInstance *inst)
{
    for (int i = 3; i >= 2; i--) {
        auto &sl = inst->bgSlots[i];
        if (sl.src && sl.isVideo && !sl.isMvSlot &&
            (sl.state == BgState::ACTIVE || sl.state == BgState::FADING_IN))
            return sl.src;
    }
    return nullptr;
}

// Returns the active MV video source (isMvSlot=true).
static obs_source_t *getMvVideoSrc(LyricsInstance *inst)
{
    for (int i = 0; i < 4; i++) {
        auto &sl = inst->bgSlots[i];
        if (sl.src && sl.isVideo && sl.isMvSlot)
            return sl.src;
    }
    return nullptr;
}

// ── Background debug API ──────────────────────────────────────
void lyrics_renderer_set_bg_debug(bool enable)
{
    QMutexLocker lk(&s_mu);
    s_bgDebugLayout = enable;
}
bool lyrics_renderer_get_bg_debug()
{
    QMutexLocker lk(&s_mu);
    return s_bgDebugLayout;
}

void lyrics_renderer_media_push(const QString &filePath, int fadeMs)
{
    { QMutexLocker lk(&s_mu); s_mediaPath=filePath; s_mvMode=false; s_mvMusicOnly=false; }
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        applyBgPath(inst, filePath, fadeMs, true, false);
}

void lyrics_renderer_media_clear(int fadeMs)
{
    QMutexLocker lk(&s_mu);
    s_mvMode = false; s_mvMusicOnly = false;
    s_mediaPath.clear();
    for(auto *inst:s_instances)
        applyBgPath(inst, QString(), fadeMs);
}

bool lyrics_renderer_media_get_time(int64_t &timeMs,int64_t &durationMs)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_source_t *src = getBgVideoSrc(inst);
        if (!src) continue;
        timeMs    = obs_source_media_get_time(src);
        durationMs= obs_source_media_get_duration(src);
        return true;
    }
    return false;
}

void lyrics_renderer_media_set_time(int64_t timeMs)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_source_t *src = getBgVideoSrc(inst);
        if (src) obs_source_media_set_time(src, timeMs);
    }
}

void lyrics_renderer_media_play_pause(bool pause)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_source_t *src = getBgVideoSrc(inst);
        if (src) obs_source_media_play_pause(src, pause);
    }
}

// ── AudioBin API ──────────────────────────────────────────────

void lyrics_renderer_audio_push(const QString &path)
{
    QMutexLocker lk(&s_mu);
    s_audioPath = path;
    for (auto *inst : s_instances)
        loadAudioSlot(inst, path);
}

void lyrics_renderer_audio_clear()
{
    QMutexLocker lk(&s_mu);
    s_audioPath.clear();
    for (auto *inst : s_instances)
        destroyAudioSlot(inst);
}

// ── MV Tab API ────────────────────────────────────────────────

void lyrics_renderer_mv_video_push(const QString &filePath, int fadeMs)
{
    QMutexLocker lk(&s_mu);
    s_mvMode      = true;
    s_mvMusicOnly = false;
    s_mediaPath   = filePath;
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    s_scriptureMode = false; s_scripTitle.clear(); s_scripText.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateTextChildren(inst,s,QString(),QString(),QString());
        updateScriptureChildren(inst,s,QString(),QString());
        obs_data_release(s);
        applyBgPath(inst, filePath, fadeMs, false, true); // isMvSlot=true, non-looping
    }
}

// ── VLC source helpers ────────────────────────────────────────
// VLC source ("vlc_source") uses libVLC which handles MP3 seeking
// correctly, unlike OBS's ffmpeg_source which snaps to keyframes
// and can reset to the beginning on seek. We prefer VLC for music.

// Returns true once if the OBS VLC plugin is installed.
// Result is cached so the probe source is only created once.
static bool vlcAvailable()
{
    static int s_cached = -1; // -1=unknown, 0=no, 1=yes
    if (s_cached >= 0) return s_cached == 1;
    obs_source_t *probe = obs_source_create_private(
        "vlc_source", "_vlc_probe", nullptr);
    s_cached = probe ? 1 : 0;
    if (probe) obs_source_release(probe);
    return s_cached == 1;
}

bool lyrics_renderer_vlc_available() { return vlcAvailable(); }

// Creates a VLC child source playing a single audio file.
// Music is stored in inst->musicSrc — completely independent of bgSlots
// so that pushing a picture/video never destroys the music source.
// Falls back to an ffmpeg_source stored in musicSrc if VLC is unavailable.
static void applyMusicPathVlc(LyricsInstance *inst, const QString &path)
{
    obs_data_t *d = obs_data_create();
    obs_source_t *newChild = obs_source_create_private(
        "vlc_source", "lyrics_music_vlc", d);
    obs_data_release(d);

    if (!newChild) {
        // VLC plugin present but source creation failed — try ffmpeg fallback
        // stored in musicSrc so it still doesn't conflict with bgSlots.
        obs_data_t *fd = obs_data_create();
        newChild = obs_source_create_private("ffmpeg_source", "lyrics_music_ffmpeg", fd);
        obs_data_release(fd);
        if (!newChild) return;

        obs_source_add_active_child(inst->self, newChild);
        obs_source_inc_active(newChild);
        obs_source_add_audio_capture_callback(newChild, mediaAudioCb, inst);

        obs_data_t *cfg = obs_data_create();
        obs_data_set_string(cfg, "local_file",       path.toUtf8().constData());
        obs_data_set_bool  (cfg, "is_local_file",    true);
        obs_data_set_bool  (cfg, "looping",          false);
        obs_data_set_bool  (cfg, "hw_decode",        true);
        obs_data_set_bool  (cfg, "restart_on_activate", false);
        obs_data_set_int   (cfg, "buffering_mb",     2);
        obs_source_update(newChild, cfg);
        obs_data_release(cfg);

        destroyMusicChild(inst);
        inst->musicSrc    = newChild;
        inst->musicActive = true;
        return;
    }

    // Activate before configuring so OBS starts the decode pipeline
    obs_source_add_active_child(inst->self, newChild);
    obs_source_inc_active(newChild);
    obs_source_add_audio_capture_callback(newChild, mediaAudioCb, inst);

    // Build a single-item VLC playlist
    obs_data_array_t *playlist = obs_data_array_create();
    obs_data_t *item = obs_data_create();
    obs_data_set_string(item, "value",    path.toUtf8().constData());
    obs_data_set_bool  (item, "hidden",   false);
    obs_data_set_bool  (item, "selected", false);
    obs_data_array_push_back(playlist, item);
    obs_data_release(item);

    obs_data_t *cfg = obs_data_create();
    obs_data_set_array(cfg, "playlist", playlist);
    obs_data_array_release(playlist);
    obs_data_set_bool(cfg, "loop",    false); // stop after track ends
    obs_data_set_bool(cfg, "shuffle", false);
    obs_source_update(newChild, cfg);
    obs_data_release(cfg);

    // Tear down the old music child only — bgSlots (background) are untouched
    destroyMusicChild(inst);

    inst->musicSrc    = newChild;
    inst->musicActive = true;
}

void lyrics_renderer_mv_music_push(const QString &filePath)
{
    // Probe VLC availability outside the mutex (OBS source creation
    // is thread-safe but we avoid holding s_mu during the probe).
    bool useVlc = vlcAvailable();

    QMutexLocker lk(&s_mu);
    // NOTE: We do NOT set s_mvMusicOnly=true here. Music is stored in
    // inst->musicSrc (separate from bgSlots), so album art from the VLC source
    // can never reach the render pipeline. Keeping s_mvMusicOnly=false ensures
    // any background in bgSlots continues to render uninterrupted.
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    s_scriptureMode = false; s_scripTitle.clear(); s_scripText.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateTextChildren(inst,s,QString(),QString(),QString());
        updateScriptureChildren(inst,s,QString(),QString());
        obs_data_release(s);
        // applyMusicPathVlc stores music in inst->musicSrc; falls back to
        // ffmpeg in musicSrc if VLC source creation fails.
        applyMusicPathVlc(inst, filePath);
    }
    (void)useVlc; // applyMusicPathVlc handles VLC/ffmpeg selection internally
}

void lyrics_renderer_mv_clear(int fadeMs)
{
    QMutexLocker lk(&s_mu);
    if (!s_mvMode) return;
    s_mvMode = false; s_mvMusicOnly = false;
    s_mediaPath.clear();
    for(auto *inst:s_instances){
        destroyMusicChild(inst);
        applyBgPath(inst, QString(), fadeMs);
    }
}

void lyrics_renderer_mv_music_clear()
{
    // Stop only the music source — leave bgSlots (background) untouched.
    // Only exit MV mode if music was the sole occupant (s_mvMusicOnly=true).
    // If a video is playing (s_mvMusicOnly=false), s_mvMode must stay true so
    // that the video tab's stop/play-pause/scrubber controls keep working.
    QMutexLocker lk(&s_mu);
    if (s_mvMusicOnly) s_mvMode = false;
    s_mvMusicOnly = false;
    for(auto *inst:s_instances)
        destroyMusicChild(inst);
}

int lyrics_renderer_mv_get_state()
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return -1;
    for(auto *inst:s_instances){
        if(s_mvMusicOnly && inst->musicSrc && inst->musicActive)
            return (int)obs_source_media_get_state(inst->musicSrc);
        obs_source_t *src = getMvVideoSrc(inst);
        if (src) return (int)obs_source_media_get_state(src);
    }
    return -1;
}

bool lyrics_renderer_mv_get_time(int64_t &timeMs,int64_t &durationMs)
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return false;
    for(auto *inst:s_instances){
        if(s_mvMusicOnly && inst->musicSrc && inst->musicActive){
            timeMs    = obs_source_media_get_time(inst->musicSrc);
            durationMs= obs_source_media_get_duration(inst->musicSrc);
            return true;
        }
        obs_source_t *src = getMvVideoSrc(inst);
        if (!src) continue;
        timeMs    = obs_source_media_get_time(src);
        durationMs= obs_source_media_get_duration(src);
        return true;
    }
    return false;
}

void lyrics_renderer_mv_set_time(int64_t timeMs)
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return;
    for(auto *inst:s_instances){
        if(s_mvMusicOnly && inst->musicSrc && inst->musicActive)
            obs_source_media_set_time(inst->musicSrc, timeMs);
        else {
            obs_source_t *src = getMvVideoSrc(inst);
            if (src) obs_source_media_set_time(src, timeMs);
        }
    }
}

void lyrics_renderer_mv_play_pause(bool pause)
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return;
    for(auto *inst:s_instances){
        if(s_mvMusicOnly && inst->musicSrc && inst->musicActive)
            obs_source_media_play_pause(inst->musicSrc, pause);
        else {
            obs_source_t *src = getMvVideoSrc(inst);
            if (src) obs_source_media_play_pause(src, pause);
        }
    }
}

// ── Music-specific query/control API ────────────────────────────────────────
// These bypass the s_mvMode guard and operate on inst->musicSrc directly.

int lyrics_renderer_music_get_state()
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        if(inst->musicSrc && inst->musicActive)
            return (int)obs_source_media_get_state(inst->musicSrc);
    return -1;
}

bool lyrics_renderer_music_get_time(int64_t &timeMs, int64_t &durationMs)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        if(!inst->musicSrc || !inst->musicActive) continue;
        timeMs     = obs_source_media_get_time(inst->musicSrc);
        durationMs = obs_source_media_get_duration(inst->musicSrc);
        return true;
    }
    return false;
}

void lyrics_renderer_music_set_time(int64_t timeMs)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        if(inst->musicSrc && inst->musicActive)
            obs_source_media_set_time(inst->musicSrc, timeMs);
}

void lyrics_renderer_music_play_pause(bool pause)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        if(inst->musicSrc && inst->musicActive)
            obs_source_media_play_pause(inst->musicSrc, pause);
}

// ── Scripture Tab API ─────────────────────────────────────────

void lyrics_renderer_scripture_push(const QString &title, const QString &text)
{
    QMutexLocker lk(&s_mu);
    s_scriptureMode = true;
    s_paragraphMode       = false;
    s_scripTitle    = title;
    s_scripText     = text;
    s_chineseMode=false; s_pinyin.clear(); s_english.clear();
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    QString dispText = applySuperscript(text);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        clearChineseChildren(inst,s);
        updateTextChildren(inst,s,QString(),QString(),QString());
        updateScriptureChildren(inst,s,title,dispText,false);
        obs_data_release(s);
    }
}

void lyrics_renderer_scripture_clear()
{
    QMutexLocker lk(&s_mu);
    if (s_paragraphMode) return; // don't clear if CTW is the active mode
    s_scriptureMode = false;
    s_scripTitle.clear();
    s_scripText.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateScriptureChildren(inst,s,QString(),QString(),false);
        obs_data_release(s);
    }
}

// ── Call to Worship Tab API ────────────────────────────────────

void lyrics_renderer_paragraph_push(const QString &title, const QString &text)
{
    QMutexLocker lk(&s_mu);
    s_scriptureMode = true;
    s_paragraphMode       = true;
    s_scripTitle    = title;
    s_scripText     = text;
    s_chineseMode=false; s_pinyin.clear(); s_english.clear();
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    QString dispText = applySuperscript(text);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        clearChineseChildren(inst,s);
        updateTextChildren(inst,s,QString(),QString(),QString());
        updateScriptureChildren(inst,s,title,dispText,true);
        obs_data_release(s);
    }
}

void lyrics_renderer_paragraph_clear()
{
    QMutexLocker lk(&s_mu);
    if (!s_paragraphMode) return; // don't clear if Scripture is the active mode
    s_scriptureMode = false;
    s_paragraphMode       = false;
    s_scripTitle.clear();
    s_scripText.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateScriptureChildren(inst,s,QString(),QString(),true);
        obs_data_release(s);
    }
}

void lyrics_renderer_set_paragraph_text_color(uint32_t fontColor, uint32_t outlineColor)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        obs_data_set_int(s,"paragraph_text_color",
                         (long long)(fontColor   & 0x00FFFFFF));
        obs_data_set_int(s,"paragraph_text_outline_color",
                         (long long)(outlineColor & 0x00FFFFFF));
        if(s_scriptureMode && s_paragraphMode)
            updateScriptureChildren(inst,s,
                s_scripTitle, applySuperscript(s_scripText), true);
        obs_data_release(s);
    }
}

// ── Settings access ────────────────────────────────────────────

obs_source_t *lyrics_renderer_get_source()
{
    QMutexLocker lk(&s_mu);
    return s_instances.isEmpty() ? nullptr : s_instances.first()->self;
}

void lyrics_renderer_set_text_color(uint32_t fontColor, uint32_t outlineColor)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        obs_data_set_int(s,"lyrics_color",
                         (long long)(fontColor   & 0x00FFFFFF));
        obs_data_set_int(s,"lyrics_outline_color",
                         (long long)(outlineColor & 0x00FFFFFF));
        updateTextChildren(inst,s,s_lyrics,s_section,s_title);
        obs_data_release(s);
    }
}

void lyrics_renderer_set_scripture_text_color(uint32_t fontColor,
                                               uint32_t outlineColor)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        obs_data_set_int(s,"scr_text_color",
                         (long long)(fontColor   & 0x00FFFFFF));
        obs_data_set_int(s,"scr_text_outline_color",
                         (long long)(outlineColor & 0x00FFFFFF));
        if(s_scriptureMode && !s_paragraphMode)
            updateScriptureChildren(inst,s,
                s_scripTitle, applySuperscript(s_scripText), false);
        obs_data_release(s);
    }
}

void lyrics_renderer_set_chinese_color(uint32_t fontColor, uint32_t outlineColor)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        obs_data_set_int(s,"ch_chinese_color",
                         (long long)(fontColor   & 0x00FFFFFF));
        obs_data_set_int(s,"ch_chinese_outline_color",
                         (long long)(outlineColor & 0x00FFFFFF));
        obs_data_set_int(s,"ch_english_color",
                         (long long)(fontColor   & 0x00FFFFFF));
        obs_data_set_int(s,"ch_english_outline_color",
                         (long long)(outlineColor & 0x00FFFFFF));
        if(s_chineseMode)
            updateChineseChildren(inst,s,s_pinyin,s_lyrics,s_english);
        obs_data_release(s);
    }
}

void lyrics_renderer_set_pinyin_color(uint32_t fontColor, uint32_t outlineColor)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        obs_data_set_int(s,"ch_pinyin_color",
                         (long long)(fontColor   & 0x00FFFFFF));
        obs_data_set_int(s,"ch_pinyin_outline_color",
                         (long long)(outlineColor & 0x00FFFFFF));
        if(s_chineseMode)
            updateChineseChildren(inst,s,s_pinyin,s_lyrics,s_english);
        obs_data_release(s);
    }
}

// ── QR Overlay API ────────────────────────────────────────────

void lyrics_renderer_qr_show(const QString &pngPath)
{
    QMutexLocker lk(&s_mu);
    s_qrVisible = true;
    s_qrPath    = pngPath;
    for (auto *inst : s_instances) {
        if (!inst->srcQr) continue;
        obs_data_t *d = obs_data_create();
        obs_data_set_string(d, "file",
                            pngPath.toUtf8().constData());
        obs_source_update(inst->srcQr, d);
        obs_data_release(d);
    }
}

void lyrics_renderer_qr_hide()
{
    QMutexLocker lk(&s_mu);
    s_qrVisible = false;
    for (auto *inst : s_instances) {
        if (!inst->srcQr) continue;
        obs_data_t *d = obs_data_create();
        obs_data_set_string(d, "file", "");
        obs_source_update(inst->srcQr, d);
        obs_data_release(d);
    }
}
