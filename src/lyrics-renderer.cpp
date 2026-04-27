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

// MV mode: set when MV tab owns the media source.
static bool    s_mvMode      = false;
// Music-only MV mode: when true the media source carries audio only
// (e.g. mp3 with embedded album art).  We skip rendering the transition
// source so album-art frames never appear on the OBS canvas.
static bool    s_mvMusicOnly = false;

// Chinese mode: set when a Chinese (trilingual) slide is active.
// In this mode srcLyrics carries Chinese characters, srcPinyin carries
// pinyin (top), and srcEnglish carries the English translation (bottom).
static bool    s_chineseMode = false;
static QString s_pinyin;    // cached pinyin for re-apply on settings change
static QString s_english;   // cached English translation

// Scripture mode: set when Scripture tab is displaying content.
// Lyrics and scripture text boxes are mutually cleared on push.
static bool    s_scriptureMode = false;
static QString s_scripTitle;
static QString s_scripText;

// ── Defaults ──────────────────────────────────────────────────
#define DEF_CANVAS_W      1280
#define DEF_CANVAS_H       720
#define DEF_BOTTOM_BAR_H    80
#define DEF_SCRIP_TITLE_H   70   // height of scripture title strip (px)
#define DEF_SCRIP_H_MARGIN 100   // left/right horizontal margin (px)
#define DEF_CH_PINYIN_Y      0   // Y start of pinyin source (overlaps Chinese)
#define DEF_CH_CHINESE_Y    34   // Y start of Chinese chars (~1 pinyin line below)
#define DEF_CH_PINYIN_H    320   // extents height of pinyin text box
#define DEF_CH_CHINESE_H   320   // extents height of Chinese char text box
#define DEF_CH_LINE_SPACING  2   // extra blank lines inserted between each content line

// ── File-type helpers ─────────────────────────────────────────
static const QSet<QString> s_imageExts = {
    "jpg","jpeg","png","gif","bmp","webp","tga","tiff"
};
static bool isImageFile(const QString &p)
{
    return s_imageExts.contains(QFileInfo(p).suffix().toLower());
}

// ── Per-instance ──────────────────────────────────────────────
struct LyricsInstance {
    obs_source_t *self          = nullptr;

    // Current active media child (image_source or ffmpeg_source)
    obs_source_t *srcMedia      = nullptr;
    bool          mediaIsImg    = false;
    bool          mediaActive   = false;

    // OBS built-in fade_transition source — handles crossfade between media
    obs_source_t *transitionSrc = nullptr;

    // Lyrics text children (English mode)
    obs_source_t *srcLyrics  = nullptr;
    obs_source_t *srcSection = nullptr;
    obs_source_t *srcTitle   = nullptr;

    // Chinese mode: pinyin (top) and English translation (bottom).
    // srcLyrics is re-used as the Chinese character source.
    obs_source_t *srcPinyin  = nullptr;
    obs_source_t *srcEnglish = nullptr;

    // Scripture text children
    obs_source_t *srcScripTitle = nullptr;
    obs_source_t *srcScripText  = nullptr;

    // Canvas
    int canvasW      = DEF_CANVAS_W;
    int canvasH      = DEF_CANVAS_H;
    int bottomBarH   = DEF_BOTTOM_BAR_H;
    int scripTitleH  = DEF_SCRIP_TITLE_H;
    int scripHMargin = DEF_SCRIP_H_MARGIN;

    // Chinese mode layout — start positions (Y) and extents heights
    int chPinyinY   = DEF_CH_PINYIN_Y;    // Y start of pinyin source
    int chChineseY  = DEF_CH_CHINESE_Y;   // Y start of Chinese char source
    int chPinyinH   = DEF_CH_PINYIN_H;    // extents height of pinyin box
    int chChineseH  = DEF_CH_CHINESE_H;   // extents height of Chinese char box
    int chLineSpacing = DEF_CH_LINE_SPACING; // extra blank lines between content pairs

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
                          const QString &text, int extW, int extH)
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
    pushTextChild(inst->srcLyrics, s,"lyrics_", lyrics, inst->canvasW,mainH);
    pushTextChild(inst->srcSection,s,"section_",section,halfW,        barH);
    pushTextChild(inst->srcTitle,  s,"title_",  title,  halfW,        barH);
}

