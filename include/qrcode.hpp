// qrcode.hpp - Minimal QR Code encoder for Nintendo Switch console output
// Supports Version 1-10, ECC Level L, Byte mode
// Renders using UTF-8 half-block characters for compact display
//
// Algorithm based on the QR code specification ISO/IEC 18004

#pragma once
#include <string>
#include <vector>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// ── GF(256) arithmetic ────────────────────────────────────────────────────────
static const uint8_t GF_EXP[512] = {
    1,2,4,8,16,32,64,128,29,58,116,232,205,135,19,38,76,152,45,90,180,117,234,201,143,3,6,12,24,48,96,192,
    157,39,78,156,37,74,148,53,106,212,181,119,238,193,159,35,70,140,5,10,20,40,80,160,93,186,105,210,185,111,
    222,161,95,190,97,194,153,47,94,188,101,202,137,15,30,60,120,240,253,231,211,187,107,214,177,127,254,225,
    223,163,91,182,113,226,217,175,67,134,17,34,68,136,13,26,52,104,208,189,103,206,129,31,62,124,248,237,199,
    147,59,118,236,197,151,51,102,204,133,23,46,92,184,109,218,169,79,158,33,66,132,21,42,84,168,77,154,41,82,
    164,85,170,73,146,57,114,228,213,183,115,230,209,191,99,198,145,63,126,252,229,215,179,123,246,241,255,227,
    219,171,75,150,49,98,196,149,55,110,220,165,87,174,65,130,25,50,100,200,141,7,14,28,56,112,224,221,167,83,
    166,81,162,89,178,121,242,249,239,195,155,43,86,172,69,138,9,18,36,72,144,61,122,244,245,247,243,251,235,
    203,139,11,22,44,88,176,125,250,233,207,131,27,54,108,216,173,71,142,
    // repeat for exp table
    1,2,4,8,16,32,64,128,29,58,116,232,205,135,19,38,76,152,45,90,180,117,234,201,143,3,6,12,24,48,96,192,
    157,39,78,156,37,74,148,53,106,212,181,119,238,193,159,35,70,140,5,10,20,40,80,160,93,186,105,210,185,111,
    222,161,95,190,97,194,153,47,94,188,101,202,137,15,30,60,120,240,253,231,211,187,107,214,177,127,254,225,
    223,163,91,182,113,226,217,175,67,134,17,34,68,136,13,26,52,104,208,189,103,206,129,31,62,124,248,237,199,
    147,59,118,236,197,151,51,102,204,133,23,46,92,184,109,218,169,79,158,33,66,132,21,42,84,168,77,154,41,82,
    164,85,170,73,146,57,114,228,213,183,115,230,209,191,99,198,145,63,126,252,229,215,179,123,246,241,255,227,
    219,171,75,150,49,98,196,149,55,110,220,165,87,174,65,130,25,50,100,200,141,7,14,28,56,112,224,221,167,83,
    166,81,162,89,178,121,242,249,239,195,155,43,86,172,69,138,9,18,36,72,144,61,122,244,245,247,243,251,235,
    203,139,11,22,44,88,176,125,250,233,207,131,27,54,108,216,173,71,142
};
static const uint8_t GF_LOG[256] = {
    0,0,1,25,2,50,26,198,3,223,51,238,27,104,199,75,4,100,224,14,52,141,239,129,28,193,105,248,200,8,76,113,
    5,138,101,47,225,36,15,33,53,147,142,218,240,18,130,69,29,181,194,125,106,39,249,185,201,154,9,120,77,228,
    114,166,6,191,139,98,102,221,48,253,226,152,37,179,16,145,34,136,54,208,148,206,143,150,219,189,241,210,
    19,92,131,56,70,64,30,66,182,163,195,72,126,110,107,58,40,84,250,133,186,61,202,94,155,159,10,21,121,43,
    78,212,229,172,115,243,167,87,7,112,192,247,140,128,99,13,103,74,222,237,49,197,254,24,227,165,153,119,
    38,184,180,124,17,68,146,217,35,32,137,46,55,63,209,91,149,188,207,205,144,135,151,178,220,252,190,97,
    242,86,211,171,20,42,93,158,132,60,57,83,71,109,65,162,31,45,67,216,183,123,164,118,196,23,73,236,127,12,
    111,246,108,161,59,82,41,157,85,170,251,96,134,177,187,204,62,90,203,89,95,176,156,169,160,81,11,245,22,
    235,122,117,44,215,79,174,213,233,230,231,173,232,116,214,244,234,168,80,88,175
};

