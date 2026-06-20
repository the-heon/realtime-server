#include "Logic.h"
#include "Packet.pb.h"
#include "../PacketManager.h"
#include "../PacketID.h"
#include "../../Core/ServerSession.h"
#include "../../Game/GameRoom.h"
#include "../../Game/RoomManager.h"
#include "../../Utils/Logger.h"

namespace GameLogic
{

void Handle_C_LOGIN(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    Protocol::C_LOGIN pkt;
    if (!pkt.ParseFromArray(payload, payloadSize))
    {
        LOG_WARN("Handle_C_LOGIN: failed to parse payload");
        return;
    }

    const std::string& accountId = pkt.account_id();
    // const std::string& password = pkt.password(); // 실제로는 비밀번호 검증/DB 조회가 들어갈 자리

    // 1. 사용자 인증 및 DB 처리 로직 (생략)
    bool authSuccess = true; // 실제로는 DB 쿼리 결과

    Protocol::S_LOGIN resPkt;
    resPkt.set_success(authSuccess);

    if (authSuccess)
    {
        session->SetAccountId(accountId);
        resPkt.set_message("Login OK.");
    }
    else
    {
        resPkt.set_message("Authentication failed.");
    }

    session->Send(resPkt);
}

void Handle_C_CHAT(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    Protocol::C_CHAT pkt;
    if (!pkt.ParseFromArray(payload, payloadSize))
    {
        LOG_WARN("Handle_C_CHAT: failed to parse payload");
        return;
    }

    GameRoom* room = session->GetRoom();
    if (room == nullptr)
    {
        LOG_WARN("Handle_C_CHAT: " << session->GetAccountId() << " is not in a room");
        return;
    }

    std::string sender = session->GetAccountId();
    std::string message = pkt.message();

    // 채팅은 룸 멤버 목록을 건드리지 않지만, "보내기"도 룸 상태에 대한 작업으로
    // 취급해서 다른 작업들과 같은 순서로 처리되도록 틱 스레드에 맡깁니다.
    room->PushJob([room, sender, message]() {
        Protocol::S_CHAT resPkt;
        resPkt.set_sender(sender);
        resPkt.set_message(message);
        room->Broadcast(resPkt);
    });
}

void Handle_C_ROOM_ENTER(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    Protocol::C_ROOM_ENTER pkt;
    if (!pkt.ParseFromArray(payload, payloadSize))
    {
        LOG_WARN("Handle_C_ROOM_ENTER: failed to parse payload");
        return;
    }

    if (session->GetAccountId().empty())
    {
        Protocol::S_ROOM_ENTER ack;
        ack.set_success(false);
        ack.set_message("로그인 후 입장해주세요.");
        session->Send(ack);
        return;
    }

    GameRoom* previousRoom = session->GetRoom();
    if (previousRoom != nullptr)
    {
        previousRoom->PushJob([previousRoom, session]() { previousRoom->HandleLeave(session); });
    }

    GameRoom* room = RoomManager::Instance().GetOrCreate(pkt.room_name());
    room->PushJob([room, session]() { room->HandleEnter(session); });
}

void Handle_C_ROOM_LEAVE(const std::shared_ptr<ServerSession>& session, char* /*payload*/, int /*payloadSize*/)
{
    GameRoom* room = session->GetRoom();
    if (room == nullptr)
    {
        Protocol::S_ROOM_LEAVE ack;
        ack.set_success(false);
        session->Send(ack);
        return;
    }

    room->PushJob([room, session]() {
        room->HandleLeave(session);
        Protocol::S_ROOM_LEAVE ack;
        ack.set_success(true);
        session->Send(ack);
    });
}

void Handle_C_MOVE(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    Protocol::C_MOVE pkt;
    if (!pkt.ParseFromArray(payload, payloadSize))
    {
        LOG_WARN("Handle_C_MOVE: failed to parse payload");
        return;
    }

    GameRoom* room = session->GetRoom();
    if (room == nullptr)
    {
        return; // 룸에 들어가 있지 않으면 이동도 의미가 없음
    }

    std::string accountId = session->GetAccountId();
    float x = pkt.x();
    float y = pkt.y();
    room->PushJob([room, accountId, x, y]() { room->HandleMove(accountId, x, y); });
}

void RegisterHandlers(PacketManager& packetManager)
{
    packetManager.RegisterHandler(static_cast<uint16_t>(PacketID::C_LOGIN), Handle_C_LOGIN);
    packetManager.RegisterHandler(static_cast<uint16_t>(PacketID::C_CHAT), Handle_C_CHAT);
    packetManager.RegisterHandler(static_cast<uint16_t>(PacketID::C_ROOM_ENTER), Handle_C_ROOM_ENTER);
    packetManager.RegisterHandler(static_cast<uint16_t>(PacketID::C_ROOM_LEAVE), Handle_C_ROOM_LEAVE);
    packetManager.RegisterHandler(static_cast<uint16_t>(PacketID::C_MOVE), Handle_C_MOVE);
}

} // namespace GameLogic
