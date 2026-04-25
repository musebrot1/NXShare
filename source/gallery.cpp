#include "gallery.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <sstream>
#include <map>
#include <set>

Gallery::Gallery() {}
Gallery::~Gallery() {}

void Gallery::scan() {
    m_files.clear();

    // Initialize capsa (capture service)
    capsaInitialize();

    scanStorage(CapsAlbumStorage_Sd);
    scanStorage(CapsAlbumStorage_Nand);

    capsaExit();

    // Resolve game names via NS service
    resolveGameNames();

    // Sort newest first
    std::sort(m_files.begin(), m_files.end(), [](const MediaFile& a, const MediaFile& b) {
        return a.filename > b.filename;
    });
}

void Gallery::scanStorage(CapsAlbumStorage storage) {
    u64 totalCount = 0;
    Result r = capsaGetAlbumFileCount(storage, &totalCount);
    if (R_FAILED(r) || totalCount == 0) return;

    std::vector<CapsAlbumEntry> entries(totalCount);
    u64 actualCount = 0;
    r = capsaGetAlbumFileList(storage, &actualCount, entries.data(), totalCount);
    if (R_FAILED(r)) return;

    for (u64 i = 0; i < actualCount; i++) {
        const CapsAlbumEntry& e = entries[i];
        CapsAlbumFileContents content = (CapsAlbumFileContents)e.file_id.content;

        MediaType type;
        std::string ext;
        if (content == CapsAlbumFileContents_ScreenShot || content == CapsAlbumFileContents_ExtraScreenShot) {
            type = MEDIA_SCREENSHOT;
            ext = ".jpg";
        } else if (content == CapsAlbumFileContents_Movie || content == CapsAlbumFileContents_ExtraMovie) {
            type = MEDIA_VIDEO;
            ext = ".mp4";
        } else {
            continue;
        }

        // Build filename from datetime + application_id
        char fname[128];
        snprintf(fname, sizeof(fname), "%04u%02u%02u%02u%02u%02u%02u-%016llX%s",
            e.file_id.datetime.year,
            e.file_id.datetime.month,
            e.file_id.datetime.day,
            e.file_id.datetime.hour,
            e.file_id.datetime.minute,
            e.file_id.datetime.second,
            e.file_id.datetime.id,
            (unsigned long long)e.file_id.application_id,
            ext.c_str());

        // Find the actual file path on SD card
        std::string fullPath = "";
        const char* albumPaths[] = {
            "sdmc:/Nintendo/Album",
            "sdmc:/emuMMC/RAW1/Nintendo/Album",
            "sdmc:/emuMMC/SD00/Nintendo/Album",
            "sdmc:/emuMMC/SD01/Nintendo/Album",
            nullptr
        };
        char datePath[64];
        snprintf(datePath, sizeof(datePath), "%04u/%02u/%02u",
            e.file_id.datetime.year,
            e.file_id.datetime.month,
            e.file_id.datetime.day);
        for (int p = 0; albumPaths[p] != nullptr; p++) {
            std::string candidate = std::string(albumPaths[p]) + "/" + datePath + "/" + fname;
            FILE* test = fopen(candidate.c_str(), "rb");
            if (test) { fclose(test); fullPath = candidate; break; }
        }

        MediaFile file;
        file.filename  = fname;
        file.fullPath  = fullPath;
        file.type      = type;
        file.filesize  = e.size;
        file.capsEntry = e;
        parseFilename(file);
        m_files.push_back(file);
    }
}

void Gallery::parseFilename(MediaFile& file) const {
    const std::string& fn = file.filename;
    if (fn.size() >= 14) {
        file.date = fn.substr(0,4) + "-" + fn.substr(4,2) + "-" + fn.substr(6,2);
        file.time = fn.substr(8,2) + ":" + fn.substr(10,2) + ":" + fn.substr(12,2);
    } else {
        file.date = "Unknown";
        file.time = "Unknown";
    }
    size_t dash = fn.find('-');
    size_t dot  = fn.rfind('.');
    if (dash != std::string::npos && dot != std::string::npos)
        file.gameId = fn.substr(dash+1, dot-dash-1);
    else
        file.gameId = "Unknown";
    file.gameName = ""; // resolved later
}

bool Gallery::getThumbnail(const std::string& filename, std::vector<uint8_t>& outJpeg) const {
    const MediaFile* f = findByFilename(filename);
    if (!f) return false;

    // capsaLoadAlbumFileThumbnail returns a JPEG for both screenshots and videos
    // Thumbnail size is typically 320x180
    const u64 bufSize = 0x20000; // 128KB - more than enough for a thumbnail
    outJpeg.resize(bufSize);

    capsaInitialize();
    u64 actualSize = 0;
    Result r = capsaLoadAlbumFileThumbnail(&f->capsEntry.file_id, &actualSize, outJpeg.data(), bufSize);
    capsaExit();

    if (R_FAILED(r) || actualSize == 0) {
        outJpeg.clear();
        return false;
    }

    outJpeg.resize(actualSize);
    return true;
}


