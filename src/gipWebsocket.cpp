#include "gipWebsocket.h"
#include <fstream>
#include <iostream>

gipWebsocket::gipWebsocket() {
    connected = false;
    running = false;
    intervalSeconds = 0;
    saveFilePath = "";
    lastRecordTime = std::chrono::steady_clock::now() - std::chrono::hours(1);

#ifdef _WIN32
    hWinHttpDll = nullptr;
    fnWinHttpOpen = nullptr;
    fnWinHttpConnect = nullptr;
    fnWinHttpOpenRequest = nullptr;
    fnWinHttpSetOption = nullptr;
    fnWinHttpSendRequest = nullptr;
    fnWinHttpReceiveResponse = nullptr;
    fnWinHttpWebSocketCompleteUpgrade = nullptr;
    fnWinHttpWebSocketReceive = nullptr;
    fnWinHttpWebSocketClose = nullptr;
    fnWinHttpCloseHandle = nullptr;

    hSession = nullptr;
    hConnect = nullptr;
    hRequest = nullptr;
    hWebSocket = nullptr;

    initWinHttp();
#endif
}

gipWebsocket::~gipWebsocket() {
    disconnect();
#ifdef _WIN32
    if (hWinHttpDll) {
        FreeLibrary(hWinHttpDll);
        hWinHttpDll = nullptr;
    }
#endif
}

void gipWebsocket::initialize() {}

void gipWebsocket::setInterval(int seconds) {
    intervalSeconds = (seconds >= 0) ? seconds : 0;
}

int gipWebsocket::getInterval() const {
    return intervalSeconds;
}

void gipWebsocket::setSaveFilePath(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(dataMutex);
    saveFilePath = filepath;
}

std::string gipWebsocket::getSaveFilePath() const {
    return saveFilePath;
}

std::string gipWebsocket::getLastJson() {
    std::lock_guard<std::mutex> lock(dataMutex);
    return lastJsonData;
}

bool gipWebsocket::isConnected() const {
    return connected;
}

bool gipWebsocket::connect(const std::string& host, const std::string& path, int port, bool isSecure) {
#ifdef _WIN32
    if (!fnWinHttpOpen) return false;
    disconnect();

    hSession = fnWinHttpOpen(L"GlistEngine/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    std::wstring wHost(host.begin(), host.end());
    hConnect = fnWinHttpConnect(hSession, wHost.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) {
        disconnect();
        return false;
    }

    std::wstring wPath(path.begin(), path.end());
    DWORD flags = isSecure ? WINHTTP_FLAG_SECURE : 0;
    hRequest = fnWinHttpOpenRequest(hConnect, L"GET", wPath.c_str(), NULL, NULL, NULL, flags);
    if (!hRequest) {
        disconnect();
        return false;
    }

    BOOL bRes = fnWinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0);
    if (!bRes) {
        disconnect();
        return false;
    }

    bRes = fnWinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bRes) {
        disconnect();
        return false;
    }

    bRes = fnWinHttpReceiveResponse(hRequest, NULL);
    if (!bRes) {
        disconnect();
        return false;
    }

    hWebSocket = fnWinHttpWebSocketCompleteUpgrade(hRequest, 0);
    if (!hWebSocket) {
        disconnect();
        return false;
    }

    fnWinHttpCloseHandle(hRequest);
    hRequest = nullptr;

    connected = true;
    return true;
#else
    return false;
#endif
}

void gipWebsocket::startListening(MessageCallback callback) {
#ifdef _WIN32
    if (!connected || !hWebSocket || !fnWinHttpWebSocketReceive) return;
    running = true;
    onMessageCallback = callback;

    listenThread = std::thread([this]() {
        std::vector<BYTE> buffer(8192);
        std::string messageAccumulator = "";

        while (running && connected && hWebSocket) {
            DWORD bytesRead = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType;
            DWORD status = fnWinHttpWebSocketReceive(
                hWebSocket,
                buffer.data(),
                (DWORD)buffer.size(),
                &bytesRead,
                &bufferType
            );

            if (status != ERROR_SUCCESS || bytesRead == 0) {
                break;
            }

            messageAccumulator.append(reinterpret_cast<char*>(buffer.data()), bytesRead);

            if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                bufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                processIncomingMessage(messageAccumulator);
                messageAccumulator.clear();
            }
        }
        connected = false;
    });
    listenThread.detach();
