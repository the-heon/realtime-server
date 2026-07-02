#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>

// WinHTTP 기반 동기 HTTP 클라이언트.
// AuthService의 전용 스레드에서만 호출하므로 동기 방식으로 구현합니다.
// (IOCP 워커 스레드에서 직접 호출하면 스레드가 블록되므로 절대 사용하지 마세요)
class HttpClient
{
public:
    struct Response
    {
        int         statusCode = 0;
        std::string body;
        bool ok() const { return statusCode >= 200 && statusCode < 300; }
    };

    // 동기 GET 요청. 실패 시 statusCode=0 반환.
    static Response Get(const std::string& host, int port,
                        const std::string& path, int timeoutMs = 3000);

private:
    static std::wstring ToWide(const std::string& s);
};
