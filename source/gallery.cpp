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
#include <time.h>

static s64 utcTimestampFromParts(int year, int month, int day, int hour, int minute, int second) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const s64 days = (s64)era * 146097 + (s64)doe - 719468;
    return days * 86400 + hour * 3600 + minute * 60 + second;
}

static s64 localTimestampFromAlbumDateTime(const CapsAlbumFileDateTime& dt) {
    struct tm tm_info = {};
    tm_info.tm_year = (int)dt.year - 1900;
    tm_info.tm_mon  = (int)dt.month - 1;
    tm_info.tm_mday = (int)dt.day;
    tm_info.tm_hour = (int)dt.hour;
    tm_info.tm_min  = (int)dt.minute;
    tm_info.tm_sec  = (int)dt.second;
    tm_info.tm_isdst = -1;

    time_t ts = mktime(&tm_info);
    if (ts != (time_t)-1) return (s64)ts;

    return utcTimestampFromParts(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

static bool parsePngUtcTimestamp(const std::string& name, s64& outTimestamp) {
    if (name.size() < 19 || name[4] != '-' || name[7] != '-' || name[10] != '_')
        return false;

    int year, month, day, hour, minute, second;
    if (sscanf(name.c_str(), "%4d-%2d-%2d_%2d-%2d-%2d",
               &year, &month, &day, &hour, &minute, &second) != 6)
        return false;

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
        return false;

    outTimestamp = utcTimestampFromParts(year, month, day, hour, minute, second);
    return true;
}

// Recursively scan a directory tree for PNG files
void Gallery::scanPngDirectory(const char* dirPath) {
    DIR* d = opendir(dirPath);
    if (!d) return;

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue; // skip . and ..

        std::string fullPath = std::string(dirPath) + "/" + ent->d_name;

        if (ent->d_type == DT_DIR) {
            // Recurse into subdirectory
            scanPngDirectory(fullPath.c_str());
            continue;
        }

        if (ent->d_type != DT_REG) continue;

        std::string name = ent->d_name;
        if (name.size() < 4) continue;
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".png") continue;

        MediaFile f;
        f.filename = name;
        f.fullPath = fullPath;
        f.type     = MEDIA_SCREENSHOT;
        f.gameName = "Misc";
        f.gameId   = "png";
        f.storage  = "sd";
        f.filesize = 0;
        f.sortTimestamp = 0;

        struct stat st = {};
        bool hasStat = stat(f.fullPath.c_str(), &st) == 0;
        if (hasStat) {
            f.filesize = st.st_size;
            f.sortTimestamp = (s64)st.st_mtime;
        }

        // Parse date/time from filename (YYYY-MM-DD_HH-MM-SS*)
        // PNG timestamps are UTC; keep display fields as encoded, but sort by epoch.
        if (name.size() >= 19 &&
            name[4] == '-' && name[7] == '-' && name[10] == '_') {
            f.date = name.substr(0, 10);
            std::string t = name.substr(11, 8);
            std::replace(t.begin(), t.end(), '-', ':');
            f.time = t;
            parsePngUtcTimestamp(name, f.sortTimestamp);
        } else if (hasStat) {
            // Filesystem mtimes are epoch-based, and localtime is only for display.
            f.sortTimestamp = (s64)st.st_mtime;
            struct tm* tm_info = localtime(&st.st_mtime);
            char buf[11], tbuf[9];
            strftime(buf,  sizeof(buf),  "%Y-%m-%d", tm_info);
            strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm_info);
            f.date = buf;
            f.time = tbuf;
        } else {
            f.date = "Unknown";
            f.time = "Unknown";
        }

        m_files.push_back(f);
    }
    closedir(d);
}

