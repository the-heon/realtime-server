#pragma once
#include <memory>

class ServerSession;
class PacketManager;

namespace GameLogic
{
    void Handle_C_LOGIN     (const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_CHAT      (const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_ROOM_ENTER(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_ROOM_LEAVE(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_MOVE      (const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);

    // C_PING: payload = [uint64 clientTimestampMs]
    // 서버는 S_PONG = [uint64 clientTimestampMs][uint64 serverTimestampMs] 로 응답해
    // 클라이언트가 RTT를 계산할 수 있게 합니다.
    void Handle_C_PING      (const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);

    // C_ROOM_LIST: payload 없음
    // 서버는 현재 모든 룸 목록을 S_ROOM_LIST 바이너리 포맷으로 응답합니다.
    void Handle_C_ROOM_LIST (const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);

    void RegisterHandlers(PacketManager& packetManager);
}
