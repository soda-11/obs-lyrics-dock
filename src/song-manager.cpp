// song-manager.cpp  v10.0
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

// ── Global lines-per-slide (English songs only) ───────────────
static int g_linesPerSlide = 4;
void setGlobalLinesPerSlide(int n) { g_linesPerSlide = qMax(1, n); }
int  getGlobalLinesPerSlide()      { return g_linesPerSlide; }

// ── English slide distribution ────────────────────────────────
// Blank line = forced slide break; within each paragraph lines are
// chunked into groups of lpp (lines-per-slide).
QList<QString> LyricsSection::slides() const
{
    const int lpp = g_linesPerSlide;
    const QStringList allLines = text.split('\n');

    QList<QString> result;
    QStringList    paragraph;

    auto flushParagraph = [&]() {
        if (paragraph.isEmpty()) return;
        for (int i = 0; i < paragraph.size(); i += lpp) {
            QStringList chunk;
            for (int j = i; j < qMin(i + lpp, paragraph.size()); ++j)
                chunk << paragraph[j];
            if (!chunk.isEmpty())
                result << chunk.join('\n');
        }
        paragraph.clear();
    };

    for (const QString &line : allLines) {
        if (line.trimmed().isEmpty())
            flushParagraph();
        else
            paragraph << line;
    }
    flushParagraph();
    return result;
}

// ── Chinese slide distribution ────────────────────────────────
// All three blocks (text / pinyin / english) are split on blank lines.
// Each blank-line-delimited group in `text` becomes one slide.
// Pinyin must have exactly the same number of non-blank lines as text
// (1:1 per line); English may have any number ≥ Chinese line count.
QList<LyricsSlide> LyricsSection::chineseSlides() const
{
    // Split each block into paragraphs (groups separated by blank lines)
    auto toParagraphs = [](const QString &src) -> QList<QStringList> {
        QList<QStringList> paras;
        QStringList cur;
        for (const QString &line : src.split('\n')) {
            if (line.trimmed().isEmpty()) {
                if (!cur.isEmpty()) { paras << cur; cur.clear(); }
            } else {
                cur << line;
            }
        }
        if (!cur.isEmpty()) paras << cur;
        return paras;
    };

    QList<QStringList> chParas = toParagraphs(text);
    QList<QStringList> pyParas = showPinyin      ? toParagraphs(pinyin)  : QList<QStringList>();
    QList<QStringList> enParas = showTranslation ? toParagraphs(english) : QList<QStringList>();

    QString lbl = label.isEmpty() ? sectionTypeLabel(type) : label;

    QList<LyricsSlide> result;
    for (int i = 0; i < chParas.size(); ++i) {
        LyricsSlide sl;
        sl.isChinese = true;
        sl.section   = lbl;
        sl.text      = chParas[i].join('\n');
        sl.pinyin    = (showPinyin      && i < pyParas.size()) ? pyParas[i].join('\n') : QString();
        sl.english   = (showTranslation && i < enParas.size()) ? enParas[i].join('\n') : QString();
        result << sl;
    }
    return result;
}

QJsonObject LyricsSection::toJson() const
{
    QJsonObject o;
    o["type"]            = sectionTypeLabel(type);
    o["label"]           = label;
    o["text"]            = text;
    o["isChinese"]       = isChinese;
    o["showPinyin"]      = showPinyin;
    o["showTranslation"] = showTranslation;
    if (!pinyin.isEmpty())         o["pinyin"]         = pinyin;
    if (!english.isEmpty())        o["english"]        = english;
    if (!backgroundPath.isEmpty()) o["backgroundPath"] = backgroundPath;
    return o;
}

LyricsSection LyricsSection::fromJson(const QJsonObject &o)
{
    LyricsSection s;
    s.type            = sectionTypeFromString(o["type"].toString());
    s.label           = o["label"].toString();
    s.text            = o["text"].toString();
    s.isChinese       = o["isChinese"].toBool(false);
    s.showPinyin      = o["showPinyin"].toBool(false);
    s.showTranslation = o["showTranslation"].toBool(false);
    s.pinyin          = o["pinyin"].toString();
    s.english         = o["english"].toString();
    s.backgroundPath  = o["backgroundPath"].toString();
    return s;
}

QList<LyricsSlide> Song::allSlides() const
{
    QList<LyricsSlide> result;
    for (const auto &sec : sections) {
        // Effective background: section override first, then song default
        QString effectiveBg = sec.backgroundPath.isEmpty() ? backgroundPath : sec.backgroundPath;
        if (sec.isChinese) {
            QList<LyricsSlide> slides = sec.chineseSlides();
            for (auto &sl : slides)
                sl.backgroundPath = effectiveBg;
            result << slides;
        } else {
            QString lbl = sec.label.isEmpty()
                          ? sectionTypeLabel(sec.type) : sec.label;
            for (const auto &slideText : sec.slides()) {
                LyricsSlide sl;
                sl.section        = lbl;
                sl.text           = slideText;
                sl.backgroundPath = effectiveBg;
                result << sl;
            }
        }
    }
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
    if (!backgroundPath.isEmpty()) o["backgroundPath"] = backgroundPath;
    QJsonArray arr;
    for (const auto &s : sections) arr.append(s.toJson());
    o["sections"]  = arr;
    return o;
}

Song Song::fromJson(const QJsonObject &o)
{
    Song s;
    s.id             = o["id"].toString();
    s.title          = o["title"].toString();
    s.artist         = o["artist"].toString();
    s.key            = o["key"].toString();
    s.tempo          = o["tempo"].toString();
    s.copyright      = o["copyright"].toString();
    s.backgroundPath = o["backgroundPath"].toString();
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
