#ifndef SRC_GIPWEBSOCKET_H_
#define SRC_GIPWEBSOCKET_H_

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

typedef HINTERNET(WINAPI* pfnWinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef HINTERNET(WINAPI* pfnWinHttpConnect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
typedef HINTERNET(WINAPI* pfnWinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
typedef BOOL(WINAPI* pfnWinHttpSetOption)(HINTERNET, DWORD, LPVOID, DWORD);
typedef BOOL(WINAPI* pfnWinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI* pfnWinHttpReceiveResponse)(HINTERNET, LPVOID);
typedef HINTERNET(WINAPI* pfnWinHttpWebSocketCompleteUpgrade)(HINTERNET, DWORD_PTR);
typedef DWORD(WINAPI* pfnWinHttpWebSocketReceive)(HINTERNET, PVOID, DWORD, DWORD*, WINHTTP_WEB_SOCKET_BUFFER_TYPE*);
typedef DWORD(WINAPI* pfnWinHttpWebSocketClose)(HINTERNET, USHORT, PVOID, DWORD);
typedef BOOL(WINAPI* pfnWinHttpCloseHandle)(HINTERNET);
#endif

class gipWebsocket {
public:
    using MessageCallback = std::function<void(const std::string& rawJson)>;

    gipWebsocket();
    virtual ~gipWebsocket();

    void initialize();

    bool connect(const std::string& host, const std::string& path, int port = 443, bool isSecure = true);
    void startListening(MessageCallback callback = nullptr);
    void disconnect();
    bool isConnected() const;

    void setInterval(int seconds);
    int getInterval() const;

    void setSaveFilePath(const std::string& filepath);
    std::string getSaveFilePath() const;

    std::string getLastJson();

    static std::string getJsonValue(const std::string& json, const std::string& key);

private:
    void initWinHttp();
    void processIncomingMessage(const std::string& message);
    void saveJsonToFile(const std::string& json);

#ifdef _WIN32
    HMODULE hWinHttpDll;
    pfnWinHttpOpen fnWinHttpOpen;
    pfnWinHttpConnect fnWinHttpConnect;
    pfnWinHttpOpenRequest fnWinHttpOpenRequest;
    pfnWinHttpSetOption fnWinHttpSetOption;
    pfnWinHttpSendRequest fnWinHttpSendRequest;
    pfnWinHttpReceiveResponse fnWinHttpReceiveResponse;
    pfnWinHttpWebSocketCompleteUpgrade fnWinHttpWebSocketCompleteUpgrade;
    pfnWinHttpWebSocketReceive fnWinHttpWebSocketReceive;
    pfnWinHttpWebSocketClose fnWinHttpWebSocketClose;
    pfnWinHttpCloseHandle fnWinHttpCloseHandle;

    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hRequest;
    HINTERNET hWebSocket;
#else
    void* hSession;
    void* hConnect;
    void* hRequest;
    void* hWebSocket;
#endif

    std::atomic<bool> connected;
    std::atomic<bool> running;
    std::atomic<int> intervalSeconds;
    std::string saveFilePath;

    std::mutex dataMutex;
    std::string lastJsonData;
    std::chrono::steady_clock::time_point lastRecordTime;

    MessageCallback onMessageCallback;
    std::thread listenThread;
};

#endif /* SRC_GIPWEBSOCKET_H_ */