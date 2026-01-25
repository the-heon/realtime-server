#pragma once
#include "ServerSession.h"
#include "Packet.pb.h" // Protobuf 정의 포함

// 패킷 처리 함수들을 관리하는 네임스페이스 (중복 방지)
namespace GameLogic
{
    /**
     * @brief 로그인 요청(C_LOGIN)을 처리합니다.
     * @param session 데이터를 보낸 클라이언트 세션
     * @param payload Protobuf 데이터의 시작 주소
     * @param payloadSize 데이터의 크기
     */
    void Handle_C_LOGIN(ServerSession* session, char* payload, int payloadSize);

    /**
     * @brief 채팅 메시지(C_CHAT)를 처리합니다.
     * @param session 데이터를 보낸 클라이언트 세션
     * @param payload Protobuf 데이터의 시작 주소
     * @param payloadSize 데이터의 크기
     */
    void Handle_C_CHAT(ServerSession* session, char* payload, int payloadSize);

    // 앞으로 추가될 패킷 핸들러들을 여기에 선언합니다.
    // void Handle_C_MOVE(...);
    // void Handle_C_SKILL(...);
}