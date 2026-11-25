#pragma once
// song-manager.hpp  v10.0

#include <QString>
#include <QList>
#include <QPair>
#include <QJsonObject>
#include <QJsonArray>

enum class SectionType {
    Verse, Chorus, Bridge, PreChorus, Outro, Intro, Tag, Custom
};

QString     sectionTypeLabel(SectionType t);
SectionType sectionTypeFromString(const QString &s);

// Global lines-per-slide setting (applies to English songs only).
void setGlobalLinesPerSlide(int n);
int  getGlobalLinesPerSlide();

// ── Runtime slide struct ──────────────────────────────────────
// Returned by Song::allSlides() for both English and Chinese songs.
struct LyricsSlide {
    QString section;      // section label
    QString text;         // English lyrics  OR  Chinese characters
    QString pinyin;       // non-empty only when isChinese && showPinyin
    QString english;      // non-empty only when isChinese && showTranslation
    bool    isChinese = false;
};

// ── Section ───────────────────────────────────────────────────
struct LyricsSection {
    SectionType type  = SectionType::Verse;
    QString     label;

    // Read from files written before v12.27.0 only; never written.  Backgrounds now live on
    // slide cards (Segment::cards), so Segment::syncCards() moves this value there and clears it.
    QString     legacyBackgroundPath;

    // ── English mode (isChinese == false) ─────────────────────
    QString     text;   // English lyrics — blank line = slide break

    // ── Chinese mode (isChinese == true) ──────────────────────
    bool    isChinese       = false;
    bool    showPinyin      = false;  // Block 2 enabled
    bool    showTranslation = false;  // Block 3 enabled
    QString pinyin;    // parallel pinyin, same blank-line structure as text
    QString english;   // translation, same blank-line structure as text

    // Returns slides for English songs (uses global lpp + blank-line breaks).
    QList<QString>     slides()        const;

    // Returns slides for Chinese songs (blank-line = slide boundary in all blocks).
    QList<LyricsSlide> chineseSlides() const;

    QJsonObject toJson() const;
    static LyricsSection fromJson(const QJsonObject &o);
};

struct Song {
    QString              id;
    QString              title;
    QString              artist;
    QString              key;
    QString              tempo;
    QString              copyright;
    QList<LyricsSection> sections;

    // Returns all slides across all sections.
    // Each LyricsSlide carries isChinese, pinyin, english as needed.
    QList<LyricsSlide> allSlides() const;

    QJsonObject toJson() const;
    static Song fromJson(const QJsonObject &o);
};

class SongManager {
public:
    static SongManager &instance();
    void load();
    void save();
    // The library file is versioned (songs.json = {"version":2,"songs":[...]}).  A
    // version-1 file (bare array, had backgrounds) is upgraded once: the original is kept
    // as songs.v1.bak.json, then the file is rewritten without backgrounds.
    void ensureCurrent();
    // Replace the song with the same id, or add it when that id is not in the library.
    void upsertSong(const Song &s);
    const QList<Song> &songs() const { return m_songs; }
    Song *findById(const QString &id);
    void addSong(const Song &s);
    void updateSong(const Song &s);
    void removeSong(const QString &id);
    static Song parseQuickLyrics(const QString &title, const QString &text);
private:
    SongManager() = default;
    QList<Song> m_songs;
    int m_diskVersion = 2; // version of songs.json as read (1 = old bare array)
    QString dataPath() const;
};
