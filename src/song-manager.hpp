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
    QString backgroundPath; // effective background (section override → song default → "")
};

// ── Section ───────────────────────────────────────────────────
struct LyricsSection {
    SectionType type  = SectionType::Verse;
    QString     label;

    QString     backgroundPath; // optional background media (empty = inherit song default)

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
    QString              backgroundPath; // default background for all sections of this song
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
    const QList<Song> &songs() const { return m_songs; }
    Song *findById(const QString &id);
    void addSong(const Song &s);
    void updateSong(const Song &s);
    void removeSong(const QString &id);
    static Song parseQuickLyrics(const QString &title, const QString &text);
private:
    SongManager() = default;
    QList<Song> m_songs;
    QString dataPath() const;
};
