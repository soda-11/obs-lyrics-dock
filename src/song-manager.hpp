#pragma once
// song-manager.hpp  v2.0

#include <QString>
#include <QList>
#include <QPair>
#include <QJsonObject>
#include <QJsonArray>

enum class SectionType {
    Verse, Chorus, Bridge, PreChorus, Outro, Intro, Tag, Custom
};

QString sectionTypeLabel(SectionType t);
SectionType sectionTypeFromString(const QString &s);

// Global lines-per-slide setting (default 4).
// Controlled by the spinbox in the slides header.
void setGlobalLinesPerSlide(int n);
int  getGlobalLinesPerSlide();

struct LyricsSection {
    SectionType type  = SectionType::Verse;
    QString     label;
    QString     text;
    QList<QString> slides() const; // split respecting blank-line breaks + global lpp
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

    QList<QPair<QString,QString>> allSlides() const;
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
