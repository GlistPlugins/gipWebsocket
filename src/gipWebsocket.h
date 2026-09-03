#ifndef GIPWEBSOCKET_H_
#define GIPWEBSOCKET_H_

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

class gipWebsocket {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    gipWebsocket();
    virtual ~gipWebsocket();

    // Cross-platform connection management
    bool connect(const std::string& host, const std::string& path, int port = 443, bool isSecure = true);
    void startListening(MessageCallback callback = nullptr);
    void disconnect();
    bool isConnected() const;

    // Time Interval / Throttling (0 = real-time instant, N = every N seconds)
    void setInterval(int seconds);
    int getInterval() const;

    // Automatic local file persistence
    void setSaveFilePath(const std::string& filepath);
    std::string getSaveFilePath() const;

    // JSON helpers
    std::string getLastJson();
    static std::string getJsonValue(const std::string& json, const std::string& key);

private:
    void listeningLoop();
    void processIncomingMessage(const std::string& message);
    void saveJsonToFile(const std::string& json);

    std::string host;
    std::string path;
    int port;
    bool isSecure;

    std::atomic<bool> connected;
    std::atomic<bool> shouldStop;

    std::thread listenThread;
    MessageCallback onMessageCallback;

    int intervalSeconds;
    std::chrono::steady_clock::time_point lastRecordTime;
    std::string lastJsonData;
    std::string saveFilePath;
    std::mutex dataMutex;

    void* internalHandle;
};

#endif /* GIPWEBSOCKET_H_ */
