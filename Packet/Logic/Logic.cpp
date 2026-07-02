#include "Logic.h"
#include "Packet.pb.h"
#include "../PacketManager.h"
#include "../PacketID.h"
#include "../../Core/ServerSession.h"
#include "../../Game/GameRoom.h"
#include "../../Game/RoomManager.h"
#include "../../Utils/Logger.h"
#include "../../Utils/AuthService.h"
#include <chrono>
#include <cstring>
#include <vector>
#include <cstdint>

namespace GameLogic
{

// 로그인 흐름:
//   1. 클라이언트가 먼저 session-server에 POST /auth/login -> session_token 수령
//   2. 클라이언트가 realtime-server에 C_LOGIN 전송
//        account_id = 계정 ID
//        password   = session_token  (proto 필드명 재활용; 실제로는 토큰)
//   3. realtime-server가 AuthService(비동기) -> session-server GET /auth/validate 호출
//   4. 검증 완료 콜백에서 S_LOGIN 응답
void Handle_C_LOGIN(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    Protocol::C_LOGIN pkt;
    if (!pkt.ParseFromArray(payload, payloadSize)) {
        LOG_WARN("Handle_C_LOGIN: failed to parse payload");
        return;
    }

    const std::string token = pkt.password(); // password 필드 = session_token

    if (token.empty()) {
        Protocol::S_LOGIN res;
        res.set_success(false);
        res.set_message("session_token이 비어 있습니다. 먼저 session-server에 로그인하세요.");
        session->Send(res);
        return;
    }

    // IOCP 워커 스레드를 블록하지 않도록 AuthService에 위임.
    // 콜백은 AuthService 전용 스레드에서 호출됩니다.
    AuthService::Instance().ValidateAsync(token,
        [session](bool valid, std::string accountId)
        {
            Protocol::S_LOGIN res;
            res.set_success(valid);

            if (valid) {
                session->SetAccountId(accountId);
                res.set_message("Login OK.");
                LOG_INFO("Login success: accountId=" << accountId
                         << " sessionId=" << session->GetSessionId());
            } else {
                res.set_message("유효하지 않은 session_token입니다.");
                LOG_WARN("Login failed: invalid token, sessionId=" << session->GetSessionId());
            }

            session->Send(res);
        });
}

void Handle_C_CHAT(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    Protocol::C_CHAT pkt;
    if (!pkt.ParseFromArray(payload, payloadSize)) {
        LOG_WARN("Handle_C_CHAT: failed to parse payload");
        return;
    }

    GameRoom* room = session->GetRoom();
    if (room == nullptr) {
        LOG_WARN("Handle_C_CHAT: " << session->GetAccountId() << " is not in a room");
        return;
    }

    std::string sender  = session->GetAccountId();
    std::string message = pkt.message();

    room->PushJob([room, sender, message]() {
        Protocol::S_CHAT res;
        res.set_sender(sender);
        res.set_message(message);
        room->Broadcast(res);
    });
}

void Handle_C_ROOM_ENTER(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    Protocol::C_ROOM_ENTER pkt;
    if (!pkt.ParseFromArray(payload, payloadSize)) {
        LOG_WARN("Handle_C_ROOM_ENTER: failed to parse payload");
        return;
    }

    if (session->GetAccountId().empty()) {
        Protocol::S_ROOM_ENTER ack;
        ack.set_success(false);
        ack.set_message("로그인 후 입장해주세요.");
        session->Send(ack);
        return;
    }

    if (GameRoom* prev = session->GetRoom()) {
        prev->PushJob([prev, session]() { prev->HandleLeave(session); });
    }

    GameRoom* room = RoomManager::Instance().GetOrCreate(pkt.room_name());
    room->PushJob([room, session]() { room->HandleEnter(session); });
}

void Handle_C_ROOM_LEAVE(const std::shared_ptr<ServerSession>& session, char* /*payload*/, int /*payloadSize*/)
{
    GameRoom* room = session->GetRoom();
    if (room == nullptr) {
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
    if (!pkt.ParseFromArray(payload, payloadSize)) {
        LOG_WARN("Handle_C_MOVE: failed to parse payload");
        return;
    }

    GameRoom* room = session->GetRoom();
    if (room == nullptr) return;

    std::string accountId = session->GetAccountId();
    float x = pkt.x(), y = pkt.y();
    room->PushJob([room, accountId, x, y]() { room->HandleMove(accountId, x, y); });
}

void Handle_C_PING(const std::shared_ptr<ServerSession>& session, char* payload, int payloadSize)
{
    if (payloadSize < 8) {
        LOG_WARN("Handle_C_PING: payload too small (" << payloadSize << " bytes)");
        return;
    }

    uint64_t clientTs = 0;
    std::memcpy(&clientTs, payload, sizeof(uint64_t));

    uint64_t serverTs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    uint8_t pong[16];
    std::memcpy(pong,     &clientTs, 8);
    std::memcpy(pong + 8, &serverTs, 8);

    session->SendRaw(static_cast<uint16_t>(PacketID::S_PONG), pong, sizeof(pong));
}

void Handle_C_ROOM_LIST(const std::shared_ptr<ServerSession>& session, char* /*payload*/, int /*payloadSize*/)
{
    auto rooms = RoomManager::Instance().GetRoomInfoList();

    // S_ROOM_LIST 바이너리 포맷 직렬화
    // [uint16 count] { [uint16 nameLen][name bytes][uint16 playerCount][uint16 maxPlayers][uint8 state] }...
    std::vector<uint8_t> data;
    data.reserve(2 + rooms.size() * 26);

    auto writeU16 = [&](uint16_t v) {
        data.push_back(static_cast<uint8_t>(v & 0xFF));
        data.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    };

    writeU16(static_cast<uint16_t>(rooms.size()));
    for (const auto& r : rooms) {
        writeU16(static_cast<uint16_t>(r.name.size()));
        for (char c : r.name) data.push_back(static_cast<uint8_t>(c));
        writeU16(static_cast<uint16_t>(r.playerCount));
        writeU16(static_cast<uint16_t>(r.maxPlayers));
        data.push_back(static_cast<uint8_t>(r.state));
    }

    session->SendRaw(static_cast<uint16_t>(PacketID::S_ROOM_LIST),
                     data.data(), static_cast<int>(data.size()));
}

void Handle_C_READY(const std::shared_ptr<ServerSession>& session, char* /*payload*/, int /*payloadSize*/)
{
    if (session->GetAccountId().empty()) {
        LOG_WARN("Handle_C_READY: 로그인 없이 ready 시도, sessionId=" << session->GetSessionId());
        return;
    }

    GameRoom* room = session->GetRoom();
    if (room == nullptr) {
        LOG_WARN("Handle_C_READY: " << session->GetAccountId() << " is not in a room");
        return;
    }

    std::string accountId = session->GetAccountId();
    room->PushJob([room, accountId]() {
        room->HandleReady(accountId);
    });
}

void RegisterHandlers(PacketManager& pm)
{
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_LOGIN),     Handle_C_LOGIN);
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_CHAT),      Handle_C_CHAT);
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_ROOM_ENTER),Handle_C_ROOM_ENTER);
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_ROOM_LEAVE),Handle_C_ROOM_LEAVE);
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_MOVE),      Handle_C_MOVE);
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_PING),      Handle_C_PING);
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_ROOM_LIST), Handle_C_ROOM_LIST);
    pm.RegisterHandler(static_cast<uint16_t>(PacketID::C_READY),     Handle_C_READY);
}

} // namespace GameLogic
