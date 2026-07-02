#include "HttpClient.h"
#include "Logger.h"
#include <vector>

#pragma comment(lib, "winhttp.lib")

std::wstring HttpClient::ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}

HttpClient::Response HttpClient::Get(const std::string& host, int port,
                                      const std::string& path, int timeoutMs)
{
    return DoRequest("GET", host, port, path, "", timeoutMs);
}

HttpClient::Response HttpClient::Post(const std::string& host, int port,
                                       const std::string& path,
                                       const std::string& body, int timeoutMs)
{
    return DoRequest("POST", host, port, path, body, timeoutMs);
}

HttpClient::Response HttpClient::DoRequest(const std::string& method,
                                            const std::string& host, int port,
                                            const std::string& path,
                                            const std::string& body, int timeoutMs)
{
    Response result;

    HINTERNET hSession = WinHttpOpen(
        L"thehun-realtime/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession) {
        LOG_ERROR("HttpClient: WinHttpOpen failed: " << GetLastError());
        return result;
    }

    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(
        hSession, ToWide(host).c_str(),
        static_cast<INTERNET_PORT>(port), 0);

    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, ToWide(method).c_str(), ToWide(path).c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    LPCWSTR headers    = WINHTTP_NO_ADDITIONAL_HEADERS;
    DWORD   headersLen = 0;
    LPVOID  bodyPtr    = WINHTTP_NO_REQUEST_DATA;
    DWORD   bodyLen    = 0;

    if (!body.empty()) {
        headers    = L"Content-Type: application/json\r\n";
        headersLen = static_cast<DWORD>(-1L); // null-terminated
        bodyPtr    = const_cast<char*>(body.c_str());
        bodyLen    = static_cast<DWORD>(body.size());
    }

    BOOL sent = WinHttpSendRequest(hRequest, headers, headersLen,
                                   bodyPtr, bodyLen, bodyLen, 0);

    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        LOG_WARN("HttpClient: " << method << " " << path << " failed: " << GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.statusCode = static_cast<int>(statusCode);

    DWORD bytesAvail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0) {
        std::vector<char> buf(bytesAvail + 1, 0);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, buf.data(), bytesAvail, &bytesRead);
        result.body.append(buf.data(), bytesRead);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}
