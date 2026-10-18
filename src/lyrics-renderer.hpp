#pragma once
// lyrics-renderer.hpp  v10.0

#include <obs-module.h>
#include <QString>

void lyrics_renderer_register();

// ── Lyrics API ────────────────────────────────────────────────
// English mode: push a single lyrics text block
void lyrics_renderer_push(const QString &lyrics,
                          const QString &sectionLabel,
                          const QString &songTitle);
// Chinese mode: pinyin on top, Chinese characters in middle, English translation bottom.
// Pass empty strings for layers that are disabled / not present.
void lyrics_renderer_push_chinese(const QString &chinese,
                                  const QString &pinyin,
                                  const QString &english,
                                  const QString &sectionLabel,
                                  const QString &songTitle);
void lyrics_renderer_clear();

// ── Lyrics-tab Media API ───────────────────────────────────────
// fadeMs: crossfade duration in ms (0 = instant cut)
void lyrics_renderer_media_push(const QString &filePath, int fadeMs);
void lyrics_renderer_media_clear(int fadeMs = 0);

bool lyrics_renderer_media_get_time(int64_t &timeMs,
                                    int64_t &durationMs);
void lyrics_renderer_media_set_time(int64_t timeMs);
void lyrics_renderer_media_play_pause(bool pause);

// Returns true if the OBS VLC plugin is installed and will be used for music.
bool lyrics_renderer_vlc_available();

// ── Scripture Tab API ──────────────────────────────────────────
void lyrics_renderer_scripture_push(const QString &title, const QString &text);
void lyrics_renderer_scripture_clear();

// ── Call to Worship Tab API ────────────────────────────────────
// Identical to Scripture API but uses independent ctw_title_ / ctw_text_
// font/layout settings so each tab can have its own look.
void lyrics_renderer_ctw_push(const QString &title, const QString &text);
void lyrics_renderer_ctw_clear();
void lyrics_renderer_set_ctw_text_color(uint32_t fontColor, uint32_t outlineColor);

// ── Settings access (used by LayoutDialog) ─────────────────────
// Returns the first active OBS source instance, or nullptr if none.
obs_source_t *lyrics_renderer_get_source();

// ── MV Tab API ────────────────────────────────────────────────
// While MV mode is active, text layers are suppressed on the OBS source.
// Calling lyrics_renderer_media_push() exits MV mode automatically.
void lyrics_renderer_mv_video_push(const QString &filePath, int fadeMs);
void lyrics_renderer_mv_music_push(const QString &filePath);
// Stop only the music source — does NOT affect background slots.
void lyrics_renderer_mv_music_clear();
void lyrics_renderer_mv_clear(int fadeMs = 0);

// ── AudioBin API ──────────────────────────────────────────────
// Routes audio-bin playback through a dedicated OBS ffmpeg_source slot so
// audio appears in the OBS sound meter and uses OBS audio monitoring.
void lyrics_renderer_audio_push(const QString &filePath);
void lyrics_renderer_audio_clear();

// Returns obs_media_state cast to int, or -1 when MV mode is inactive.
// OBS_MEDIA_STATE_ENDED == 6
int  lyrics_renderer_mv_get_state();
bool lyrics_renderer_mv_get_time(int64_t &timeMs, int64_t &durationMs);
void lyrics_renderer_mv_set_time(int64_t timeMs);
void lyrics_renderer_mv_play_pause(bool pause);

// ── Music-specific query/control API ─────────────────────────────────────────
// Operate on the dedicated musicSrc slot — bypass the s_mvMode guard so they
// work even when a background picture/video is simultaneously showing in bgSlots.
int  lyrics_renderer_music_get_state();
bool lyrics_renderer_music_get_time(int64_t &timeMs, int64_t &durationMs);
void lyrics_renderer_music_set_time(int64_t timeMs);
void lyrics_renderer_music_play_pause(bool pause);

// ── Background Debug API ──────────────────────────────────────
// When enabled, each of the 4 BG slots is rendered at 854×480 in its own
// quadrant on the OBS canvas (anchors: slot0=0,0  slot1=0,640  slot2=360,0
// slot3=360,640) so layering and fade states are directly visible.
// Has no effect on the dock's own col-2 preview.
void lyrics_renderer_set_bg_debug(bool enable);
bool lyrics_renderer_get_bg_debug();

// ── QR Overlay API ────────────────────────────────────────────
// Show a QR code image (80×80 px PNG) at bottom-right of the OBS canvas.
// Call qr_show() with the path to the pre-rendered PNG file,
// call qr_hide() to remove it.
void lyrics_renderer_qr_show(const QString &pngPath);
void lyrics_renderer_qr_hide();

// ── Color API ──────────────────────────────────────────────────
// fontColor / outlineColor: 0x00BBGGRR  (Windows COLORREF / OBS text-gdiplus format)
void lyrics_renderer_set_text_color(uint32_t fontColor, uint32_t outlineColor);
void lyrics_renderer_set_scripture_text_color(uint32_t fontColor, uint32_t outlineColor);
// Chinese mode: set Chinese character + translation color (used by color presets 1-3)
void lyrics_renderer_set_chinese_color(uint32_t fontColor, uint32_t outlineColor);
// Chinese mode: set pinyin color (used by color presets 4-5)
void lyrics_renderer_set_pinyin_color(uint32_t fontColor, uint32_t outlineColor);

#define LYRICS_SOURCE_ID "obs-lyrics-source"