static void updateScriptureChildren(LyricsInstance *inst, obs_data_t *s,
                                    const QString &title, const QString &text)
{
    // Text boxes are inset by scripHMargin px on each side horizontally.
    int textW = qMax(64, inst->canvasW - 2 * inst->scripHMargin);
    pushTextChild(inst->srcScripTitle, s, "scr_title_", title,
                  textW, inst->scripTitleH);
    pushTextChild(inst->srcScripText,  s, "scr_text_",  text,
                  textW, inst->canvasH - inst->scripTitleH);
}

// ── Chinese slide child push ──────────────────────────────────
// srcPinyin and srcLyrics (Chinese chars) are positioned at independent
// start-Y values so they OVERLAP and interleave visually.
// Extra blank lines (chLineSpacing) are inserted between each content
// line so each pair has room for the other source's text to fit into.
// srcEnglish starts immediately below the Chinese text box.
static void updateChineseChildren(LyricsInstance *inst, obs_data_t *s,
                                  const QString &pinyin,
                                  const QString &chinese,
                                  const QString &english)
{
    // Insert blank lines between content lines so the two overlapping
    // sources interleave correctly.  The user tunes ch_pinyin_y /
    // ch_chinese_y / ch_line_spacing in OBS properties until aligned.
    auto addSpacing = [](const QString &text, int sp) -> QString {
        if (sp <= 0 || text.isEmpty()) return text;
        QStringList lines  = text.split('\n');
        QStringList result;
        for (int i = 0; i < lines.size(); ++i) {
            result << lines[i];
            if (i < lines.size() - 1) {
                for (int k = 0; k < sp; ++k) result << QString();
            }
        }
        return result.join('\n');
    };
    int sp = inst->chLineSpacing;
    pushTextChild(inst->srcPinyin, s, "ch_pinyin_",
                  addSpacing(pinyin,  sp), inst->canvasW, inst->chPinyinH);
    pushTextChild(inst->srcLyrics, s, "ch_chinese_",
                  addSpacing(chinese, sp), inst->canvasW, inst->chChineseH);
    // English translation starts below the Chinese text box
    int englishH = qMax(0, inst->canvasH - inst->bottomBarH
                           - inst->chChineseY - inst->chChineseH);
    pushTextChild(inst->srcEnglish, s, "ch_english_", english,
                  inst->canvasW, englishH);
    inst->chineseMode = true;
}

