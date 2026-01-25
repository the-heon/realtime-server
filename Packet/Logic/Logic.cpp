// GameLogic.cpp
#include "Logic.h"
#include "Packet.pb.h"      // Protobuf 메시지 정의 (Protocol::C_LOGIN 등)

// C_LOGIN 패킷 처리 함수 구현
void Handle_C_LOGIN(ServerSession* ServerSession, char* payload, int payloadSize)
{
    Protocol::C_LOGIN pkt;
    if (!pkt.ParseFromArray(payload, payloadSize))
    {
        // 파싱 실패 시 처리
        return;
    }

    const std::string& accountId = pkt.account_id();
    // const std::string& password = pkt.password(); // 비밀번호는 보안상 처리 필요

    // 1. 사용자 인증 및 DB 처리 로직 (생략)
    bool authSuccess = true; // 실제로는 DB 쿼리 결과

    // 2. 응답 패킷 생성 및 전송
    Protocol::S_LOGIN resPkt;
    resPkt.set_success(authSuccess);
    
    if (authSuccess)
    {
        // ServerSession->SetAccountId(accountId);
        resPkt.set_message("Login OK.");
    }
    else
    {
        resPkt.set_message("Authentication failed.");
    }

    // 세션의 Send 함수를 통해 클라이언트에 응답합니다.
    ServerSession->Send(resPkt); 
}

// C_CHAT 패킷 처리 함수 구현
void Handle_C_CHAT(ServerSession* ServerSession, char* payload, int payloadSize)
{
    Protocol::C_CHAT pkt;
    if (!pkt.ParseFromArray(payload, payloadSize))
    {
        // 파싱 실패 시 처리
        return;
    }

    const std::string& message = pkt.message();
    
    // 1. 메시지 필터링 및 유효성 검사 (생략)

    // 2. 브로드캐스트용 응답 패킷 생성
    Protocol::S_CHAT resPkt;
    // resPkt.set_sender(ServerSession->GetAccountId()); // 발신자 정보 추가
    resPkt.set_message(message);

    // 3. 채팅방/전체 서버에 브로드캐스트 (생략)
    // GServerManager::Broadcast(resPkt); 
}