void Gallery::scan() {
    m_files.clear();

    // Initialize capsa (capture service)
    capsaInitialize();

    scanStorage(CapsAlbumStorage_Sd);
    scanStorage(CapsAlbumStorage_Nand);

    capsaExit();

    // Scan for PNG files saved by homebrew tools
    static const char* PNG_DIRS[] = {
        "sdmc:/Nintendo/Album/PNGs",
        "sdmc:/emuMMC/RAW1/Nintendo/Album/PNGs",
        "sdmc:/emuMMC/SD00/Nintendo/Album/PNGs",
        "sdmc:/emuMMC/SD01/Nintendo/Album/PNGs",
    };
    for (const char* dir : PNG_DIRS) {
        scanPngDirectory(dir);
    }

    // Resolve game names via NS service
    resolveGameNames();

    // Sort newest first
    std::sort(m_files.begin(), m_files.end(), [](const MediaFile& a, const MediaFile& b) {
        if (a.sortTimestamp != b.sortTimestamp)
            return a.sortTimestamp > b.sortTimestamp;
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
        file.storage   = storage == CapsAlbumStorage_Nand ? "system" : "sd";
        file.sortTimestamp = localTimestampFromAlbumDateTime(e.file_id.datetime);
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
            // NACP title is up to 512 bytes per language entry, null-terminated
            // see also: https://switchbrew.org/wiki/NACP#ApplicationTitle
            char name[513] = {};
            NacpLanguageEntry* langentry = {};
            // nacpGetLanguageEntry defaults to system language if available, or falls back
            r = nacpGetLanguageEntry(&(ctrlData->nacp), &langentry);
            if (R_SUCCEEDED(r) && langentry != 0 && langentry->name[0] != '\x00') {
                memcpy(name, langentry->name, 512);
            } else {
                // try fallback to this?
                r = nsGetApplicationDesiredLanguage(&(ctrlData->nacp), &langentry);
                if (R_SUCCEEDED(r) && langentry != 0 && langentry->name[0] != '\x00') {
                    memcpy(name, langentry->name, 512);
                } else {
                    // fallback, in order: en, en-gb, ja, fr, de, es, es-latam, it, nl, fr-ca, pt, ru, kr, zh, zh-hans, pt-br ...
                    for (int n = 0; n < 16; n++) {
                        if (ctrlData->nacp.lang[n].name[0] != '\x00') {
                            memcpy(name, ctrlData->nacp.lang[n].name, 512);
                            break;
                        }
                    }
                }
            }
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
    // First try exact filename match
    for (const auto& f : m_files)
        if (f.filename == filename) return &f;
    // Fallback: match by basename of fullPath (for PNG files in subdirs)
    for (const auto& f : m_files) {
        size_t slash = f.fullPath.rfind('/');
        std::string base = (slash != std::string::npos) ? f.fullPath.substr(slash+1) : f.fullPath;
        if (base == filename) return &f;
    }
    return nullptr;
}

std::string Gallery::jsonEscape(const std::string& s) {
    std::string out;
    static const char HEX[] = "0123456789ABCDEF";

    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += HEX[c >> 4];
                    out += HEX[c & 0x0F];
                } else {
                    out += (char)c;
                }
                break;
        }
    }
    return out;
}

std::string Gallery::toJSON(int offset, int limit, const std::string& filter,
                            const std::string& game, int year, int month,
                            const std::string& storage) const {
    std::vector<const MediaFile*> filtered;
    for (const auto& f : m_files) {
        if (filter == "screenshots" && f.type != MEDIA_SCREENSHOT) continue;
        if (filter == "videos"      && f.type != MEDIA_VIDEO)      continue;
        if (!game.empty() && f.gameId != game && f.gameName != game) continue;
        if (!storage.empty() && f.storage != storage) continue;
        if (year > 0) {
            if (f.date.size() < 7 || atoi(f.date.substr(0, 4).c_str()) != year) continue;
            if (month > 0 && atoi(f.date.substr(5, 2).c_str()) != month) continue;
        }
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

    // Storage locations present in the complete album, independent of filters.
    bool hasSystemStorage = false;
    bool hasSdStorage = false;
    for (const auto& f : m_files) {
        if (f.storage == "system") hasSystemStorage = true;
        if (f.storage == "sd") hasSdStorage = true;
    }
    json << "\"storages\":[";
    if (hasSystemStorage) json << "\"system\"";
    if (hasSystemStorage && hasSdStorage) json << ",";
    if (hasSdStorage) json << "\"sd\"";
    json << "],";

    // Available dates from the complete album, independent of active filters.
    std::map<int, std::set<int>, std::greater<int>> dates;
    for (const auto& f : m_files) {
        if (f.date.size() < 10 || f.date[4] != '-' || f.date[7] != '-') continue;
        int fileYear = atoi(f.date.substr(0, 4).c_str());
        int fileMonth = atoi(f.date.substr(5, 2).c_str());
        if (fileYear > 0 && fileMonth >= 1 && fileMonth <= 12)
            dates[fileYear].insert(fileMonth);
    }

    json << "\"dates\":[";
    bool firstYear = true;
    for (const auto& entry : dates) {
        if (!firstYear) json << ",";
        firstYear = false;
        json << "{\"year\":" << entry.first << ",\"months\":[";
        bool firstMonth = true;
        for (int availableMonth : entry.second) {
            if (!firstMonth) json << ",";
            firstMonth = false;
            json << availableMonth;
        }
        json << "]}";
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
        json << "\"storage\":\"" << jsonEscape(f->storage) << "\",";
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
                const char* mime = "image/jpeg";
                {
                    const std::string& fp3 = f->fullPath;
                    if (fp3.size() > 4) {
                        std::string ext = fp3.substr(fp3.size() - 4);
                        if (ext == ".png") mime = "image/png";
                        else if (ext == ".mp4") mime = "video/mp4";
                    }
                }
                size_t sl = f->fullPath.rfind('/');
                std::string fname = (sl != std::string::npos) ? f->fullPath.substr(sl+1) : f->fullPath;
                char hdr[512];
                int hdrLen = snprintf(hdr, sizeof(hdr),
                    "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
                    "Content-Disposition: attachment; filename=\"%s\"\r\n"
                    "Content-Length: %ld\r\nAccept-Ranges: bytes\r\n"
                    "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
                    mime, fname.c_str(), fileSize);
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
