#include "thumb.hpp"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>

// MP4 files store a cover image inside a 'covr' box inside udta/meta/ilst
// The Switch stores screenshots as the poster frame in this box as JPEG
// Box structure: [4 bytes size][4 bytes type][...data...]

static uint32_t readU32BE(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

// Search for a box type within a buffer, returns offset of box data (after header)
// Returns -1 if not found
static int findBox(const uint8_t* buf, int bufLen, const char* type, int* dataLen) {
    int pos = 0;
    while (pos + 8 <= bufLen) {
        uint32_t size = readU32BE(buf + pos);
        if (size < 8 || (int)(pos + size) > bufLen) break;
        if (memcmp(buf + pos + 4, type, 4) == 0) {
            *dataLen = (int)size - 8;
            return pos + 8;
        }
        pos += size;
    }
    return -1;
}

// Extract cover JPEG from MP4 file
// Returns JPEG bytes if found, empty vector otherwise
std::vector<uint8_t> extractVideoThumbnail(const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) return {};

    // Read first 2MB - enough to find metadata boxes
    const int READ_SIZE = 2 * 1024 * 1024;
    std::vector<uint8_t> buf(READ_SIZE);
    int bytesRead = (int)fread(buf.data(), 1, READ_SIZE, f);
    fclose(f);
    if (bytesRead < 16) return {};
    buf.resize(bytesRead);

    const uint8_t* data = buf.data();
    int len = bytesRead;

    // Find 'moov' box
    int moovLen = 0;
    int moovOff = findBox(data, len, "moov", &moovLen);
    if (moovOff < 0) return {};

    // Find 'udta' inside moov
    int udtaLen = 0;
    int udtaOff = findBox(data + moovOff, moovLen, "udta", &udtaLen);
    if (udtaOff < 0) return {};
    udtaOff += moovOff;

    // Find 'meta' inside udta
    int metaLen = 0;
    int metaOff = findBox(data + udtaOff, udtaLen, "meta", &metaLen);
    if (metaOff < 0) return {};
    metaOff += udtaOff;
    // meta has a 4-byte version/flags field before its children
    if (metaLen < 4) return {};
    metaOff += 4;
    metaLen -= 4;

    // Find 'ilst' inside meta
    int ilstLen = 0;
    int ilstOff = findBox(data + metaOff, metaLen, "ilst", &ilstLen);
    if (ilstOff < 0) return {};
    ilstOff += metaOff;

    // Find 'covr' inside ilst
    int covrLen = 0;
    int covrOff = findBox(data + ilstOff, ilstLen, "covr", &covrLen);
    if (covrOff < 0) return {};
    covrOff += ilstOff;

    // Inside covr: one or more item boxes, find 'data'
    int dataBoxLen = 0;
    int dataBoxOff = findBox(data + covrOff, covrLen, "data", &dataBoxLen);
    if (dataBoxOff < 0) return {};
    dataBoxOff += covrOff;

    // 'data' box: 4 bytes type indicator + 4 bytes locale = 8 bytes before actual image
    if (dataBoxLen < 8) return {};
    dataBoxOff += 8;
    dataBoxLen -= 8;

    if (dataBoxLen <= 0) return {};

    // Check it's a JPEG (FF D8) or PNG (89 50)
    if (dataBoxOff + 2 > len) return {};
    uint8_t b0 = data[dataBoxOff];
    uint8_t b1 = data[dataBoxOff + 1];
    if (!((b0 == 0xFF && b1 == 0xD8) || (b0 == 0x89 && b1 == 0x50))) return {};

    return std::vector<uint8_t>(data + dataBoxOff, data + dataBoxOff + dataBoxLen);
}
