
// IOCP 비동기 작업 유형 정의 (예시)
enum class EIOType
{
    RECV, // 데이터 수신
    SEND, // 데이터 송신
    ACCEPT // 연결 수락
};

// 🌟 IOCP Overlapped I/O 작업 구조체 (CompletionKey로 전달되지 않고, OVERLAPPED 포인터로 사용됨)
// 각 비동기 요청(Recv, Send)마다 생성됩니다.
struct IOContext
{
    OVERLAPPED overlapped = {}; // WinSock2의 기본 OVERLAPPED 구조체
    EIOType ioType = EIOType::RECV; // 어떤 종류의 I/O 작업인지 식별
    ServerSession* serverSession = nullptr; // 이 작업이 속한 세션 포인터
    WSABUF wsaBuf = {}; // 전송/수신 버퍼 정보

    // 생성자: 오버랩 구조체의 필드를 초기화합니다.
    IOContext(EIOType type, ServerSession* owner) 
        : ioType(type), serverSession(owner) 
    {
        memset(&overlapped, 0, sizeof(OVERLAPPED));
    }
    
    // 복사 방지
    IOContext(const IOContext&) = delete;
    IOContext& operator=(const IOContext&) = delete;
};

// 🧑‍💻 클라이언트 세션 클래스 정의
class ServerSession
{
public:
    ServerSession();
    ~ServerSession();

    // 💡 연결 및 초기화
    void Init(SOCKET socket, const sockaddr_in& addr);
    void Disconnect();

    // 💡 Getter
    SOCKET GetSocket() const { return m_sock; }
    sockaddr_in GetAddress() const { return m_addr; }

    // 💡 I/O 관련 상태
    // IOCP Recv를 위한 컨텍스트와 버퍼
    IOContext* GetRecvContext() { return m_recvContext; }
    char* GetRecvBuffer() { return m_recvBuffer; }
    int GetRecvBufferSize() const { return BUF_SIZE; }

private:
    // 🌟 핵심 연결 정보
    SOCKET m_sock = INVALID_SOCKET;
    sockaddr_in m_addr = {};

    // 🌟 I/O 관련 멤버
    // 수신 버퍼 크기 (예시)
    static constexpr int BUF_SIZE = 4096; 
    char m_recvBuffer[BUF_SIZE]; 

    // 🌟 Recv 작업을 위한 Overlapped Context (메모리 풀에서 할당된다고 가정)
    IOContext* m_recvContext = nullptr;

    // TODO: Send 큐, Lock, 세션 ID, 로그인 상태 등 추가 정보가 필요합니다.
};