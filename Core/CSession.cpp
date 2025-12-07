// CSession.cpp

#include "CSession.h"

// 전역 메모리 풀 (예시, 외부에서 정의되었다고 가정)
// extern CMemoryPool g_overlapPool;

// 🎯 생성자
CSession::CSession()
{
    // CMemoryPool g_overlapPool을 사용하여 CIOContext를 할당한다고 가정합니다.
    // 여기서는 간단히 new로 대체합니다. 실제 서버에서는 메모리 풀을 사용해야 합니다.
    try
    {
        // Recv 작업에 사용할 I/O Context를 미리 할당합니다.
        m_recvContext = new CIOContext(EIOType::RECV, this); 
        m_recvContext->wsaBuf.buf = m_recvBuffer;
        m_recvContext->wsaBuf.len = BUF_SIZE;
        
    }
    catch (const std::bad_alloc& e)
    {
        // 메모리 할당 실패 처리 (Fatal Error)
        std::cerr << "Fatal Error: Failed to allocate CIOContext. " << e.what() << std::endl;
        m_recvContext = nullptr; 
    }
}

// 🎯 소멸자
CSession::~CSession()
{
    // 할당했던 Context 메모리를 해제합니다.
    if (m_recvContext)
    {
        // 실제 서버에서는 메모리 풀에 반환해야 합니다.
        delete m_recvContext; 
        m_recvContext = nullptr;
    }

    if (m_sock != INVALID_SOCKET)
    {
        // 소켓 정리 (CIOCPServer::Disconnect에서 호출되어야 함)
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
}

// 🎯 초기화 및 연결 정보 설정
void CSession::Init(SOCKET socket, const sockaddr_in& addr)
{
    m_sock = socket;
    m_addr = addr;

    // Recv Context의 버퍼 정보 초기화 (Init 시점에서는 불필요할 수 있지만 안전을 위해)
    if (m_recvContext)
    {
        m_recvContext->wsaBuf.buf = m_recvBuffer;
        m_recvContext->wsaBuf.len = BUF_SIZE;
        // 다른 멤버(overlapped, ioType)는 생성자에서 초기화됨
    }
}

// 🎯 연결 종료
void CSession::Disconnect()
{
    if (m_sock != INVALID_SOCKET)
    {
        // WinSock 소켓 닫기
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        // std::cout << "Session Disconnected." << std::endl; // 로그 예시
    }
    // TODO: 세션 cleanup 로직 (다른 스레드에 Disconnect 알림 등)
}