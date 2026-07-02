#include "HttpClient.h"
#include "Logger.h"
#include <vector>
#include <stdexcept>

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

    // 연결/수신 타임아웃 설정 (ms 단위)
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(
        hSession, ToWide(host).c_str(),
        static_cast<INTERNET_PORT>(port), 0);

    if (!hConnect) {
        LOG_ERROR("HttpClient: WinHttpConnect failed: " << GetLastError());
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", ToWide(path).c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0); // 0 = HTTP (not HTTPS); 내부망 통신이므로 TLS 불필요

    if (!hRequest) {
        LOG_ERROR("HttpClient: WinHttpOpenRequest failed: " << GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    BOOL sent = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        LOG_WARN("HttpClient: request failed: " << GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // HTTP 상태 코드 읽기
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.statusCode = static_cast<int>(statusCode);

    // 응답 바디 읽기
    DWORD bytesAvail = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvail) && bytesAvail > 0)
    {
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
