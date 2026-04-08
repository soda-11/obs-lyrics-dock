// lyrics-renderer.cpp  v9.3
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

// ── Defaults ──────────────────────────────────────────────────
#define DEF_CANVAS_W     1280
#define DEF_CANVAS_H      720
#define DEF_BOTTOM_BAR_H   80

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

    // Text children
    obs_source_t *srcLyrics  = nullptr;
    obs_source_t *srcSection = nullptr;
    obs_source_t *srcTitle   = nullptr;

    // Canvas
    int canvasW    = DEF_CANVAS_W;
    int canvasH    = DEF_CANVAS_H;
    int bottomBarH = DEF_BOTTOM_BAR_H;
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
// fade_transition. fadeMs=0 = instant cut.
static void applyMediaPath(LyricsInstance *inst,
                            const QString &path, int fadeMs)
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
        obs_data_set_bool  (cfg, "looping",             true);
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
    inst->canvasW    =(int)obs_data_get_int(settings,"canvas_w");
    inst->canvasH    =(int)obs_data_get_int(settings,"canvas_h");
    inst->bottomBarH =(int)obs_data_get_int(settings,"bottom_bar_h");
    if(!inst->canvasW)    inst->canvasW    =DEF_CANVAS_W;
    if(!inst->canvasH)    inst->canvasH    =DEF_CANVAS_H;
    if(!inst->bottomBarH) inst->bottomBarH =DEF_BOTTOM_BAR_H;

    auto makeGdi=[](const char *name)->obs_source_t*{
        obs_data_t *d=obs_data_create();
        obs_data_set_string(d,"text","");
        obs_data_set_bool(d,"word_wrap",true);
        auto *s=obs_source_create_private("text_gdiplus",name,d);
        obs_data_release(d);
        return s;
    };
    inst->srcLyrics  = makeGdi("lyrics_main");
    inst->srcSection = makeGdi("lyrics_section");
    inst->srcTitle   = makeGdi("lyrics_title");

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
      lk.unlock();
      applyMediaPath(inst,med,0); // no fade on first load
      updateTextChildren(inst,settings,ly,sec,ttl); }
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
    delete inst;
}

static void src_defaults(obs_data_t *s)
{
    obs_data_set_default_int(s,"canvas_w",     DEF_CANVAS_W);
    obs_data_set_default_int(s,"canvas_h",     DEF_CANVAS_H);
    obs_data_set_default_int(s,"bottom_bar_h", DEF_BOTTOM_BAR_H);

    uint32_t white=0x00FFFFFFu,black=0x00000000u;
    uint32_t lgrey=0x00CCCCCCu,mgrey=0x00AAAAAAu;

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
    setBox("lyrics_", 48,1,1,white);
    setBox("section_",22,0,1,lgrey);
    setBox("title_",  22,2,1,mgrey);
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
    obs_properties_add_int(cgrp,"canvas_w",    "Width (px)",   320,7680,1);
    obs_properties_add_int(cgrp,"canvas_h",    "Height (px)",  180,4320,1);
    obs_properties_add_int(cgrp,"bottom_bar_h","Bottom bar height (px)",10,400,1);
    obs_properties_add_text(cgrp,"_info",
        "Render order (bottom to top):\n"
        "  1. Media layer  (gallery images/videos)\n"
        "  2. Lyrics text  (full canvas)\n"
        "  3. Section label (bottom-left)\n"
        "  4. Song title   (bottom-right)",
        OBS_TEXT_INFO);
    obs_properties_add_group(props,"canvas_grp",
        "Canvas & Layout",OBS_GROUP_NORMAL,cgrp);
    addBoxGroup(props,"lyrics_", "1.  Lyrics Text");
    addBoxGroup(props,"section_","2.  Section Label  (bottom-left)");
    addBoxGroup(props,"title_",  "3.  Song Title  (bottom-right)");
    return props;
}

static void src_update(void *data, obs_data_t *settings)
{
    auto *inst=static_cast<LyricsInstance*>(data);
    inst->canvasW    =(int)obs_data_get_int(settings,"canvas_w");
    inst->canvasH    =(int)obs_data_get_int(settings,"canvas_h");
    inst->bottomBarH =(int)obs_data_get_int(settings,"bottom_bar_h");
    if(!inst->canvasW)    inst->canvasW    =DEF_CANVAS_W;
    if(!inst->canvasH)    inst->canvasH    =DEF_CANVAS_H;
    if(!inst->bottomBarH) inst->bottomBarH =DEF_BOTTOM_BAR_H;
    QMutexLocker lk(&s_mu);
    updateTextChildren(inst,settings,s_lyrics,s_section,s_title);
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
    if (inst->transitionSrc) {
        obs_transition_set_size(inst->transitionSrc,
            (uint32_t)cW, (uint32_t)cH);
        obs_source_video_render(inst->transitionSrc);
    }

    // ── 2. Lyrics text — full canvas ─────────────────────────
    gs_matrix_push();
    gs_matrix_translate3f(0.f,0.f,0.f);
    obs_source_video_render(inst->srcLyrics);
    gs_matrix_pop();

    // ── 3. Section label — bottom-left ───────────────────────
    gs_matrix_push();
    gs_matrix_translate3f(0.f,(float)mainH,0.f);
    obs_source_video_render(inst->srcSection);
    gs_matrix_pop();

    // ── 4. Song title — bottom-right ─────────────────────────
    gs_matrix_push();
    gs_matrix_translate3f((float)halfW,(float)mainH,0.f);
    obs_source_video_render(inst->srcTitle);
    gs_matrix_pop();

    UNUSED_PARAMETER(effect);
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
    obs_register_source(&info);
}

// ── Public API ────────────────────────────────────────────────
void lyrics_renderer_push(const QString &lyrics,
                          const QString &section,
                          const QString &title)
{
    QMutexLocker lk(&s_mu);
    s_lyrics=lyrics; s_section=section; s_title=title;
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateTextChildren(inst,s,s_lyrics,s_section,s_title);
        obs_data_release(s);
    }
}

void lyrics_renderer_clear()
{
    QMutexLocker lk(&s_mu);
    s_lyrics.clear(); s_section.clear(); s_title.clear();
    for(auto *inst:s_instances){
        obs_data_t *s=obs_source_get_settings(inst->self);
        updateTextChildren(inst,s,QString(),QString(),QString());
        obs_data_release(s);
    }
}

void lyrics_renderer_media_push(const QString &filePath, int fadeMs)
{
    { QMutexLocker lk(&s_mu); s_mediaPath=filePath; }
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        applyMediaPath(inst,filePath,fadeMs);
}

void lyrics_renderer_media_clear()
{
    { QMutexLocker lk(&s_mu); s_mediaPath.clear(); }
    QMutexLocker lk(&s_mu);
    for(auto *inst:s_instances)
        applyMediaPath(inst,QString(),0);
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
