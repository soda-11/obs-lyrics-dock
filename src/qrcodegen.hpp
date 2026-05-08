#pragma once
// qrcodegen.hpp — single-header QR code generator  v1.0
// Byte mode, ECC level M, QR versions 1–10
// Usage: auto grid = qrcodegen::encode("http://...");
//        grid[row][col] == true → dark module
//
// Algorithm follows ISO/IEC 18004:2015 and Nayuki's reference logic.

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace qrcodegen {

// ══════════════════════════════════════════════════════════════════
// GF(256) arithmetic — primitive polynomial 0x11D
// ══════════════════════════════════════════════════════════════════
namespace gf {
    static uint8_t EXP[512];
    static uint8_t LOG[256];
    static bool    s_init = false;
    static void init() {
        if (s_init) return; s_init = true;
        uint32_t x = 1;
        for (int i = 0; i < 255; i++) {
            EXP[i] = (uint8_t)x;
            LOG[x]  = (uint8_t)i;
            x <<= 1;
            if (x & 256) x ^= 0x11D;
        }
        for (int i = 255; i < 512; i++) EXP[i] = EXP[i - 255];
    }
    static uint8_t mul(uint8_t a, uint8_t b) {
        init();
        if (!a || !b) return 0;
        return EXP[(int)LOG[a] + (int)LOG[b]];
    }
} // namespace gf

// ══════════════════════════════════════════════════════════════════
// Reed-Solomon error correction
// ══════════════════════════════════════════════════════════════════

// Build generator polynomial of given degree.
// Result[i] is the coefficient of x^i in the monic divisor (leading 1 omitted).
// Stored lowest-degree first.
static std::vector<uint8_t> rsGenPoly(int degree)
{
    gf::init();
    // g(x) = product_{i=0}^{degree-1} (x - alpha^i)
    // We build it iteratively as an array of coefficients high→low (excluding leading).
    std::vector<uint8_t> g(degree, 0);
    g[degree - 1] = 1; // g = x^(degree-1)
    uint8_t root = 1;  // alpha^0 = 1
    for (int i = 0; i < degree; i++) {
        // Multiply g(x) by (x - root)
        for (int j = 0; j < degree; j++) {
            g[j] = gf::mul(g[j], root);
            if (j + 1 < degree) g[j] ^= g[j + 1];
        }
        root = gf::mul(root, 2); // root = alpha^(i+1)
    }
    return g; // g[0] is the coefficient of x^0 in the actual polynomial
}

// Compute numEcc RS remainder bytes for the given data.
static std::vector<uint8_t> rsEncode(const std::vector<uint8_t>& data, int numEcc)
{
    auto gen = rsGenPoly(numEcc);
    std::vector<uint8_t> rem(numEcc, 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ rem[0];
        // Shift rem left and subtract factor * gen
        for (int i = 0; i + 1 < numEcc; i++)
            rem[i] = rem[i + 1] ^ gf::mul(gen[i], factor);
        rem[numEcc - 1] = gf::mul(gen[numEcc - 1], factor);
    }
    return rem;
}

// ══════════════════════════════════════════════════════════════════
// Version / block capacity tables (ECC level M)
// ══════════════════════════════════════════════════════════════════
struct VersionInfo {
    int totalData;  // total data codewords
    int ecPerBlock; // ECC codewords per block
    int nb1, db1;   // group 1: nb1 blocks of db1 data codewords
    int nb2, db2;   // group 2: nb2 blocks of db2 (0 if unused)
};

// Index by version (1-based); [0] unused.
static const VersionInfo VI[11] = {
    {  0,  0, 0,  0, 0,  0}, // [0] unused
    { 16, 10, 1, 16, 0,  0}, // v1
    { 28, 16, 1, 28, 0,  0}, // v2
    { 44, 26, 1, 44, 0,  0}, // v3
    { 64, 18, 2, 32, 0,  0}, // v4
    { 86, 24, 2, 43, 0,  0}, // v5
    {108, 16, 4, 27, 0,  0}, // v6
    {124, 18, 4, 31, 0,  0}, // v7
    {154, 22, 2, 38, 2, 39}, // v8
    {182, 22, 3, 36, 2, 37}, // v9
    {216, 26, 4, 43, 1, 44}, // v10
};

// Remainder bits appended after placing all codewords (versions 1–10).
static const int REMAINDER_BITS[11] = {0, 0, 7, 7, 7, 7, 7, 0, 0, 0, 0};

// Alignment pattern center coordinates for versions 1–10.
static std::vector<int> alignPos(int v) {
    static const std::vector<int> T[11] = {
        {},          // 0 unused
        {},          // 1
        {6,18},      // 2
        {6,22},      // 3
        {6,26},      // 4
        {6,30},      // 5
        {6,34},      // 6
        {6,22,38},   // 7
        {6,24,42},   // 8
        {6,26,46},   // 9
        {6,28,50},   // 10
    };
    return (v >= 1 && v <= 10) ? T[v] : std::vector<int>{};
}

// ══════════════════════════════════════════════════════════════════
// Select minimum version for a given byte-mode payload at ECC M
// ══════════════════════════════════════════════════════════════════
static int selectVersion(int byteLen)
{
    for (int v = 1; v <= 10; v++) {
        // Header bits: 4 (mode indicator) + 8 (char count, v1-9) or 16 (v10)
        int headerBits = (v <= 9) ? 12 : 20;
        int capacity   = VI[v].totalData * 8;
        if (headerBits + byteLen * 8 <= capacity) return v;
    }
    return -1;
}

// ══════════════════════════════════════════════════════════════════
// QrCode builder
// ══════════════════════════════════════════════════════════════════
class QrCode {
public:
    // Encode a UTF-8 / byte string to a QR code.
    static QrCode encode(const std::string& text)
    {
        int v = selectVersion((int)text.size());
        if (v < 1) throw std::length_error("Text too long for QR v1-10 ECC M");
        return QrCode(v, text);
    }

    int version()  const { return m_ver; }
    int size()     const { return m_sz; }
    bool dark(int r, int c) const {
        return (r >= 0 && r < m_sz && c >= 0 && c < m_sz)
               && m_mod[r * m_sz + c];
    }

    // Return full 2-D grid (row-major, true = dark).
    std::vector<std::vector<bool>> matrix() const {
        std::vector<std::vector<bool>> g(m_sz, std::vector<bool>(m_sz));
        for (int r = 0; r < m_sz; r++)
            for (int c = 0; c < m_sz; c++)
                g[r][c] = m_mod[r * m_sz + c];
        return g;
    }

private:
    int  m_ver, m_sz;
    std::vector<bool> m_mod;  // true = dark
    std::vector<bool> m_func; // true = function pattern (not data)

    QrCode(int ver, const std::string& text)
        : m_ver(ver), m_sz(ver * 4 + 17)
        , m_mod(m_sz * m_sz, false)
        , m_func(m_sz * m_sz, false)
    {
        drawFunctionPatterns();
        auto cw = buildCodewords(text);
        chooseMaskAndPlace(cw);
    }

    // ── Module access ──────────────────────────────────────────────
    void setFunc(int r, int c, bool dark) {
        m_mod [r * m_sz + c] = dark;
        m_func[r * m_sz + c] = true;
    }
    void setData(int r, int c, bool dark) {
        m_mod[r * m_sz + c] = dark;
    }
    bool isFunc(int r, int c) const {
        return (r >= 0 && r < m_sz && c >= 0 && c < m_sz)
               && m_func[r * m_sz + c];
    }

    // ── Function patterns ──────────────────────────────────────────

    void drawFunctionPatterns() {
        // Finder patterns (top-left, top-right, bottom-left)
        drawFinder(3, 3);
        drawFinder(3, m_sz - 4);
        drawFinder(m_sz - 4, 3);

        // Timing patterns — guard with isFunc so nothing overwrites
        // a finder or separator module if geometry ever shifts.
        for (int i = 0; i < m_sz; i++) {
            if (!isFunc(6, i)) setFunc(6, i, i % 2 == 0);
            if (!isFunc(i, 6)) setFunc(i, 6, i % 2 == 0);
        }

        // Alignment patterns — skip any center on timing row 6 or timing col 6.
        // This covers all three finder-corner overlaps and any timing-strip
        // overlap (e.g. centre (6,22) in v7) per ISO 18004 §7.3.5.
        auto pos = alignPos(m_ver);
        for (int r : pos) {
            for (int c : pos) {
                if (r == 6 || c == 6) continue;
                drawAlignment(r, c);
            }
        }

        // Dark module
        setFunc(m_sz - 8, 8, true);

        // Reserve format-info areas (will be filled per mask)
        reserveFormat();
    }

    // Draw 7×7 finder + 1-wide white separator centred at (cr, cc)
    void drawFinder(int cr, int cc) {
        for (int dr = -4; dr <= 4; dr++) {
            for (int dc = -4; dc <= 4; dc++) {
                int r = cr + dr, c = cc + dc;
                if (r < 0 || r >= m_sz || c < 0 || c >= m_sz) continue;
                int a = std::abs(dr), b = std::abs(dc);
                bool dark;
                if (a == 4 || b == 4)    dark = false; // separator (white)
                else if (a == 3 || b == 3) dark = true;  // outer ring
                else if (a == 2 || b == 2) dark = false; // inner white
                else                       dark = true;   // centre 3×3
                setFunc(r, c, dark);
            }
        }
    }

    // Draw 5×5 alignment pattern centred at (cr, cc)
    void drawAlignment(int cr, int cc) {
        for (int dr = -2; dr <= 2; dr++) {
            for (int dc = -2; dc <= 2; dc++) {
                int a = std::abs(dr), b = std::abs(dc);
                bool dark = (a == 2 || b == 2 || (a == 0 && b == 0));
                setFunc(cr + dr, cc + dc, dark);
            }
        }
    }

    // Mark the format-info areas as function (placeholder, actual bits later).
    // Skip positions already marked function (timing modules at row/col 6).
    void reserveFormat() {
        for (int i = 0; i <= 8; i++) {
            if (!m_func[8 * m_sz + i]) setFunc(8, i, false); // row 8, skip col 6 timing
            if (!m_func[i * m_sz + 8]) setFunc(i, 8, false); // col 8, skip row 6 timing
        }
        // Copy 2 horizontal: row 8, cols sz-8..sz-1 (8 modules, bits 0-7)
        for (int i = m_sz - 8; i < m_sz; i++) setFunc(8, i, false);
        // Copy 2 vertical: col 8, rows sz-7..sz-1 (7 modules, bits 8-14)
        // Dark module at (sz-8, 8) is set above in drawFunctionPatterns
        for (int i = m_sz - 7; i < m_sz; i++) setFunc(i, 8, false);
    }

    // Write 15-bit format info (ECC level M + mask)
    void writeFormatInfo(int mask) {
        // 2-bit ECC indicator for M = 0b00, 3-bit mask
        uint32_t data5 = (uint32_t)mask; // 0b00_mmm
        // BCH(15,5) generator: x^10+x^8+x^5+x^4+x^2+x+1 = 0x537
        uint32_t rem = data5 << 10;
        for (int i = 14; i >= 10; i--)
            if ((rem >> i) & 1u) rem ^= (0x537u << (i - 10));
        uint32_t fmt = ((data5 << 10) | rem) ^ 0x5412u; // XOR mask string

        // Copy 1 (around top-left finder) — per ISO 18004 and verified against segno
        // Horizontal row 8: bit 14 (MSB) at col 0, bit 9 at col 5, bit 8 at col 7,
        //   bit 7 at col 8 (col 6 is timing and skipped by reserveFormat guard)
        for (int i = 0; i <= 5; i++) setFunc(8, i, (fmt >> (14 - i)) & 1u);
        setFunc(8, 7, (fmt >> 8) & 1u);
        setFunc(8, 8, (fmt >> 7) & 1u);
        // Vertical col 8: bit 6 at row 7 (row 6 = timing), bits 5..0 at rows 5..0
        setFunc(7, 8, (fmt >> 6) & 1u);
        for (int j = 0; j <= 5; j++) setFunc(j, 8, (fmt >> j) & 1u);

        // Copy 2 (top-right + bottom-left) — per ISO 18004 and verified against segno
        // Horizontal (top-right): row 8, bit 0 at col sz-1, bit 7 at col sz-8
        for (int i = 0; i <= 7; i++) setFunc(8, m_sz - 1 - i, (fmt >> i) & 1u);
        // Dark module (always dark — at row sz-8, col 8)
        setFunc(m_sz - 8, 8, true);
        // Vertical (bottom-left): col 8, bit 8 at row sz-7, bit 14 at row sz-1
        for (int i = 8; i <= 14; i++) setFunc(m_sz - 15 + i, 8, (fmt >> i) & 1u);
    }

    // ── Data encoding ──────────────────────────────────────────────

    std::vector<uint8_t> buildCodewords(const std::string& text) const {
        const VersionInfo& vi = VI[m_ver];
        int totalData = vi.totalData;

        // Bit stream
        std::vector<bool> bits;
        // Mode indicator: byte = 0100
        bits.push_back(false); bits.push_back(true);
        bits.push_back(false); bits.push_back(false);
        // Character count (8 bits for v1-9, 16 for v10)
        int ccLen = (m_ver <= 9) ? 8 : 16;
        int n = (int)text.size();
        for (int i = ccLen - 1; i >= 0; i--) bits.push_back((n >> i) & 1);
        // Data bytes
        for (uint8_t b : text) for (int i = 7; i >= 0; i--) bits.push_back((b >> i) & 1);
        // Terminator (up to 4 zeros)
        for (int i = 0; i < 4 && (int)bits.size() < totalData * 8; i++) bits.push_back(false);
        // Byte-align
        while (bits.size() % 8) bits.push_back(false);
        // Pad codewords
        bool toggle = true;
        while ((int)bits.size() < totalData * 8) {
            uint8_t pad = toggle ? 0xEC : 0x11;
            for (int i = 7; i >= 0; i--) bits.push_back((pad >> i) & 1);
            toggle = !toggle;
        }

        // Bits → bytes
        std::vector<uint8_t> raw(totalData);
        for (int i = 0; i < totalData; i++) {
            uint8_t b = 0;
            for (int j = 0; j < 8; j++) b = (uint8_t)((b << 1) | (bits[i*8+j] ? 1 : 0));
            raw[i] = b;
        }

        // Split into blocks, apply RS
        std::vector<std::vector<uint8_t>> dblk, eblk;
        int off = 0;
        for (int g = 0; g < 2; g++) {
            int nb = (g == 0) ? vi.nb1 : vi.nb2;
            int db = (g == 0) ? vi.db1 : vi.db2;
            if (!nb) continue;
            for (int b = 0; b < nb; b++) {
                std::vector<uint8_t> blk(raw.begin()+off, raw.begin()+off+db);
                dblk.push_back(blk);
                eblk.push_back(rsEncode(blk, vi.ecPerBlock));
                off += db;
            }
        }

        // Interleave data
        std::vector<uint8_t> out;
        int maxD = 0; for (auto& b : dblk) maxD = std::max(maxD, (int)b.size());
        for (int i = 0; i < maxD; i++) for (auto& b : dblk) if (i < (int)b.size()) out.push_back(b[i]);
        // Interleave ECC
        int maxE = vi.ecPerBlock;
        for (int i = 0; i < maxE; i++) for (auto& b : eblk) if (i < (int)b.size()) out.push_back(b[i]);

        return out;
    }

    // ── Data placement ─────────────────────────────────────────────

    // Place codewords in zigzag order
    void placeCodewords(const std::vector<uint8_t>& cw) {
        // Convert to bit stream
        std::vector<bool> bits;
        for (uint8_t b : cw) for (int i = 7; i >= 0; i--) bits.push_back((b >> i) & 1);
        // Append remainder bits
        for (int i = 0; i < REMAINDER_BITS[m_ver]; i++) bits.push_back(false);

        int bitIdx = 0, dir = -1;
        for (int right = m_sz - 1; right >= 1; right -= 2) {
            if (right == 6) right = 5; // skip timing column
            for (int count = 0; count < m_sz; count++) {
                int row = (dir == -1) ? (m_sz - 1 - count) : count;
                for (int dx : {0, 1}) {
                    int col = right - dx;
                    if (!isFunc(row, col)) {
                        bool bit = (bitIdx < (int)bits.size()) ? bits[bitIdx++] : false;
                        setData(row, col, bit);
                    }
                }
            }
            dir = -dir;
        }
    }

    // Apply mask pattern to data modules only
    void applyMask(int mask) {
        for (int r = 0; r < m_sz; r++) {
            for (int c = 0; c < m_sz; c++) {
                if (isFunc(r, c)) continue;
                bool inv = false;
                switch (mask) {
                    case 0: inv = (r + c) % 2 == 0; break;
                    case 1: inv = r % 2 == 0; break;
                    case 2: inv = c % 3 == 0; break;
                    case 3: inv = (r + c) % 3 == 0; break;
                    case 4: inv = (r/2 + c/3) % 2 == 0; break;
                    case 5: inv = (r*c) % 2 + (r*c) % 3 == 0; break;
                    case 6: inv = ((r*c)%2 + (r*c)%3) % 2 == 0; break;
                    case 7: inv = ((r+c)%2 + (r*c)%3) % 2 == 0; break;
                }
                if (inv) m_mod[r*m_sz+c] = !m_mod[r*m_sz+c];
            }
        }
    }

    // ── Penalty evaluation ─────────────────────────────────────────

    long penalty() const {
        long p = 0;

        // Rule 1: 5+ consecutive same-colour
        for (int r = 0; r < m_sz; r++) {
            int run = 1;
            for (int c = 1; c < m_sz; c++) {
                if (m_mod[r*m_sz+c] == m_mod[r*m_sz+c-1]) { run++; if (run==5) p+=3; else if (run>5) p++; }
                else run = 1;
            }
        }
        for (int c = 0; c < m_sz; c++) {
            int run = 1;
            for (int r = 1; r < m_sz; r++) {
                if (m_mod[r*m_sz+c] == m_mod[(r-1)*m_sz+c]) { run++; if (run==5) p+=3; else if (run>5) p++; }
                else run = 1;
            }
        }

        // Rule 2: 2×2 same-colour blocks
        for (int r = 0; r < m_sz-1; r++)
            for (int c = 0; c < m_sz-1; c++) {
                bool v = m_mod[r*m_sz+c];
                if (v==m_mod[r*m_sz+c+1] && v==m_mod[(r+1)*m_sz+c] && v==m_mod[(r+1)*m_sz+c+1]) p+=3;
            }

        // Rule 3: finder-like patterns
        static const bool P1[11]={true,false,true,true,true,false,true,false,false,false,false};
        static const bool P2[11]={false,false,false,false,true,false,true,true,true,false,true};
        for (int r = 0; r < m_sz; r++)
            for (int c = 0; c <= m_sz-11; c++) {
                bool m1=true, m2=true;
                for (int i=0; i<11; i++) { if(m_mod[r*m_sz+c+i]!=P1[i]) m1=false; if(m_mod[r*m_sz+c+i]!=P2[i]) m2=false; }
                if (m1||m2) p+=40;
            }
        for (int c = 0; c < m_sz; c++)
            for (int r = 0; r <= m_sz-11; r++) {
                bool m1=true, m2=true;
                for (int i=0; i<11; i++) { if(m_mod[(r+i)*m_sz+c]!=P1[i]) m1=false; if(m_mod[(r+i)*m_sz+c]!=P2[i]) m2=false; }
                if (m1||m2) p+=40;
            }

        // Rule 4: dark/light balance
        int dark=0; for (bool b : m_mod) if (b) dark++;
        double ratio = (double)dark / (m_sz*m_sz);
        p += (long)(std::abs(ratio*100.0 - 50.0) / 5.0) * 10;

        return p;
    }

    // ── Choose best mask, write final matrix ───────────────────────

    void chooseMaskAndPlace(const std::vector<uint8_t>& cw) {
        int   bestMask    = 0;
        long  bestPenalty = LONG_MAX;

        for (int mask = 0; mask < 8; mask++) {
            writeFormatInfo(mask);
            placeCodewords(cw);
            applyMask(mask);
            long pen = penalty();
            if (pen < bestPenalty) { bestPenalty = pen; bestMask = mask; }
            // Reset data modules
            for (int i = 0; i < m_sz*m_sz; i++) if (!m_func[i]) m_mod[i] = false;
        }

        // Permanently apply best mask
        writeFormatInfo(bestMask);
        placeCodewords(cw);
        applyMask(bestMask);
    }
};

// ── Convenience free function ──────────────────────────────────────
inline std::vector<std::vector<bool>> encode(const std::string& text)
{
    return QrCode::encode(text).matrix();
}

} // namespace qrcodegen
