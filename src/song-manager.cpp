// song-manager.cpp  v2.0
#include "song-manager.hpp"

#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

QString sectionTypeLabel(SectionType t)
{
    switch (t) {
    case SectionType::Verse:     return "Verse";
    case SectionType::Chorus:    return "Chorus";
    case SectionType::Bridge:    return "Bridge";
    case SectionType::PreChorus: return "Pre-Chorus";
    case SectionType::Outro:     return "Outro";
    case SectionType::Intro:     return "Intro";
    case SectionType::Tag:       return "Tag";
    default:                     return "Section";
    }
}

SectionType sectionTypeFromString(const QString &s)
{
    QString l = s.toLower();
    if (l.startsWith("verse"))   return SectionType::Verse;
    if (l.startsWith("chorus"))  return SectionType::Chorus;
    if (l.startsWith("bridge"))  return SectionType::Bridge;
    if (l.startsWith("pre"))     return SectionType::PreChorus;
    if (l.startsWith("outro"))   return SectionType::Outro;
    if (l.startsWith("intro"))   return SectionType::Intro;
    if (l.startsWith("tag"))     return SectionType::Tag;
    return SectionType::Custom;
}

QList<QString> LyricsSection::slides() const
{
    QStringList lines = text.split('\n');
    QList<QString> result;
    const int lpp = 4;
    for (int i = 0; i < lines.size(); i += lpp) {
        QStringList chunk;
        for (int j = i; j < qMin(i + lpp, (int)lines.size()); ++j)
            if (!lines[j].trimmed().isEmpty())
                chunk << lines[j];
        if (!chunk.isEmpty())
            result << chunk.join('\n');
    }
    return result;
}

QJsonObject LyricsSection::toJson() const
{
    QJsonObject o;
    o["type"]  = sectionTypeLabel(type);
    o["label"] = label;
    o["text"]  = text;
    return o;
}

LyricsSection LyricsSection::fromJson(const QJsonObject &o)
{
    LyricsSection s;
    s.type  = sectionTypeFromString(o["type"].toString());
    s.label = o["label"].toString();
    s.text  = o["text"].toString();
    return s;
}

QList<QPair<QString,QString>> Song::allSlides() const
{
    QList<QPair<QString,QString>> result;
    for (const auto &sec : sections)
        for (const auto &slide : sec.slides())
            result << qMakePair(
                sec.label.isEmpty() ? sectionTypeLabel(sec.type) : sec.label,
                slide);
    return result;
}

QJsonObject Song::toJson() const
{
    QJsonObject o;
    o["id"]        = id;
    o["title"]     = title;
    o["artist"]    = artist;
    o["key"]       = key;
    o["tempo"]     = tempo;
    o["copyright"] = copyright;
    QJsonArray arr;
    for (const auto &s : sections) arr.append(s.toJson());
    o["sections"]  = arr;
    return o;
}

Song Song::fromJson(const QJsonObject &o)
{
    Song s;
    s.id        = o["id"].toString();
    s.title     = o["title"].toString();
    s.artist    = o["artist"].toString();
    s.key       = o["key"].toString();
    s.tempo     = o["tempo"].toString();
    s.copyright = o["copyright"].toString();
    for (const auto &v : o["sections"].toArray())
        s.sections << LyricsSection::fromJson(v.toObject());
    return s;
}

SongManager &SongManager::instance()
{
    static SongManager inst;
    return inst;
}

QString SongManager::dataPath() const
{
    QString dir = QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation)
                  + "/obs-lyrics-dock";
    QDir().mkpath(dir);
    return dir + "/songs.json";
}

void SongManager::load()
{
    QFile f(dataPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    m_songs.clear();
    for (const auto &v : doc.array())
        m_songs << Song::fromJson(v.toObject());
}

void SongManager::save()
{
    QJsonArray arr;
    for (const auto &s : m_songs) arr.append(s.toJson());
    QFile f(dataPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(arr).toJson());
}

Song *SongManager::findById(const QString &id)
{
    for (auto &s : m_songs)
        if (s.id == id) return &s;
    return nullptr;
}

void SongManager::addSong(const Song &s)
{
    m_songs << s;
    save();
}

void SongManager::updateSong(const Song &s)
{
    for (auto &existing : m_songs)
        if (existing.id == s.id) { existing = s; save(); return; }
}

void SongManager::removeSong(const QString &id)
{
    m_songs.erase(
        std::remove_if(m_songs.begin(), m_songs.end(),
                       [&](const Song &s){ return s.id == id; }),
        m_songs.end());
    save();
}

Song SongManager::parseQuickLyrics(const QString &title, const QString &raw)
{
    Song song;
    song.id    = QUuid::createUuid().toString(QUuid::WithoutBraces);
    song.title = title;

    QStringList lines = raw.split('\n');
    QRegularExpression hdr(R"(^\[(.+)\]\s*$)");
    LyricsSection cur;
    bool inSection = false;

    auto flush = [&](){
        if (inSection) {
            cur.text = cur.text.trimmed();
            if (!cur.text.isEmpty()) song.sections << cur;
        }
    };

    for (const QString &line : lines) {
        auto m = hdr.match(line.trimmed());
        if (m.hasMatch()) {
            flush();
            cur       = LyricsSection();
            cur.type  = sectionTypeFromString(m.captured(1));
            cur.label = m.captured(1);
            inSection = true;
        } else {
            if (!inSection) {
                cur.type  = SectionType::Verse;
                cur.label = "Verse";
                inSection = true;
            }
            cur.text += line + '\n';
        }
    }
    flush();

    if (song.sections.isEmpty() && !raw.trimmed().isEmpty()) {
        LyricsSection s;
        s.type = SectionType::Verse; s.label = "Verse"; s.text = raw.trimmed();
        song.sections << s;
    }
    return song;
}
