#pragma once
#include <string>
#include <pthread.h>
#include "gallery.hpp"

struct ClientRequest {
    std::string method;
    std::string path;
    std::string query;
    long rangeStart = -1;
    long rangeEnd   = -1;
};

class Server {
public:
    Server(int port, Gallery* gallery, const char* ipStr);
    ~Server();

    void start();
    void stop();
    bool isRunning() const { return m_running; }

private:
    int m_port;
    int m_socket;
    Gallery* m_gallery;
    std::string m_ip;
    bool m_running;
    pthread_t m_thread;

    static void* threadFunc(void* arg);
    void serverLoop();
    void handleClient(int clientSock);

    // Request parsing
    ClientRequest parseRequest(const std::string& raw);
    std::string getQueryParam(const std::string& query, const std::string& key);

    // Response builders
    void sendResponse(int sock, int code, const std::string& contentType,
                      const std::string& body);
    void sendFile(int sock, const std::string& filepath, const std::string& contentType,
                  long rangeStart = -1, long rangeEnd = -1);
    void sendNotFound(int sock);

    // Route handlers
    void handleIndex(int sock);
    void handleAPI(int sock, const ClientRequest& req);
    void handleMedia(int sock, const ClientRequest& req);
    void handleThumb(int sock, const ClientRequest& req);

    // Helpers
    std::string urlDecode(const std::string& s);
    std::string guessMime(const std::string& filename);
};
