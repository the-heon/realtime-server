// pch.h
#pragma once

// C/C++ 표준 라이브러리 (자주 사용되는 것)
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <queue>
#include <memory>
#include <stdexcept>
#include <mutex> // 스레드 동기화 관련
#include <functional> // CPacketManager에서 PacketHandlerFunc에 필요
#include <map>        // CPacketManager에서 핸들러 맵에 필요

// Windows API 및 Winsock
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

// Protocol Buffers (자동 생성 파일)
// #include "Packet.pb.h" // 변경 시 전체 재컴파일 유발 가능성이 높아 주석 유지

// 정수 타입 정의 (이미 추가하셨습니다)
#include <cstdint> 

