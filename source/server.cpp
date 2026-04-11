#include "server.hpp"
#include "gallery.hpp"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sstream>
#include <algorithm>

// Embedded web frontend - include directly so no linker symbol needed
#include "html_data.cpp"  // lives in include/
#include "thumb.hpp"

Server::Server(int port, Gallery* gallery, const char* ipStr)
    : m_port(port), m_socket(-1), m_gallery(gallery),
      m_ip(ipStr), m_running(false) {}

Server::~Server() {
    stop();
}

void Server::start() {
    m_running = true;
    pthread_create(&m_thread, nullptr, threadFunc, this);
}

void Server::stop() {
    m_running = false;
    if (m_socket >= 0) {
        close(m_socket);
        m_socket = -1;
    }
    pthread_join(m_thread, nullptr);
}

void* Server::threadFunc(void* arg) {
    ((Server*)arg)->serverLoop();
    return nullptr;
}

void Server::serverLoop() {
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0) {
        printf("  ERROR: Could not create socket\n");
        return;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(m_port);

    if (bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("  ERROR: Could not bind to port %d\n", m_port);
        close(m_socket);
        return;
    }

    if (listen(m_socket, 8) < 0) {
        printf("  ERROR: listen() failed\n");
        close(m_socket);
        return;
    }

    while (m_running) {
        // Use select() with timeout so we can check m_running
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(m_socket, &fds);
        struct timeval tv = {1, 0}; // 1 second timeout
        
        int ready = select(m_socket + 1, &fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSock = accept(m_socket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSock < 0) continue;

        handleClient(clientSock);
        close(clientSock);
    }

    close(m_socket);
}

void Server::handleClient(int clientSock) {
    // Set receive timeout
    struct timeval tv = {5, 0};
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Read request
    char buf[4096] = {};
    int received = recv(clientSock, buf, sizeof(buf) - 1, 0);
    if (received <= 0) return;

    buf[received] = '\0';
    ClientRequest req = parseRequest(std::string(buf));

    // Route
    if (req.path == "/" || req.path == "/index.html") {
        handleIndex(clientSock);
    } else if (req.path.substr(0, 4) == "/api") {
        handleAPI(clientSock, req);
    } else if (req.path.substr(0, 7) == "/media/") {
        handleMedia(clientSock, req);
    } else if (req.path.substr(0, 7) == "/thumb/") {
        handleThumb(clientSock, req);
    } else {
        sendNotFound(clientSock);
    }
}

ClientRequest Server::parseRequest(const std::string& raw) {
    ClientRequest req;
    req.rangeStart = -1;
    req.rangeEnd   = -1;
    std::istringstream stream(raw);
    std::string line;
    std::getline(stream, line);
    
    // Parse "GET /path?query HTTP/1.1"
    size_t sp1 = line.find(' ');
    size_t sp2 = line.rfind(' ');
    if (sp1 == std::string::npos || sp2 == std::string::npos || sp1 == sp2) {
        req.method = "GET";
        req.path   = "/";
        return req;
    }
    
    req.method = line.substr(0, sp1);
    std::string pathQuery = line.substr(sp1 + 1, sp2 - sp1 - 1);
    
    size_t qmark = pathQuery.find('?');
    if (qmark != std::string::npos) {
        req.path  = pathQuery.substr(0, qmark);
        req.query = pathQuery.substr(qmark + 1);
    } else {
        req.path = pathQuery;
    }
    
    // Parse remaining headers for Range:
    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") break;
        if (line.find("Range:") == 0 || line.find("range:") == 0) {
            // Range: bytes=START-END
            size_t eq = line.find('=');
            size_t dash = line.find('-', eq);
            if (eq != std::string::npos && dash != std::string::npos) {
                std::string startStr = line.substr(eq+1, dash-eq-1);
                std::string endStr   = line.substr(dash+1);
                // trim whitespace/cr
                while (!startStr.empty() && (startStr.back()==' '||startStr.back()=='\r')) startStr.pop_back();
                while (!endStr.empty()   && (endStr.back()==' '  ||endStr.back()=='\r'))   endStr.pop_back();
                if (!startStr.empty()) req.rangeStart = atoll(startStr.c_str());
                if (!endStr.empty())   req.rangeEnd   = atoll(endStr.c_str());
            }
        }
    }
    return req;
}

std::string Server::getQueryParam(const std::string& query, const std::string& key) {
    std::string search = key + "=";
    size_t pos = query.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    size_t end = query.find('&', pos);
    if (end == std::string::npos) end = query.size();
    return urlDecode(query.substr(pos, end - pos));
}

