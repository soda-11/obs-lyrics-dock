#pragma once
// lyrics-renderer.hpp  v9.5.1

#include <obs-module.h>
#include <QString>

void lyrics_renderer_register();

// ── Lyrics API ────────────────────────────────────────────────
void lyrics_renderer_push(const QString &lyrics,
                          const QString &sectionLabel,
                          const QString &songTitle);
void lyrics_renderer_clear();

// ── Media API ─────────────────────────────────────────────────
// fadeMs: crossfade duration in ms (0 = instant cut)
void lyrics_renderer_media_push(const QString &filePath, int fadeMs);
void lyrics_renderer_media_clear();

bool lyrics_renderer_media_get_time(int64_t &timeMs,
                                    int64_t &durationMs);
void lyrics_renderer_media_set_time(int64_t timeMs);
void lyrics_renderer_media_play_pause(bool pause);

#define LYRICS_SOURCE_ID "obs-lyrics-source"