static uint8_t gfMul(uint8_t a, uint8_t b) {
    if (a == 0 || b == 0) return 0;
    return GF_EXP[(GF_LOG[a] + GF_LOG[b]) % 255];
}

// ── Reed-Solomon ──────────────────────────────────────────────────────────────
// Generator polynomials for various EC codeword counts (ECC-L)
// We precompute for the sizes we need

static std::vector<uint8_t> rsGenerator(int degree) {
    std::vector<uint8_t> g = {1};
    for (int i = 0; i < degree; i++) {
        std::vector<uint8_t> t(g.size() + 1, 0);
        uint8_t coeff = GF_EXP[i];
        for (int j = 0; j < (int)g.size(); j++) {
            t[j] ^= gfMul(g[j], coeff);
            t[j+1] ^= g[j];
        }
        g = t;
    }
    return g;
}

static std::vector<uint8_t> rsDivide(const std::vector<uint8_t>& data, int ecCount) {
    std::vector<uint8_t> gen = rsGenerator(ecCount);
    std::vector<uint8_t> rem(data.begin(), data.end());
    rem.resize(data.size() + ecCount, 0);
    for (int i = 0; i < (int)data.size(); i++) {
        uint8_t f = rem[i];
        if (f != 0) {
            for (int j = 1; j <= ecCount; j++) {
                rem[i+j] ^= gfMul(gen[ecCount-j], f);
            }
        }
    }
    return std::vector<uint8_t>(rem.begin() + data.size(), rem.end());
}

// ── QR version/capacity tables (ECC-L) ───────────────────────────────────────
struct QRVersion {
    int version, size, dataCW, ecCW, remainder;
};

static const QRVersion QR_VERSIONS[] = {
    {1,  21,  19,  7,  0},
    {2,  25,  34, 10,  7},
    {3,  29,  55, 15,  7},
    {4,  33,  80, 20,  7},
    {5,  37, 108, 26,  7},
    {6,  41, 136, 36,  0},
    {7,  45, 156, 40,  0},
    {8,  49, 194, 48,  0},
    {9,  53, 232, 60,  0},
    {10, 57, 274, 72,  0},
};

// ── Bit buffer ────────────────────────────────────────────────────────────────
struct BitBuffer {
    std::vector<uint8_t> data;
    int bits = 0;
    void append(uint32_t val, int n) {
        for (int i = n-1; i >= 0; i--) {
            if (bits % 8 == 0) data.push_back(0);
            if ((val >> i) & 1) data[bits/8] |= 1 << (7 - bits%8);
            bits++;
        }
    }
};

// ── QR matrix ────────────────────────────────────────────────────────────────
class QRMatrix {
public:
    int sz;
    std::vector<uint8_t> modules;   // bit 0 = dark/light, bit 1 = reserved
    QRMatrix(int s) : sz(s), modules(s*s, 0) {}

    void set(int r, int c, bool dark, bool reserved = false) {
        modules[r*sz+c] = (dark ? 1 : 0) | (reserved ? 2 : 0);
    }
    bool isReserved(int r, int c) const { return (modules[r*sz+c] & 2) != 0; }
    bool isDark(int r, int c) const     { return (modules[r*sz+c] & 1) != 0; }