std::string Server::urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            int val;
            sscanf(s.c_str() + i + 1, "%02x", &val);
            out += (char)val;
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

void Server::handleIndex(int sock) {
    // Serve embedded HTML
    const char* start = _binary_data_web_index_html_start;
    size_t size = strlen(start);
    
    std::ostringstream header;
    header << "HTTP/1.1 200 OK\r\n";
    header << "Content-Type: text/html; charset=utf-8\r\n";
    header << "Content-Length: " << size << "\r\n";
    header << "Connection: close\r\n";
    header << "\r\n";
    
    std::string hdr = header.str();
    send(sock, hdr.c_str(), hdr.size(), 0);
    send(sock, start, size, 0);
}

void Server::handleAPI(int sock, const ClientRequest& req) {
    // /api/list?offset=0&limit=50&filter=screenshots|videos|""
    if (req.path == "/api/list") {
        int offset = 0, limit = 50;
        std::string filter = getQueryParam(req.query, "filter");
        std::string offsetStr = getQueryParam(req.query, "offset");
        std::string limitStr  = getQueryParam(req.query, "limit");
        if (!offsetStr.empty()) offset = atoi(offsetStr.c_str());
        if (!limitStr.empty())  limit  = atoi(limitStr.c_str());
        
        std::string json = m_gallery->toJSON(offset, limit, filter);
        sendResponse(sock, 200, "application/json", json);
        return;
    }
    
    // /api/refresh - rescan gallery
    if (req.path == "/api/refresh") {
        m_gallery->scan();
        std::ostringstream json;
        json << "{\"count\":" << m_gallery->getCount() << "}";
        sendResponse(sock, 200, "application/json", json.str());
        return;
    }
    
    sendNotFound(sock);
}

void Server::handleMedia(int sock, const ClientRequest& req) {
    // /media/<filename>
    std::string filename = req.path.substr(7); // strip "/media/"
    filename = urlDecode(filename);
    
    // Security: reject paths with ".."
    if (filename.find("..") != std::string::npos) {
        sendNotFound(sock);
        return;
    }
    
    const MediaFile* file = m_gallery->findByFilename(filename);
    if (!file) {
        sendNotFound(sock);
        return;
    }

    if (!m_gallery->serveFileToSocket(filename, sock, req.rangeStart, req.rangeEnd)) {
        sendNotFound(sock);
    }
}


void Server::handleThumb(int sock, const ClientRequest& req) {
    std::string filename = req.path.substr(7);
    filename = urlDecode(filename);
    if (filename.find("..") != std::string::npos) { sendNotFound(sock); return; }

    const MediaFile* file = m_gallery->findByFilename(filename);
    if (!file) { sendNotFound(sock); return; }

    // Use capsaLoadAlbumFileThumbnail - works for both screenshots AND videos
    std::vector<uint8_t> thumb;
    if (m_gallery->getThumbnail(filename, thumb)) {
        char hdr[256];
        int hdrLen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
            thumb.size());
        send(sock, hdr, hdrLen, 0);
        send(sock, (const char*)thumb.data(), thumb.size(), 0);
        return;
    }

    if (false) { // placeholder for old video branch
        // No matching jpg - send a tiny 1x1 transparent placeholder
        // (1x1 grey JPEG, 329 bytes)
        static const uint8_t placeholder[] = {
            0xFF,0xD8,0xFF,0xE0,0x00,0x10,0x4A,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,
            0x00,0x01,0x00,0x00,0xFF,0xDB,0x00,0x43,0x00,0x08,0x06,0x06,0x07,0x06,0x05,0x08,
            0x07,0x07,0x07,0x09,0x09,0x08,0x0A,0x0C,0x14,0x0D,0x0C,0x0B,0x0B,0x0C,0x19,0x12,
            0x13,0x0F,0x14,0x1D,0x1A,0x1F,0x1E,0x1D,0x1A,0x1C,0x1C,0x20,0x24,0x2E,0x27,0x20,
            0x22,0x2C,0x23,0x1C,0x1C,0x28,0x37,0x29,0x2C,0x30,0x31,0x34,0x34,0x34,0x1F,0x27,
            0x39,0x3D,0x38,0x32,0x3C,0x2E,0x33,0x34,0x32,0xFF,0xC0,0x00,0x0B,0x08,0x00,0x01,
            0x00,0x01,0x01,0x01,0x11,0x00,0xFF,0xC4,0x00,0x1F,0x00,0x00,0x01,0x05,0x01,0x01,
            0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,
            0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0xFF,0xC4,0x00,0xB5,0x10,0x00,0x02,0x01,0x03,
            0x03,0x02,0x04,0x03,0x05,0x05,0x04,0x04,0x00,0x00,0x01,0x7D,0x01,0x02,0x03,0x00,
            0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,
            0x81,0x91,0xA1,0x08,0x23,0x42,0xB1,0xC1,0x15,0x52,0xD1,0xF0,0x24,0x33,0x62,0x72,
            0x82,0x09,0x0A,0x16,0x17,0x18,0x19,0x1A,0x25,0x26,0x27,0x28,0x29,0x2A,0x34,0x35,
            0x36,0x37,0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x53,0x54,0x55,
            0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x73,0x74,0x75,
            0x76,0x77,0x78,0x79,0x7A,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x92,0x93,0x94,
            0x95,0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xB2,
            0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,
            0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,
            0xE7,0xE8,0xE9,0xEA,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFF,0xDA,
            0x00,0x08,0x01,0x01,0x00,0x00,0x3F,0x00,0xFB,0xD6,0xFF,0xD9
        };
        char hdr[256];
        int hdrLen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n"
            "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
            sizeof(placeholder));
        send(sock, hdr, hdrLen, 0);
        send(sock, (const char*)placeholder, sizeof(placeholder), 0);
        return;
    }
    sendFile(sock, file->fullPath, "image/jpeg");
}

