// pinyin-engine.cpp — offline CJK → plain ASCII pinyin
// Full coverage: 20,924 characters from Unicode Unihan kMandarin field.
// No tone diacritics. Direct O(1) index lookup via pinyin-table.hpp.
// Religious proper nouns receive initial caps.
#include "pinyin-engine.hpp"
#include "pinyin-table.hpp"
#include <QString>
#include <QStringList>
#include <QChar>

namespace {

// Override table: pinyin that differs from Unihan OR needs initial cap.
// Checked before the Unihan table. cap=true → first letter uppercased.
struct Override { uint32_t cp; const char *py; bool cap; };
static const Override kOverrides[] = {
    {0x534E, "hua",   true},  // 华 → Hua   (耶和华)
    {0x548C, "he",    true},  // 和 → He    (耶和华)
    {0x5723, "sheng", true},  // 圣 → Sheng
    {0x7962, "ni",    true},  // 祢 → Ni    (worship "You"; Unihan gives "mi")
    {0x795E, "shen",  true},  // 神 → Shen
    {0x7A63, "su",    true},  // 稣 → Su    (耶稣; Unihan gives "rang")
    {0x8036, "ye",    true},  // 耶 → Ye
};
static constexpr int kOverrideSize = static_cast<int>(sizeof(kOverrides) / sizeof(kOverrides[0]));

// Returns override entry or nullptr.
static const Override *findOverride(uint32_t cp)
{
    for (int i = 0; i < kOverrideSize; ++i)
        if (kOverrides[i].cp == cp) return &kOverrides[i];
    return nullptr;
}

// Returns Unihan pinyin (no override, no cap).
static const char *unihanPinyin(uint32_t cp)
{
    if (cp < 0x4E00 || cp > 0x9FFF) return nullptr;
    return kPinyinTable[cp - 0x4E00];
}

static bool isCJKPunctuation(uint32_t cp)
{
    if (cp >= 0x3000 && cp <= 0x303F) return true; // CJK Symbols & Punctuation
    if (cp >= 0xFF00 && cp <= 0xFF0F) return true; // Fullwidth ASCII punctuation
    if (cp >= 0xFE30 && cp <= 0xFE4F) return true; // CJK Compatibility Forms
    switch (cp) {
        case 0x2014: case 0x2015: // em dash
        case 0x2018: case 0x2019: // smart single quotes
        case 0x201C: case 0x201D: // smart double quotes
        case 0x2026:              // ellipsis
        case 0xFF01: case 0xFF0C: case 0xFF1A: case 0xFF1B:
        case 0xFF1F: case 0xFF08: case 0xFF09:
            return true;
        default: return false;
    }
}

} // namespace

namespace PinyinEngine {

QString generate(const QString &chineseText)
{
    QStringList outLines;
    for (const QString &line : chineseText.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty()) {
            outLines << QString();
            continue;
        }
        QStringList tokens;
        for (QChar ch : line) {
            if (ch.isSpace()) continue;
            uint32_t cp = static_cast<uint32_t>(ch.unicode());
            if (isCJKPunctuation(cp)) continue;

            const Override *ov = findOverride(cp);
            const char *raw = ov ? ov->py : unihanPinyin(cp);
            if (raw && *raw) {
                QString s = QString::fromLatin1(raw);
                if (ov && ov->cap)
                    s[0] = s[0].toUpper();
                tokens << s;
            } else if (cp < 0x2E80) {
                // ASCII / Latin: pass through (numbers, English, punctuation)
                tokens << QString(ch);
            }
            // Unknown / no-reading CJK: silently skip
        }
        outLines << tokens.join(QLatin1Char(' '));
    }
    return outLines.join(QLatin1Char('\n'));
}

} // namespace PinyinEngine
