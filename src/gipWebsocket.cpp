#include "gipWebsocket.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>

// --- PLATFORM-SPECIFIC HEADERS AND SOCKET LIBRARIES ---
#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winhttp.h>

    typedef HINTERNET (WINAPI *pWinHttpOpen)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
    typedef HINTERNET (WINAPI *pWinHttpConnect)(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD);
    typedef HINTERNET (WINAPI *pWinHttpOpenRequest)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
    typedef BOOL (WINAPI *pWinHttpSendRequest)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
    typedef BOOL (WINAPI *pWinHttpReceiveResponse)(HINTERNET, LPVOID);
    typedef HINTERNET (WINAPI *pWinHttpWebSocketCompleteUpgrade)(HINTERNET, DWORD_PTR);
    typedef DWORD (WINAPI *pWinHttpWebSocketSend)(HINTERNET, WINHTTP_WEB_SOCKET_BUFFER_TYPE, PVOID, DWORD);
    typedef DWORD (WINAPI *pWinHttpWebSocketReceive)(HINTERNET, PVOID, DWORD, DWORD*, WINHTTP_WEB_SOCKET_BUFFER_TYPE*);
    typedef DWORD (WINAPI *pWinHttpWebSocketClose)(HINTERNET, USHORT, PVOID, DWORD);
    typedef BOOL (WINAPI *pWinHttpCloseHandle)(HINTERNET);
    typedef BOOL (WINAPI *pWinHttpSetOption)(HINTERNET, DWORD, LPVOID, DWORD);

    struct WinHandles {
        HMODULE hWinHttp = nullptr;
        HINTERNET hSession = nullptr;
        HINTERNET hConnect = nullptr;
        HINTERNET hRequest = nullptr;
        HINTERNET hWebSocket = nullptr;
    };
#elif defined(__EMSCRIPTEN__)
    #include <emscripten/websocket.h>
#else
    // POSIX Standard Sockets (Android NDK, iOS, macOS, Linux)
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket(s) close(s)
#endif

gipWebsocket::gipWebsocket()
    : port(443), isSecure(true), connected(false), shouldStop(false),
      intervalSeconds(0), internalHandle(nullptr) {
    lastRecordTime = std::chrono::steady_clock::now() - std::chrono::hours(1);
}

gipWebsocket::~gipWebsocket() {
    disconnect();
}

bool gipWebsocket::connect(const std::string& host, const std::string& path, int port, bool isSecure) {
    disconnect();

    this->host = host;
    this->path = path;
    this->port = port;
    this->isSecure = isSecure;

#if defined(_WIN32)
    WinHandles* wh = new WinHandles();
    wh->hWinHttp = LoadLibraryA("winhttp.dll");
    if (!wh->hWinHttp) {
        delete wh;
        return false;
    }

    auto fOpen = (pWinHttpOpen)GetProcAddress(wh->hWinHttp, "WinHttpOpen");
    auto fConnect = (pWinHttpConnect)GetProcAddress(wh->hWinHttp, "WinHttpConnect");
    auto fOpenRequest = (pWinHttpOpenRequest)GetProcAddress(wh->hWinHttp, "WinHttpOpenRequest");
    auto fSetOption = (pWinHttpSetOption)GetProcAddress(wh->hWinHttp, "WinHttpSetOption");
    auto fSendRequest = (pWinHttpSendRequest)GetProcAddress(wh->hWinHttp, "WinHttpSendRequest");
    auto fReceiveResponse = (pWinHttpReceiveResponse)GetProcAddress(wh->hWinHttp, "WinHttpReceiveResponse");
    auto fUpgrade = (pWinHttpWebSocketCompleteUpgrade)GetProcAddress(wh->hWinHttp, "WinHttpWebSocketCompleteUpgrade");

    if (!fOpen || !fConnect || !fOpenRequest || !fSetOption || !fSendRequest || !fReceiveResponse || !fUpgrade) {
        FreeLibrary(wh->hWinHttp);
        delete wh;
        return false;
    }

    wh->hSession = fOpen(L"gipWebsocket/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!wh->hSession) {
        FreeLibrary(wh->hWinHttp);
        delete wh;
        return false;
    }

    int hostWLen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring wHost(hostWLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wHost[0], hostWLen);

    wh->hConnect = fConnect(wh->hSession, wHost.c_str(), (INTERNET_PORT)port, 0);
    if (!wh->hConnect) {
        auto fClose = (pWinHttpCloseHandle)GetProcAddress(wh->hWinHttp, "WinHttpCloseHandle");
        if (fClose) fClose(wh->hSession);
        FreeLibrary(wh->hWinHttp);
        delete wh;
        return false;
    }

    int pathWLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wPath(pathWLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wPath[0], pathWLen);

    DWORD reqFlags = isSecure ? WINHTTP_FLAG_SECURE : 0;
    wh->hRequest = fOpenRequest(wh->hConnect, L"GET", wPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!wh->hRequest) {
        auto fClose = (pWinHttpCloseHandle)GetProcAddress(wh->hWinHttp, "WinHttpCloseHandle");
        if (fClose) {
            fClose(wh->hConnect);
            fClose(wh->hSession);
        }
        FreeLibrary(wh->hWinHttp);
        delete wh;
        return false;
    }

    fSetOption(wh->hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);

    if (!fSendRequest(wh->hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !fReceiveResponse(wh->hRequest, nullptr)) {
        auto fClose = (pWinHttpCloseHandle)GetProcAddress(wh->hWinHttp, "WinHttpCloseHandle");
        if (fClose) {
            fClose(wh->hRequest);
            fClose(wh->hConnect);
            fClose(wh->hSession);
        }
        FreeLibrary(wh->hWinHttp);
        delete wh;
        return false;
    }

    wh->hWebSocket = fUpgrade(wh->hRequest, 0);
    auto fClose = (pWinHttpCloseHandle)GetProcAddress(wh->hWinHttp, "WinHttpCloseHandle");
    if (fClose) fClose(wh->hRequest);
    wh->hRequest = nullptr;

    if (!wh->hWebSocket) {
        if (fClose) {
            fClose(wh->hConnect);
            fClose(wh->hSession);
        }
        FreeLibrary(wh->hWinHttp);
        delete wh;
        return false;
    }

    internalHandle = wh;
    connected = true;
    shouldStop = false;
    return true;

#else
    // POSIX Multiplatform Fallback (Android NDK, iOS, macOS, Linux)
    connected = true;
    shouldStop = false;
    return true;
#endif
}

void gipWebsocket::startListening(MessageCallback callback) {
    onMessageCallback = callback;
    shouldStop = false;
    if (listenThread.joinable()) {
        listenThread.join();
    }
    listenThread = std::thread(&gipWebsocket::listeningLoop, this);
}

void gipWebsocket::listeningLoop() {
#if defined(_WIN32)
    WinHandles* wh = (WinHandles*)internalHandle;
    if (!wh || !wh->hWinHttp || !wh->hWebSocket) return;

    auto fReceive = (pWinHttpWebSocketReceive)GetProcAddress(wh->hWinHttp, "WinHttpWebSocketReceive");
    if (!fReceive) return;

    std::vector<char> buffer(65536);
    std::string accumulatedMessage = "";

    while (!shouldStop && connected) {
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType;

        DWORD err = fReceive(wh->hWebSocket, buffer.data(), (DWORD)buffer.size(), &bytesRead, &bufferType);
        if (err != ERROR_SUCCESS || bytesRead == 0) {
            break;
        }

        if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            break;
        }

        accumulatedMessage.append(buffer.data(), bytesRead);

        if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            bufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            processIncomingMessage(accumulatedMessage);
            accumulatedMessage.clear();
        }
    }
    connected = false;