void Gallery::resolveGameNames() {
    // Build unique set of application IDs
    std::map<std::string, std::string> nameCache;

    if (R_FAILED(nsInitialize())) return;

    for (auto& f : m_files) {
        if (f.gameId.empty() || f.gameId == "Unknown") continue;
        if (nameCache.count(f.gameId)) {
            f.gameName = nameCache[f.gameId];
            continue;
        }

        // Parse application ID from hex string
        u64 appId = 0;
        try { appId = std::stoull(f.gameId, nullptr, 16); } catch (...) { continue; }
        if (appId == 0) continue;

        // Known system app IDs
        static const std::map<u64, std::string> SYSTEM_NAMES = {
            {0x0100000000001000ULL, "Home Screen"},
            {0x0100000000001005ULL, "Errors"},
            {0x0100000000001009ULL, "Mii Editor"},
            {0x000000000000100DULL, "Homebrew"},
            {0x010000000000100DULL, "Homebrew"},
            {0x0100000000001013ULL, "User Settings"},
        };
        if (SYSTEM_NAMES.count(appId)) {
            f.gameName = SYSTEM_NAMES.at(appId);
            nameCache[f.gameId] = f.gameName;
            continue;
        }

        // Get application control data (NACP + icon)
        NsApplicationControlData* ctrlData = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
        if (!ctrlData) continue;

        u64 actualSize = 0;
        Result r = nsGetApplicationControlData(NsApplicationControlSource_Storage, appId, ctrlData, sizeof(NsApplicationControlData), &actualSize);

        if (R_SUCCEEDED(r) && actualSize > 0) {
            // NACP title is at offset 0, up to 512 bytes per language entry
            // First entry (American English) name is at offset 0, 512 bytes, null-terminated
            char name[513] = {};
            memcpy(name, ctrlData->nacp.lang[0].name, 512);
            name[512] = '\0';

            // Trim null bytes and whitespace
            std::string gameName(name);
            size_t end = gameName.find('\0');
            if (end != std::string::npos) gameName = gameName.substr(0, end);
            // Trim trailing spaces
            while (!gameName.empty() && (gameName.back() == ' ' || gameName.back() == '\0'))
                gameName.pop_back();

            if (!gameName.empty()) {
                f.gameName = gameName;
                nameCache[f.gameId] = gameName;
            }
        }
        free(ctrlData);
    }

    nsExit();

    // Any file still without a name gets grouped as "Misc"
    for (auto& f : m_files) {
        if (f.gameName.empty()) {
            f.gameName = "Misc";
            nameCache[f.gameId] = "Misc";
        }
    }
}

int Gallery::getCount() const { return (int)m_files.size(); }

int Gallery::getScreenshotCount() const {
    int c = 0;
    for (const auto& f : m_files) if (f.type == MEDIA_SCREENSHOT) c++;
    return c;
}

int Gallery::getVideoCount() const {
    int c = 0;
    for (const auto& f : m_files) if (f.type == MEDIA_VIDEO) c++;
    return c;
}

const std::vector<MediaFile>& Gallery::getFiles() const { return m_files; }


const MediaFile* Gallery::findByFilename(const std::string& filename) const {
    for (const auto& f : m_files)
        if (f.filename == filename) return &f;
    return nullptr;
}

std::string Gallery::jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

std::string Gallery::toJSON(int offset, int limit, const std::string& filter, const std::string& game) const {
    std::vector<const MediaFile*> filtered;
    for (const auto& f : m_files) {
        if (filter == "screenshots" && f.type != MEDIA_SCREENSHOT) continue;
        if (filter == "videos"      && f.type != MEDIA_VIDEO)      continue;
        if (!game.empty() && f.gameId != game && f.gameName != game) continue;
        filtered.push_back(&f);
    }

    int total = (int)filtered.size();
    int end   = std::min(offset + limit, total);

    std::ostringstream json;
    json << "{";
    json << "\"total\":" << total << ",";
    json << "\"offset\":" << offset << ",";
    json << "\"limit\":" << limit << ",";
    json << "\"screenshots\":" << getScreenshotCount() << ",";
    json << "\"videos\":" << getVideoCount() << ",";
    // Game names list
    json << "\"games\":[";
    auto gnames = getGameNames();
    for (int i = 0; i < (int)gnames.size(); i++) {
        if (i) json << ",";
        json << "\"" << jsonEscape(gnames[i]) << "\"";
    }
    json << "],";

    json << "\"files\":[";

    for (int i = offset; i < end; i++) {
        const MediaFile* f = filtered[i];
        if (i > offset) json << ",";
        json << "{";
        json << "\"filename\":\"" << jsonEscape(f->filename) << "\",";
        json << "\"date\":\"" << jsonEscape(f->date) << "\",";
        json << "\"time\":\"" << jsonEscape(f->time) << "\",";
        json << "\"gameId\":\"" << jsonEscape(f->gameId) << "\",";
        json << "\"gameName\":\"" << jsonEscape(f->gameName) << "\",";
        json << "\"type\":\"" << (f->type == MEDIA_SCREENSHOT ? "screenshot" : "video") << "\",";
        json << "\"size\":" << f->filesize;
        json << "}";
    }

    json << "]}";
    return json.str();
}