// Clears both Chinese-mode extra sources (call when switching to English or clearing).
static void clearChineseChildren(LyricsInstance *inst, obs_data_t *s)
{
    pushTextChild(inst->srcPinyin,  s, "ch_pinyin_",  QString(), inst->canvasW, inst->chPinyinH);
    pushTextChild(inst->srcEnglish, s, "ch_english_", QString(), inst->canvasW,
                  qMax(0, inst->canvasH - inst->bottomBarH
                            - inst->chChineseY - inst->chChineseH));
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

// ── Media child management ────────────────────────────────────

static void destroyMediaChild(LyricsInstance *inst)
{
    if (!inst->srcMedia) return;
    if (!inst->mediaIsImg)
        obs_source_remove_audio_capture_callback(
            inst->srcMedia, mediaAudioCb, inst);
    obs_source_dec_active(inst->srcMedia);
    obs_source_remove_active_child(inst->self, inst->srcMedia);
    obs_source_release(inst->srcMedia);
    inst->srcMedia    = nullptr;
    inst->mediaActive = false;
}

// Creates a new media child, activates it, and starts the OBS
// fade_transition. fadeMs=0 = instant cut. looping=false for MV sources.
static void applyMediaPath(LyricsInstance *inst,
                            const QString &path, int fadeMs,
                            bool looping = true)
{
    if (path.isEmpty()) {
        if (inst->transitionSrc)
            obs_transition_start(inst->transitionSrc,
                OBS_TRANSITION_MODE_AUTO,
                (uint32_t)(fadeMs > 0 ? fadeMs : 0), nullptr);
        destroyMediaChild(inst);
        return;
    }

    bool needImg    = isImageFile(path);
    const char *type= needImg ? "image_source" : "ffmpeg_source";

    // Create new child source
    obs_data_t *d  = obs_data_create();
    auto *newChild = obs_source_create_private(type,
                         "lyrics_media_child", d);
    obs_data_release(d);
    if (!newChild) return;

    // Activate new child so OBS starts the decode pipeline
    obs_source_add_active_child(inst->self, newChild);
    obs_source_inc_active(newChild);
    if (!needImg)
        obs_source_add_audio_capture_callback(
            newChild, mediaAudioCb, inst);

    // Configure file path
    obs_data_t *cfg = obs_data_create();
    if (needImg) {
        obs_data_set_string(cfg, "file", path.toUtf8().constData());
        obs_data_set_bool  (cfg, "unload", false);
    } else {
        obs_data_set_string(cfg, "local_file", path.toUtf8().constData());
        obs_data_set_bool  (cfg, "is_local_file",       true);
        obs_data_set_bool  (cfg, "looping",             looping);
        obs_data_set_bool  (cfg, "hw_decode",           true);
        obs_data_set_bool  (cfg, "restart_on_activate", false);
        obs_data_set_int   (cfg, "buffering_mb",        2);
    }
    obs_source_update(newChild, cfg);
    obs_data_release(cfg);

    // Remove old child audio callback before transition replaces it
    if (inst->srcMedia && !inst->mediaIsImg)
        obs_source_remove_audio_capture_callback(
            inst->srcMedia, mediaAudioCb, inst);

    // Start OBS fade transition to the new child — OBS handles blending
    if (inst->transitionSrc) {
        obs_transition_set_size(inst->transitionSrc,
            (uint32_t)inst->canvasW, (uint32_t)inst->canvasH);
        obs_transition_start(inst->transitionSrc,
            OBS_TRANSITION_MODE_AUTO,
            (uint32_t)(fadeMs > 0 ? fadeMs : 0),
            newChild);
    }

    // Release old child (transition holds a ref until done)
    if (inst->srcMedia) {
        obs_source_dec_active(inst->srcMedia);
        obs_source_remove_active_child(inst->self, inst->srcMedia);
        obs_source_release(inst->srcMedia);
    }
    inst->srcMedia    = newChild;
    inst->mediaIsImg  = needImg;
    inst->mediaActive = true;
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
    inst->chPinyinH   =(int)obs_data_get_int(settings,"ch_pinyin_h");
    inst->chChineseH  =(int)obs_data_get_int(settings,"ch_chinese_h");
    inst->chLineSpacing=(int)obs_data_get_int(settings,"ch_line_spacing");
    if(!inst->canvasW)       inst->canvasW      =DEF_CANVAS_W;
    if(!inst->canvasH)       inst->canvasH      =DEF_CANVAS_H;
    if(!inst->bottomBarH)    inst->bottomBarH   =DEF_BOTTOM_BAR_H;
    if(!inst->scripTitleH)   inst->scripTitleH  =DEF_SCRIP_TITLE_H;
    if(!inst->chPinyinH)     inst->chPinyinH    =DEF_CH_PINYIN_H;
    if(!inst->chChineseH)    inst->chChineseH   =DEF_CH_CHINESE_H;
    // ch_pinyin_y / ch_chinese_y may legitimately be 0 — use has_user_value
    if (!obs_data_has_user_value(settings,"ch_pinyin_y"))
        inst->chPinyinY  = DEF_CH_PINYIN_Y;
    else
        inst->chPinyinY  = (int)obs_data_get_int(settings,"ch_pinyin_y");
    if (!obs_data_has_user_value(settings,"ch_chinese_y"))
        inst->chChineseY = DEF_CH_CHINESE_Y;
    else
        inst->chChineseY = (int)obs_data_get_int(settings,"ch_chinese_y");
    // margin may legitimately be 0 — only default if key not found
    if (!obs_data_has_user_value(settings, "scrip_h_margin"))
        inst->scripHMargin = DEF_SCRIP_H_MARGIN;

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

    // Create OBS built-in fade transition source for media crossfade
    {
        obs_data_t *td = obs_data_create();
        inst->transitionSrc = obs_source_create_private(
            "fade_transition", "lyrics_media_fade", td);
        obs_data_release(td);
        if (inst->transitionSrc) {
            obs_source_add_active_child(inst->self, inst->transitionSrc);
            obs_source_inc_active(inst->transitionSrc);
            obs_transition_set_size(inst->transitionSrc,
                (uint32_t)inst->canvasW, (uint32_t)inst->canvasH);
        }
    }

    { QMutexLocker lk(&s_mu); s_instances<<inst; }
    { QMutexLocker lk(&s_mu);
      QString med=s_mediaPath, ly=s_lyrics,
              sec=s_section, ttl=s_title;
      QString stit=s_scripTitle, stxt=s_scripText;
      lk.unlock();
      applyMediaPath(inst,med,0); // no fade on first load
      updateTextChildren(inst,settings,ly,sec,ttl);
      updateScriptureChildren(inst,settings,stit,stxt); }
    return inst;
}

static void src_destroy(void *data)
{
    auto *inst=static_cast<LyricsInstance*>(data);
    { QMutexLocker lk(&s_mu); s_instances.removeAll(inst); }
    destroyMediaChild(inst);
    if (inst->transitionSrc) {
        obs_source_dec_active(inst->transitionSrc);
        obs_source_remove_active_child(inst->self, inst->transitionSrc);
        obs_source_release(inst->transitionSrc);
        inst->transitionSrc = nullptr;
    }
    obs_source_release(inst->srcLyrics);
    obs_source_release(inst->srcSection);
    obs_source_release(inst->srcTitle);
    obs_source_release(inst->srcPinyin);
    obs_source_release(inst->srcEnglish);
    obs_source_release(inst->srcScripTitle);
    obs_source_release(inst->srcScripText);
    delete inst;
}

static void src_defaults(obs_data_t *s)
{
    obs_data_set_default_int(s,"canvas_w",      DEF_CANVAS_W);
    obs_data_set_default_int(s,"canvas_h",      DEF_CANVAS_H);
    obs_data_set_default_int(s,"bottom_bar_h",  DEF_BOTTOM_BAR_H);
    obs_data_set_default_int(s,"scrip_title_h",  DEF_SCRIP_TITLE_H);
    obs_data_set_default_int(s,"scrip_h_margin", DEF_SCRIP_H_MARGIN);
    obs_data_set_default_int(s,"ch_pinyin_y",    DEF_CH_PINYIN_Y);
    obs_data_set_default_int(s,"ch_chinese_y",   DEF_CH_CHINESE_Y);
    obs_data_set_default_int(s,"ch_pinyin_h",    DEF_CH_PINYIN_H);
    obs_data_set_default_int(s,"ch_chinese_h",   DEF_CH_CHINESE_H);
    obs_data_set_default_int(s,"ch_line_spacing", DEF_CH_LINE_SPACING);

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
    obs_properties_add_int(cgrp,"canvas_w",     "Width (px)",   320,7680,1);
    obs_properties_add_int(cgrp,"canvas_h",     "Height (px)",  180,4320,1);
    obs_properties_add_int(cgrp,"bottom_bar_h", "Lyrics: bottom bar height (px)",10,400,1);
    obs_properties_add_int(cgrp,"scrip_title_h",  "Scripture: title strip height (px)",10,400,1);
    obs_properties_add_int(cgrp,"scrip_h_margin", "Scripture: left/right margin (px)",  0,400,1);
    obs_properties_add_int(cgrp,"ch_pinyin_y",  "Chinese: Pinyin starting Y position (px)",  0,720,1);
    obs_properties_add_int(cgrp,"ch_chinese_y", "Chinese: Character starting Y position (px)",0,720,1);
    obs_properties_add_int(cgrp,"ch_pinyin_h",  "Chinese: Pinyin text box height (px)",  20,600,1);
    obs_properties_add_int(cgrp,"ch_chinese_h", "Chinese: Character text box height (px)",20,600,1);
    obs_properties_add_int(cgrp,"ch_line_spacing","Chinese: Line spacing between pairs (blank lines)",0,5,1);
    obs_properties_add_text(cgrp,"_info",
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
        "  Scripture mode:\n"
        "    2. Scripture title (top strip)\n"
        "    3. Scripture text  (main area below title)",
        OBS_TEXT_INFO);
    obs_properties_add_group(props,"canvas_grp",
        "Canvas & Layout",OBS_GROUP_NORMAL,cgrp);
    addBoxGroup(props,"lyrics_", "1.  English Lyrics — Main Text");
    addBoxGroup(props,"section_","2.  Lyrics — Section Label  (bottom-left)");
    addBoxGroup(props,"title_",  "3.  Lyrics — Song Title  (bottom-right)");
    addBoxGroup(props,"scr_title_","4.  Scripture — Title  (top strip)");
    addBoxGroup(props,"scr_text_", "5.  Scripture — Text  (main area)");
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
    inst->chPinyinH    =(int)obs_data_get_int(settings,"ch_pinyin_h");
    inst->chChineseH   =(int)obs_data_get_int(settings,"ch_chinese_h");
    inst->chLineSpacing=(int)obs_data_get_int(settings,"ch_line_spacing");
    if(!inst->canvasW)      inst->canvasW      =DEF_CANVAS_W;
    if(!inst->canvasH)      inst->canvasH      =DEF_CANVAS_H;
    if(!inst->bottomBarH)   inst->bottomBarH   =DEF_BOTTOM_BAR_H;
    if(!inst->scripTitleH)  inst->scripTitleH  =DEF_SCRIP_TITLE_H;
    if(!inst->chPinyinH)    inst->chPinyinH    =DEF_CH_PINYIN_H;
    if(!inst->chChineseH)   inst->chChineseH   =DEF_CH_CHINESE_H;
    if (!obs_data_has_user_value(settings,"ch_pinyin_y"))
        inst->chPinyinY  = DEF_CH_PINYIN_Y;
    else
        inst->chPinyinY  = (int)obs_data_get_int(settings,"ch_pinyin_y");
    if (!obs_data_has_user_value(settings,"ch_chinese_y"))
        inst->chChineseY = DEF_CH_CHINESE_Y;
    else
        inst->chChineseY = (int)obs_data_get_int(settings,"ch_chinese_y");
    if(!obs_data_has_user_value(settings,"scrip_h_margin"))
        inst->scripHMargin = DEF_SCRIP_H_MARGIN;
    QMutexLocker lk(&s_mu);
    // Re-apply whichever mode is currently active so text re-layouts
    // correctly after a canvas size / font change.
    if (s_scriptureMode) {
        clearChineseChildren(inst, settings);
        updateTextChildren(inst,settings,QString(),QString(),QString());
        updateScriptureChildren(inst,settings,s_scripTitle,s_scripText);
    } else if (s_chineseMode) {
        updateScriptureChildren(inst,settings,QString(),QString());
        updateChineseChildren(inst,settings,s_pinyin,s_lyrics,s_english);
        int halfW=inst->canvasW/2, barH=inst->bottomBarH;
        pushTextChild(inst->srcSection,settings,"section_",s_section,halfW,barH);
        pushTextChild(inst->srcTitle,  settings,"title_",  s_title,  halfW,barH);
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

static void src_render(void *data, gs_effect_t *effect)
{
    auto *inst=static_cast<LyricsInstance*>(data);
    int cW=inst->canvasW, cH=inst->canvasH;
    int mainH=cH-inst->bottomBarH;
    int halfW=cW/2;

    // ── 1. Media layer — OBS fade_transition handles crossfade ──
    // Skip when s_mvMusicOnly: music files with embedded album art would
    // otherwise render a still image to the OBS canvas.
    if (inst->transitionSrc && !s_mvMusicOnly) {
        obs_transition_set_size(inst->transitionSrc,
            (uint32_t)cW, (uint32_t)cH);
        obs_source_video_render(inst->transitionSrc);
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
        // ── Chinese trilingual layout ─────────────────────────
        // Pinyin and Chinese chars start at independent user-configured Y
        // positions so they can OVERLAP and interleave visually.
        // English translation starts immediately below the Chinese text box.
        gs_matrix_push();
        gs_matrix_translate3f(0.f, (float)inst->chPinyinY, 0.f);
        obs_source_video_render(inst->srcPinyin);
        gs_matrix_pop();

        gs_matrix_push();
        gs_matrix_translate3f(0.f, (float)inst->chChineseY, 0.f);
        obs_source_video_render(inst->srcLyrics);
        gs_matrix_pop();

        int englishY = inst->chChineseY + inst->chChineseH;
        gs_matrix_push();
        gs_matrix_translate3f(0.f, (float)englishY, 0.f);
        obs_source_video_render(inst->srcEnglish);
        gs_matrix_pop();
    } else {
        // ── 4. Lyrics text — full canvas (English mode) ───────
        // Empty when in scripture mode; non-empty only when Lyrics tab is active.
        gs_matrix_push();
        gs_matrix_translate3f(0.f, 0.f, 0.f);
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
    for (auto *inst : s_instances)
        applyMediaPath(inst, QString(), 0, true);
}

static void src_activate(void *data)
{
    (void)data;
    // Ensure clean stopped state when source first becomes visible
    QMutexLocker lk(&s_mu);
    if (s_mvMode || !s_mediaPath.isEmpty()) {
        s_mvMode = false; s_mvMusicOnly = false;
        s_mediaPath.clear();
        for (auto *inst : s_instances)
            applyMediaPath(inst, QString(), 0, true);
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

void lyrics_renderer_media_push(const QString &filePath, int fadeMs)
{
    // Lyrics gallery takes back control — exit MV mode so text renders again.
    { QMutexLocker lk(&s_mu); s_mediaPath=filePath; s_mvMode=false; }
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        applyMediaPath(inst,filePath,fadeMs,true /*loop*/);
}

void lyrics_renderer_media_clear()
{
    QMutexLocker lk(&s_mu);
    s_mvMode = false; s_mvMusicOnly = false; // background Stop resets MV mode too
    s_mediaPath.clear();
    for(auto *inst:s_instances)
        applyMediaPath(inst,QString(),0,true);
}

bool lyrics_renderer_media_get_time(int64_t &timeMs,int64_t &durationMs)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        if(!inst->srcMedia||!inst->mediaActive||inst->mediaIsImg) continue;
        timeMs    =obs_source_media_get_time(inst->srcMedia);
        durationMs=obs_source_media_get_duration(inst->srcMedia);
        return true;
    }
    return false;
}

void lyrics_renderer_media_set_time(int64_t timeMs)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        if(inst->srcMedia&&inst->mediaActive&&!inst->mediaIsImg)
            obs_source_media_set_time(inst->srcMedia,timeMs);
}

void lyrics_renderer_media_play_pause(bool pause)
{
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        if(inst->srcMedia&&inst->mediaActive&&!inst->mediaIsImg)
            obs_source_media_play_pause(inst->srcMedia,pause);
}

// ── MV Tab API ────────────────────────────────────────────────

void lyrics_renderer_mv_video_push(const QString &filePath, int fadeMs)
{
    QMutexLocker lk(&s_mu);
    s_mvMode      = true;
    s_mvMusicOnly = false;   // video → render the media layer
    s_mediaPath   = filePath;
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    s_scriptureMode = false; s_scripTitle.clear(); s_scripText.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateTextChildren(inst,s,QString(),QString(),QString());
        updateScriptureChildren(inst,s,QString(),QString());
        obs_data_release(s);
        applyMediaPath(inst,filePath,fadeMs,false);
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
// Falls back to applyMediaPath(ffmpeg_source) if VLC is unavailable.
static void applyMusicPathVlc(LyricsInstance *inst, const QString &path)
{
    obs_data_t *d = obs_data_create();
    obs_source_t *newChild = obs_source_create_private(
        "vlc_source", "lyrics_music_vlc", d);
    obs_data_release(d);

    if (!newChild) {
        // VLC plugin present but source creation failed — fall back
        applyMediaPath(inst, path, 0, false);
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

    // Tear down the old child source
    if (inst->srcMedia && !inst->mediaIsImg)
        obs_source_remove_audio_capture_callback(
            inst->srcMedia, mediaAudioCb, inst);
    if (inst->srcMedia) {
        obs_source_dec_active(inst->srcMedia);
        obs_source_remove_active_child(inst->self, inst->srcMedia);
        obs_source_release(inst->srcMedia);
    }

    inst->srcMedia    = newChild;
    inst->mediaIsImg  = false;
    inst->mediaActive = true;
}

void lyrics_renderer_mv_music_push(const QString &filePath)
{
    // Probe VLC availability outside the mutex (OBS source creation
    // is thread-safe but we avoid holding s_mu during the probe).
    bool useVlc = vlcAvailable();

    QMutexLocker lk(&s_mu);
    s_mvMode      = true;
    s_mvMusicOnly = true;    // suppress media layer → no album art on canvas
    s_mediaPath   = filePath;
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    s_scriptureMode = false; s_scripTitle.clear(); s_scripText.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateTextChildren(inst,s,QString(),QString(),QString());
        updateScriptureChildren(inst,s,QString(),QString());
        obs_data_release(s);
        if (useVlc)
            applyMusicPathVlc(inst, filePath);  // VLC: accurate MP3 seek
        else
            applyMediaPath(inst, filePath, 0, false); // ffmpeg fallback
    }
}

void lyrics_renderer_mv_clear(int fadeMs)
{
    QMutexLocker lk(&s_mu);
    if (!s_mvMode) return; // background media is playing; don't clear it
    s_mvMode = false; s_mvMusicOnly = false;
    s_mediaPath.clear();
    for(auto *inst:s_instances)
        applyMediaPath(inst,QString(),fadeMs,true);
}

int lyrics_renderer_mv_get_state()
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return -1;
    for(auto *inst:s_instances){
        if(inst->srcMedia&&inst->mediaActive&&!inst->mediaIsImg)
            return (int)obs_source_media_get_state(inst->srcMedia);
    }
    return -1;
}

bool lyrics_renderer_mv_get_time(int64_t &timeMs,int64_t &durationMs)
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return false;
    for(auto *inst:s_instances){
        if(!inst->srcMedia||!inst->mediaActive||inst->mediaIsImg) continue;
        timeMs    =obs_source_media_get_time(inst->srcMedia);
        durationMs=obs_source_media_get_duration(inst->srcMedia);
        return true;
    }
    return false;
}

void lyrics_renderer_mv_set_time(int64_t timeMs)
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return;
    for(auto *inst:s_instances)
        if(inst->srcMedia&&inst->mediaActive&&!inst->mediaIsImg)
            obs_source_media_set_time(inst->srcMedia,timeMs);
}

void lyrics_renderer_mv_play_pause(bool pause)
{
    QMutexLocker lk(&s_mu);
    if(!s_mvMode) return;
    for(auto *inst:s_instances)
        if(inst->srcMedia&&inst->mediaActive&&!inst->mediaIsImg)
            obs_source_media_play_pause(inst->srcMedia,pause);
}

// ── Scripture Tab API ─────────────────────────────────────────

void lyrics_renderer_scripture_push(const QString &title, const QString &text)
{
    QMutexLocker lk(&s_mu);
    s_scriptureMode = true;
    s_scripTitle    = title;
    s_scripText     = text;
    // Clear lyrics so both don't show simultaneously
    s_chineseMode=false; s_pinyin.clear(); s_english.clear();
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        clearChineseChildren(inst,s);
        updateTextChildren(inst,s,QString(),QString(),QString()); // clear lyrics
        updateScriptureChildren(inst,s,title,text);
        obs_data_release(s);
    }
}

void lyrics_renderer_scripture_clear()
{
    QMutexLocker lk(&s_mu);
    s_scriptureMode = false;
    s_scripTitle.clear();
    s_scripText.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateScriptureChildren(inst,s,QString(),QString());
        obs_data_release(s);
    }
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
    // Update scr_text_ color (the main body box) for scripture mode.
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        obs_data_set_int(s,"scr_text_color",
                         (long long)(fontColor   & 0x00FFFFFF));
        obs_data_set_int(s,"scr_text_outline_color",
                         (long long)(outlineColor & 0x00FFFFFF));
        if(s_scriptureMode)
            updateScriptureChildren(inst,s,s_scripTitle,s_scripText);
        obs_data_release(s);
    }
}
