#pragma once
#include <memory>

class ServerSession;
class PacketManager;

// 패킷 처리 함수들을 관리하는 네임스페이스 (중복 방지)
namespace GameLogic
{
    void Handle_C_LOGIN(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_CHAT(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_ROOM_ENTER(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_ROOM_LEAVE(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);
    void Handle_C_MOVE(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize);

    // ServerCore가 시작할 때 한 번 호출해서 위 핸들러들을 PacketManager에 등록합니다.
    void RegisterHandlers(PacketManager& packetManager);
}