    void setFinderPattern(int tr, int tc) {
        for (int r = -1; r <= 7; r++)
        for (int c = -1; c <= 7; c++) {
            int rr = tr+r, cc = tc+c;
            if (rr<0||rr>=sz||cc<0||cc>=sz) continue;
            bool dark = (r>=0&&r<=6&&(c==0||c==6))||
                        (c>=0&&c<=6&&(r==0||r==6))||
                        (r>=2&&r<=4&&c>=2&&c<=4);
            set(rr, cc, dark, true);
        }
    }

    void setAlignmentPattern(int tr, int tc) {
        for (int r = -2; r <= 2; r++)
        for (int c = -2; c <= 2; c++) {
            bool dark = (r==-2||r==2||c==-2||c==2||(r==0&&c==0));
            set(tr+r, tc+c, dark, true);
        }
    }

    void setTimingPatterns() {
        for (int i = 8; i < sz-8; i++) {
            set(6, i, i%2==0, true);
            set(i, 6, i%2==0, true);
        }
    }

    void setFormatInfo(int mask) {
        // Format info for ECC-L = 0b01, mask appended
        // Precomputed format strings for ECC-L masks 0-7
        static const uint32_t FORMAT[8] = {
            0x77C4, 0x72F3, 0x7DAA, 0x789D, 0x662F, 0x6318, 0x6C41, 0x6976
        };
        uint32_t fmt = FORMAT[mask & 7];
        // Place around top-left finder
        for (int i = 0; i <= 5; i++) set(8, i, (fmt>>i)&1, true);
        set(8, 7, (fmt>>6)&1, true);
        set(8, 8, (fmt>>7)&1, true);
        set(7, 8, (fmt>>8)&1, true);
        for (int i = 9; i <= 14; i++) set(14-i, 8, (fmt>>i)&1, true);
        // Place around top-right and bottom-left finders
        for (int i = 0; i <= 7; i++) set(sz-1-i, 8, (fmt>>i)&1, true);
        for (int i = 8; i <= 14; i++) set(8, sz-15+i, (fmt>>i)&1, true);
        set(sz-8, 8, true, true); // dark module
    }
};

// ── Alignment pattern positions ───────────────────────────────────────────────
static const int ALIGN_POS[][7] = {
    {},                          // v1
    {6, 18},                     // v2
    {6, 22},                     // v3
    {6, 26},                     // v4
    {6, 30},                     // v5
    {6, 34},                     // v6
    {6, 22, 38},                 // v7
    {6, 24, 42},                 // v8
    {6, 26, 46},                 // v9
    {6, 28, 50},                 // v10
};
static const int ALIGN_CNT[] = {0,2,2,2,2,2,2,3,3,3,3};