std::vector<std::string> Gallery::getGameNames() const {
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const auto& f : m_files) {
        std::string name = f.gameName.empty() ? f.gameId : f.gameName;
        if (!name.empty() && name != "Unknown" && !seen.count(name)) {
            names.push_back(name);
            seen.insert(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool Gallery::serveFileToSocket(const std::string& filename, int sock, long rangeStart, long rangeEnd) const {
    const MediaFile* f = findByFilename(filename);
    if (!f) return false;

    if (f->type == MEDIA_SCREENSHOT) {
        // Screenshots: try fullPath first, fall back to capsaLoadAlbumFile
        if (!f->fullPath.empty()) {
            FILE* fp = fopen(f->fullPath.c_str(), "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long fileSize = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                char hdr[256];
                int hdrLen = snprintf(hdr, sizeof(hdr),
                    "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
                    "Content-Length: %ld\r\nAccept-Ranges: bytes\r\n"
                    "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n", fileSize);
                send(sock, hdr, hdrLen, 0);
                char chunk[32768];
                size_t rd;
                while ((rd = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
                    size_t sent = 0;
                    while (sent < rd) {
                        ssize_t r = ::send(sock, chunk+sent, rd-sent, 0);
                        if (r <= 0) { fclose(fp); return true; }
                        sent += r;
                    }
                }
                fclose(fp);
                return true;
            }
        }
        // Fallback: capsa
        const u64 bufSize = 2 * 1024 * 1024;
        std::vector<uint8_t> buf(bufSize);
        capsaInitialize();
        u64 actualSize = 0;
        Result r = capsaLoadAlbumFile(&f->capsEntry.file_id, &actualSize, buf.data(), bufSize);
        capsaExit();
        if (R_FAILED(r) || actualSize == 0) return false;
        char hdr[256];
        int hdrLen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\nAccess-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n", actualSize);
        send(sock, hdr, hdrLen, 0);
        send(sock, (const char*)buf.data(), actualSize, 0);
        return true;
    }

    // Videos: use capsaOpenAlbumMovieStream for streaming
    // This is the correct way - works regardless of emuMMC/sysMMC
    capsaInitialize();

    u64 streamHandle = 0;
    Result r = capsaOpenAlbumMovieStream(&streamHandle, &f->capsEntry.file_id);
    if (R_FAILED(r)) { capsaExit(); return false; }

    u64 streamSize = 0;
    capsaGetAlbumMovieStreamSize(streamHandle, &streamSize);

    if (streamSize == 0 || streamSize > 0x80000000) {
        capsaCloseAlbumMovieStream(streamHandle);
        capsaExit();
        return false;
    }

    // Handle range request
    bool isRange = (rangeStart >= 0);
    long start   = isRange ? rangeStart : 0;
    long end     = (isRange && rangeEnd >= 0) ? rangeEnd : (long)streamSize - 1;
    long sendLen = end - start + 1;

    char hdr[512];
    int hdrLen;
    if (isRange) {
        hdrLen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\n"
            "Content-Length: %ld\r\nContent-Range: bytes %ld-%ld/%llu\r\n"
            "Accept-Ranges: bytes\r\nAccess-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            sendLen, start, end, (unsigned long long)streamSize);
    } else {
        hdrLen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n"
            "Content-Length: %llu\r\nAccept-Ranges: bytes\r\n"
            "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
            (unsigned long long)streamSize);
    }
    send(sock, hdr, hdrLen, 0);

    // Stream the video in chunks using capsa movie stream API
    const u64 chunkSize = 0x40000; // 256KB chunks
    std::vector<unsigned char> workBuf(chunkSize);
    u64 remaining = (u64)sendLen;
    u64 pos       = (u64)start;

    while (remaining > 0) {
        u64 toRead = std::min(remaining, chunkSize);
        // capsaReadMovieDataFromAlbumMovieReadStream reads by buffer index
        u64 bufIndex = pos / chunkSize;
        u64 offset   = pos % chunkSize;
        u64 actualRead = 0;
        Result rr = capsaReadMovieDataFromAlbumMovieReadStream(streamHandle, bufIndex * chunkSize, workBuf.data(), chunkSize, &actualRead);
        if (R_FAILED(rr) || actualRead == 0) break;

        u64 copySize = std::min(toRead, actualRead - offset);
        size_t sent = 0;
        while (sent < copySize) {
            ssize_t rr2 = ::send(sock, (const char*)workBuf.data() + offset + sent, copySize - sent, 0);
            if (rr2 <= 0) goto done;
            sent += rr2;
        }
        remaining -= copySize;
        pos       += copySize;
    }

done:
    capsaCloseAlbumMovieStream(streamHandle);
    capsaExit();
    return true;
}