#endif
}

void gipWebsocket::processIncomingMessage(const std::string& message) {
    auto now = std::chrono::steady_clock::now();
    int interval = intervalSeconds.load();

    bool shouldRecord = false;
    if (interval == 0) {
        shouldRecord = true;
    } else {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastRecordTime).count();
        if (elapsed >= interval) {
            shouldRecord = true;
        }
    }

    if (shouldRecord) {
        lastRecordTime = now;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            lastJsonData = message;
            if (!saveFilePath.empty()) {
                saveJsonToFile(message);
            }
        }

        if (onMessageCallback) {
            onMessageCallback(message);
        }
    }
}

void gipWebsocket::saveJsonToFile(const std::string& json) {
    if (saveFilePath.empty()) return;
    std::ofstream file(saveFilePath, std::ios::out | std::ios::trunc);
    if (file.is_open()) {
        file << json;
        file.close();
    }
}

void gipWebsocket::disconnect() {
#ifdef _WIN32
    running = false;
    connected = false;

    if (hWebSocket && fnWinHttpWebSocketClose && fnWinHttpCloseHandle) {
        fnWinHttpWebSocketClose(hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        fnWinHttpCloseHandle(hWebSocket);
        hWebSocket = nullptr;
    }
    if (hRequest && fnWinHttpCloseHandle) {
        fnWinHttpCloseHandle(hRequest);
        hRequest = nullptr;
    }
    if (hConnect && fnWinHttpCloseHandle) {
        fnWinHttpCloseHandle(hConnect);
        hConnect = nullptr;
    }
    if (hSession && fnWinHttpCloseHandle) {
        fnWinHttpCloseHandle(hSession);
        hSession = nullptr;
    }
#endif
}

std::string gipWebsocket::getJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) {
        searchKey = "\"" + key + "\":";
        pos = json.find(searchKey);
        if (pos == std::string::npos) return "";
        pos += searchKey.length();
        size_t endPos = json.find_first_of(",}", pos);
        if (endPos == std::string::npos) return "";
        return json.substr(pos, endPos - pos);
    }
    pos += searchKey.length();
    size_t endPos = json.find("\"", pos);
    if (endPos == std::string::npos) return "";
    return json.substr(pos, endPos - pos);
}

void gipWebsocket::initWinHttp() {
#ifdef _WIN32
    hWinHttpDll = LoadLibraryA("winhttp.dll");
    if (!hWinHttpDll) return;
    fnWinHttpOpen = (pfnWinHttpOpen)GetProcAddress(hWinHttpDll, "WinHttpOpen");
    fnWinHttpConnect = (pfnWinHttpConnect)GetProcAddress(hWinHttpDll, "WinHttpConnect");
    fnWinHttpOpenRequest = (pfnWinHttpOpenRequest)GetProcAddress(hWinHttpDll, "WinHttpOpenRequest");
    fnWinHttpSetOption = (pfnWinHttpSetOption)GetProcAddress(hWinHttpDll, "WinHttpSetOption");
    fnWinHttpSendRequest = (pfnWinHttpSendRequest)GetProcAddress(hWinHttpDll, "WinHttpSendRequest");
    fnWinHttpReceiveResponse = (pfnWinHttpReceiveResponse)GetProcAddress(hWinHttpDll, "WinHttpReceiveResponse");
    fnWinHttpWebSocketCompleteUpgrade = (pfnWinHttpWebSocketCompleteUpgrade)GetProcAddress(hWinHttpDll, "WinHttpWebSocketCompleteUpgrade");
    fnWinHttpWebSocketReceive = (pfnWinHttpWebSocketReceive)GetProcAddress(hWinHttpDll, "WinHttpWebSocketReceive");
    fnWinHttpWebSocketClose = (pfnWinHttpWebSocketClose)GetProcAddress(hWinHttpDll, "WinHttpWebSocketClose");
    fnWinHttpCloseHandle = (pfnWinHttpCloseHandle)GetProcAddress(hWinHttpDll, "WinHttpCloseHandle");
#endif
}