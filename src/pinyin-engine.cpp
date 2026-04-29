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

// Override lookup via switch — O(1), compiler-optimised jump table.
// Returns the correct pinyin string (already capitalised where required)
// or nullptr if this character has no override.
//
// Characters covered:
//   华 U+534E → Hua   耶和华
//   和 U+548C → He    耶和华
//   圣 U+5723 → Sheng
//   祢 U+7962 → Ni    worship "You" (Unihan gives "mi")
//   神 U+795E → Shen
//   穌 U+7A4C → Su    traditional 耶穌  (Unihan "su" – correct syllable but needs cap)
//   稣 U+7A63 → Su    simplified  耶稣  (Unihan "rang" – wrong reading entirely)
//   耶 U+8036 → Ye
static const char *overridePinyin(uint32_t cp)
{
    switch (cp) {
        case 0x534E: return "Hua";
        case 0x548C: return "He";
        case 0x5723: return "Sheng";
        case 0x7962: return "Ni";
        case 0x795E: return "Shen";
        case 0x7A23: return "Su";   // 稣 variant U+7A23 — Unihan "su" lacks capital
        case 0x7A4C: return "Su";   // 穌 traditional  — Unihan "su" lacks capital
        case 0x7A63: return "Su";   // 稣 simplified   — Unihan "rang" is wrong
        case 0x8036: return "Ye";
        default:     return nullptr;
    }
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

            // Override table is checked first (switch → jump table, no linear scan).
            // Overridden strings already carry the correct capitalisation.
            const char *raw = overridePinyin(cp);
            if (!raw) raw = unihanPinyin(cp);   // fallback: Unihan (lowercase)

            if (raw && *raw) {
                tokens << QString::fromLatin1(raw);
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