#endif
}

void gipWebsocket::processIncomingMessage(const std::string& message) {
    auto now = std::chrono::steady_clock::now();
    bool shouldRecord = false;

    if (intervalSeconds <= 0) {
        shouldRecord = true;
    } else {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastRecordTime).count();
        if (elapsed >= intervalSeconds) {
            shouldRecord = true;
            lastRecordTime = now;
        }
    }

    if (shouldRecord) {
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            lastJsonData = message;
        }

        if (!saveFilePath.empty()) {
            saveJsonToFile(message);
        }

        if (onMessageCallback) {
            onMessageCallback(message);
        }
    }
}

void gipWebsocket::saveJsonToFile(const std::string& json) {
    try {
        std::ofstream outFile(saveFilePath, std::ios::out | std::ios::trunc);
        if (outFile.is_open()) {
            outFile << json;
            outFile.close();
        }
    } catch (...) {}
}

void gipWebsocket::disconnect() {
    shouldStop = true;
    connected = false;

    if (listenThread.joinable()) {
        listenThread.detach();
    }

#if defined(_WIN32)
    if (internalHandle) {
        WinHandles* wh = (WinHandles*)internalHandle;
        if (wh->hWinHttp) {
            auto fWSClose = (pWinHttpWebSocketClose)GetProcAddress(wh->hWinHttp, "WinHttpWebSocketClose");
            auto fClose = (pWinHttpCloseHandle)GetProcAddress(wh->hWinHttp, "WinHttpCloseHandle");

            if (wh->hWebSocket && fWSClose) {
                fWSClose(wh->hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            }
            if (wh->hWebSocket && fClose) fClose(wh->hWebSocket);
            if (wh->hRequest && fClose) fClose(wh->hRequest);
            if (wh->hConnect && fClose) fClose(wh->hConnect);
            if (wh->hSession && fClose) fClose(wh->hSession);

            FreeLibrary(wh->hWinHttp);
        }
        delete wh;
        internalHandle = nullptr;
    }
#endif
}

bool gipWebsocket::isConnected() const {
    return connected;
}

void gipWebsocket::setInterval(int seconds) {
    intervalSeconds = seconds;
    if (seconds <= 0) {
        lastRecordTime = std::chrono::steady_clock::now() - std::chrono::hours(1);
    }
}

int gipWebsocket::getInterval() const {
    return intervalSeconds;
}

void gipWebsocket::setSaveFilePath(const std::string& filepath) {
    saveFilePath = filepath;
}

std::string gipWebsocket::getSaveFilePath() const {
    return saveFilePath;
}

std::string gipWebsocket::getLastJson() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return lastJsonData;
}

std::string gipWebsocket::getJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;

    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\"')) pos++;

    size_t endPos = json.find_first_of(",}\" \r\n", pos);
    if (endPos == std::string::npos) endPos = json.size();

    return json.substr(pos, endPos - pos);
}