void Server::sendResponse(int sock, int code, const std::string& contentType,
                          const std::string& body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << code << " OK\r\n";
    header << "Content-Type: " << contentType << "\r\n";
    header << "Content-Length: " << body.size() << "\r\n";
    header << "Access-Control-Allow-Origin: *\r\n";
    header << "Connection: close\r\n";
    header << "\r\n";
    
    std::string hdr = header.str();
    send(sock, hdr.c_str(), hdr.size(), 0);
    send(sock, body.c_str(), body.size(), 0);
}

void Server::sendFile(int sock, const std::string& filepath,
                      const std::string& contentType,
                      long rangeStart, long rangeEnd) {
    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) { sendNotFound(sock); return; }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    bool isRange = (rangeStart >= 0);
    if (isRange) {
        if (rangeEnd < 0 || rangeEnd >= fileSize) rangeEnd = fileSize - 1;
        long sendLen = rangeEnd - rangeStart + 1;
        char hdr[512];
        int hdrLen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 206 Partial Content\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Content-Range: bytes %ld-%ld/%ld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            contentType.c_str(), sendLen, rangeStart, rangeEnd, fileSize);
        send(sock, hdr, hdrLen, 0);
        fseek(f, rangeStart, SEEK_SET);
        long remaining = sendLen;
        char chunk[32768];
        while (remaining > 0) {
            int toRead = (int)std::min((long)sizeof(chunk), remaining);
            int rd = (int)fread(chunk, 1, toRead, f);
            if (rd <= 0) break;
            size_t sent = 0;
            while ((long)sent < rd) {
                ssize_t r = send(sock, chunk+sent, rd-sent, 0);
                if (r <= 0) { fclose(f); return; }
                sent += r;
            }
            remaining -= rd;
        }
    } else {
        char hdr[512];
        int hdrLen = snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %ld\r\n"
            "Accept-Ranges: bytes\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            contentType.c_str(), fileSize);
        send(sock, hdr, hdrLen, 0);
        char chunk[32768];
        size_t bytesRead;
        while ((bytesRead = fread(chunk, 1, sizeof(chunk), f)) > 0) {
            size_t sent = 0;
            while (sent < bytesRead) {
                ssize_t r = send(sock, chunk+sent, bytesRead-sent, 0);
                if (r <= 0) { fclose(f); return; }
                sent += r;
            }
        }
    }
    fclose(f);
}

void Server::sendNotFound(int sock) {
    sendResponse(sock, 404, "text/plain", "404 Not Found");
}

std::string Server::guessMime(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower.size() >= 4) {
        std::string ext = lower.substr(lower.size() - 4);
        if (ext == ".jpg") return "image/jpeg";
        if (ext == ".mp4") return "video/mp4";
    }
    if (lower.size() >= 5) {
        std::string ext = lower.substr(lower.size() - 5);
        if (ext == ".jpeg") return "image/jpeg";
    }
    return "application/octet-stream";
}
