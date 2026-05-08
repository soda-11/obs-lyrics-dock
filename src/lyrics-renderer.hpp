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
void lyrics_renderer_media_clear();

bool lyrics_renderer_media_get_time(int64_t &timeMs,
                                    int64_t &durationMs);
void lyrics_renderer_media_set_time(int64_t timeMs);
void lyrics_renderer_media_play_pause(bool pause);

// Returns true if the OBS VLC plugin is installed and will be used for music.
bool lyrics_renderer_vlc_available();

// ── Scripture Tab API ──────────────────────────────────────────
// Shows scripture title + text in dedicated OBS text boxes.
// Automatically clears lyrics text so both don't show simultaneously.
void lyrics_renderer_scripture_push(const QString &title, const QString &text);
void lyrics_renderer_scripture_clear();

// ── MV Tab API ────────────────────────────────────────────────
// While MV mode is active, text layers are suppressed on the OBS source.
// Calling lyrics_renderer_media_push() exits MV mode automatically.
void lyrics_renderer_mv_video_push(const QString &filePath, int fadeMs);
void lyrics_renderer_mv_music_push(const QString &filePath);
void lyrics_renderer_mv_clear(int fadeMs = 0);

// Returns obs_media_state cast to int, or -1 when MV mode is inactive.
// OBS_MEDIA_STATE_ENDED == 6
int  lyrics_renderer_mv_get_state();
bool lyrics_renderer_mv_get_time(int64_t &timeMs, int64_t &durationMs);
void lyrics_renderer_mv_set_time(int64_t timeMs);
void lyrics_renderer_mv_play_pause(bool pause);

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
