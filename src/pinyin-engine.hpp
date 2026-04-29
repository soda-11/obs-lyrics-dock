#pragma once
// pinyin-engine.hpp — offline CJK → plain ASCII pinyin converter

#include <QString>

namespace PinyinEngine {
    // Convert a multi-line Chinese text to plain ASCII pinyin.
    // One output line per input line; blank lines preserved.
    // No tone marks. Religious proper nouns receive initial caps.
    QString generate(const QString &chineseText);
}
