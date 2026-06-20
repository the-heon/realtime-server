// 간단한 동기식 테스트 클라이언트. 서버 검증용 — 실제 프로젝트 소스에는 포함하지 않습니다.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include "Packet/Logic/Packet.pb.h"
#include "Packet/PacketHeader.h"
#include "Packet/PacketID.h"

SOCKET Connect(const char* host, int port)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "connect failed: " << WSAGetLastError() << std::endl;
        exit(1);
    }
    return sock;
}

template<typename T>
void SendPacket(SOCKET sock, PacketID id, const T& message)
{
    std::string payload;
    message.SerializeToString(&payload);

    PacketHeader header;
    header.packetSize = (uint16_t)payload.size();
    header.packetId = (uint16_t)id;

    std::string buffer;
    buffer.append((char*)&header, sizeof(header));
    buffer.append(payload);

    send(sock, buffer.data(), (int)buffer.size(), 0);
}

void RecvLoop(SOCKET sock, const char* tag)
{
    char buf[4096];
    while (true) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            std::cout << "[" << tag << "] disconnected" << std::endl;
            return;
        }

        int offset = 0;
        while (offset + (int)sizeof(PacketHeader) <= n) {
            PacketHeader* header = (PacketHeader*)(buf + offset);
            int total = sizeof(PacketHeader) + header->packetSize;
            if (offset + total > n) break;

            char* payload = buf + offset + sizeof(PacketHeader);
            switch ((PacketID)header->packetId) {
                case PacketID::S_LOGIN: {
                    Protocol::S_LOGIN pkt;
                    pkt.ParseFromArray(payload, header->packetSize);
                    std::cout << "[" << tag << "] S_LOGIN success=" << pkt.success() << " msg=" << pkt.message() << std::endl;
                    break;
                }
                case PacketID::S_ROOM_ENTER: {
                    Protocol::S_ROOM_ENTER pkt;
                    pkt.ParseFromArray(payload, header->packetSize);
                    std::cout << "[" << tag << "] S_ROOM_ENTER success=" << pkt.success() << " room=" << pkt.room_name() << std::endl;
                    break;
                }
                case PacketID::S_ROOM_USER_ENTERED: {
                    Protocol::S_ROOM_USER_ENTERED pkt;
                    pkt.ParseFromArray(payload, header->packetSize);
                    std::cout << "[" << tag << "] S_ROOM_USER_ENTERED account=" << pkt.account_id() << std::endl;
                    break;
                }
                case PacketID::S_CHAT: {
                    Protocol::S_CHAT pkt;
                    pkt.ParseFromArray(payload, header->packetSize);
                    std::cout << "[" << tag << "] S_CHAT " << pkt.sender() << ": " << pkt.message() << std::endl;
                    break;
                }
                case PacketID::S_MOVE_BROADCAST: {
                    Protocol::S_MOVE_BROADCAST pkt;
                    pkt.ParseFromArray(payload, header->packetSize);
                    std::cout << "[" << tag << "] S_MOVE_BROADCAST actors=" << pkt.actors_size();
                    for (auto& a : pkt.actors()) {
                        std::cout << " (" << a.account_id() << ":" << a.x() << "," << a.y() << ")";
                    }
                    std::cout << std::endl;
                    break;
                }
                default:
                    std::cout << "[" << tag << "] unknown packetId=" << header->packetId << std::endl;
            }
            offset += total;
        }
    }
}

int main(int argc, char** argv)
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::string tag = argc > 1 ? argv[1] : "A";
    SOCKET sock = Connect("127.0.0.1", 7777);

    std::thread recvThread(RecvLoop, sock, tag.c_str());

    Protocol::C_LOGIN login;
    login.set_account_id("user_" + tag);
    login.set_password("pw");
    SendPacket(sock, PacketID::C_LOGIN, login);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    Protocol::C_ROOM_ENTER enter;
    enter.set_room_name("room1");
    SendPacket(sock, PacketID::C_ROOM_ENTER, enter);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    Protocol::C_MOVE move;
    move.set_x(1.5f);
    move.set_y(2.5f);
    SendPacket(sock, PacketID::C_MOVE, move);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    Protocol::C_CHAT chat;
    chat.set_message("hello from " + tag);
    SendPacket(sock, PacketID::C_CHAT, chat);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    closesocket(sock);
    recvThread.join();

    WSACleanup();
    return 0;
}
