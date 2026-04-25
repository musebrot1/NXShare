#pragma once
#include <string>
#include <vector>
#include <switch.h>

enum MediaType {
    MEDIA_SCREENSHOT,
    MEDIA_VIDEO,
    MEDIA_UNKNOWN
};

struct MediaFile {
    std::string filename;
    std::string fullPath;
    std::string date;
    std::string time;
    std::string gameId;
    std::string gameName;   // Resolved from NS application control data
    MediaType type;
    size_t filesize;
    CapsAlbumEntry capsEntry;
};

class Gallery {
public:
    Gallery();
    ~Gallery();

    void scan();
    int getCount() const;
    int getScreenshotCount() const;
    int getVideoCount() const;
    const std::vector<MediaFile>& getFiles() const;
    const MediaFile* findByFilename(const std::string& filename) const;
    std::string toJSON(int offset = 0, int limit = 50, const std::string& filter = "", const std::string& game = "") const;
    std::vector<std::string> getGameNames() const;

    // Get JPEG thumbnail via capsa API (works for both screenshots and videos)
    bool getThumbnail(const std::string& filename, std::vector<uint8_t>& outJpeg) const;

    // Serve file content to a socket directly (screenshots via capsa, videos via stream)
    bool serveFileToSocket(const std::string& filename, int sock, long rangeStart, long rangeEnd) const;

private:
    std::vector<MediaFile> m_files;
    void scanStorage(CapsAlbumStorage storage);
    void parseFilename(MediaFile& file) const;
    void resolveGameNames();
    static std::string jsonEscape(const std::string& s);
};