// ── Main QR encoder ───────────────────────────────────────────────────────────
static bool encodeQR(const std::string& text, QRMatrix& out) {
    int len = (int)text.size();

    // Pick version
    int ver = -1;
    const QRVersion* vinfo = nullptr;
    for (int i = 0; i < 10; i++) {
        // Byte mode: 4 + 8 bits header, then 8*len data bits
        int needed = (4 + 8 + 8*len + 7) / 8;
        if (needed <= QR_VERSIONS[i].dataCW) {
            ver = QR_VERSIONS[i].version;
            vinfo = &QR_VERSIONS[i];
            break;
        }
    }
    if (ver < 0) return false; // too long

    // Build data codewords
    BitBuffer bb;
    bb.append(0b0100, 4);    // byte mode
    bb.append(len, 8);       // character count
    for (char c : text) bb.append((uint8_t)c, 8);
    bb.append(0, 4);         // terminator
    // Pad to byte boundary
    while (bb.bits % 8 != 0) bb.append(0, 1);
    // Pad to dataCW
    bool flip = false;
    while ((int)bb.data.size() < vinfo->dataCW) {
        bb.data.push_back(flip ? 0x11 : 0xEC);
        flip = !flip;
    }

    // Reed-Solomon
    std::vector<uint8_t> ec = rsDivide(bb.data, vinfo->ecCW);

    // Interleave (single block for these versions)
    std::vector<uint8_t> codewords(bb.data.begin(), bb.data.end());
    codewords.insert(codewords.end(), ec.begin(), ec.end());

    // Build matrix
    int sz = vinfo->size;
    QRMatrix qr(sz);

    // Finder patterns
    qr.setFinderPattern(0, 0);
    qr.setFinderPattern(0, sz-7);
    qr.setFinderPattern(sz-7, 0);

    // Timing patterns
    qr.setTimingPatterns();

    // Alignment patterns
    if (ver >= 2) {
        const int* ap = ALIGN_POS[ver-1];
        int ac = ALIGN_CNT[ver];
        for (int i = 0; i < ac; i++)
        for (int j = 0; j < ac; j++) {
            int r = ap[i], c = ap[j];
            // Skip if overlaps with finder
            if ((r <= 8 && c <= 8) || (r <= 8 && c >= sz-8) || (r >= sz-8 && c <= 8))
                continue;
            qr.setAlignmentPattern(r, c);
        }
    }

    // Dark module
    qr.set(sz-8, 8, true, true);

    // Format info placeholder (mask 0)
    qr.setFormatInfo(0);

    // Place data bits using zigzag pattern
    int bitIdx = 0;
    int totalBits = (int)codewords.size() * 8 + vinfo->remainder;
    auto getBit = [&](int idx) -> bool {
        if (idx >= (int)codewords.size()*8) return false;
        return (codewords[idx/8] >> (7 - idx%8)) & 1;
    };

    for (int col = sz-1; col >= 1; col -= 2) {
        if (col == 6) col--; // skip timing column
        for (int row = 0; row < sz; row++) {
            for (int dx = 0; dx <= 1; dx++) {
                int c = col - dx;
                int r = ((col+1)/2 % 2 == 0) ? (sz-1-row) : row;
                if (!qr.isReserved(r, c)) {
                    bool bit = (bitIdx < totalBits) && getBit(bitIdx++);
                    // Apply mask 0: (row+col) % 2 == 0
                    if ((r + c) % 2 == 0) bit = !bit;
                    qr.set(r, c, bit);
                }
            }
        }
    }

    out = qr;
    return true;
}

// ── Console renderer ──────────────────────────────────────────────────────────
// Renders QR code using half-block chars, centered on 80-char console
// Each 2 QR rows → 1 console line using ▀ ▄ █ space
static void printQRToConsole(const std::string& url) {
    QRMatrix qr(21); // placeholder size
    if (!encodeQR(url, qr)) {
        printf("QR: URL too long\n");
        return;
    }

    int sz = qr.sz;
    // Add 2-module quiet zone (print as spaces)
    int border = 2;
    int totalRows = sz + border * 2;
    int totalCols = sz + border * 2;
    int padLeft = (80 - totalCols) / 2;
    if (padLeft < 0) padLeft = 0;

    auto getModule = [&](int r, int c) -> bool {
        r -= border; c -= border;
        if (r < 0 || r >= sz || c < 0 || c >= sz) return false;
        return qr.isDark(r, c);
    };

    // Print 2 rows per line using half-block chars
    for (int row = 0; row < totalRows; row += 2) {
        for (int p = 0; p < padLeft; p++) putchar(' ');
        for (int col = 0; col < totalCols; col++) {
            bool top    = getModule(row,   col);
            bool bottom = (row+1 < totalRows) ? getModule(row+1, col) : false;
            if (top && bottom)       printf("\xe2\x96\x88"); // █ full block
            else if (top)            printf("\xe2\x96\x80"); // ▀ upper half
            else if (bottom)         printf("\xe2\x96\x84"); // ▄ lower half
            else                     printf(" ");
        }
        printf("\n");
    }
}
