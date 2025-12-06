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

// Windows API 및 Winsock
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

// Protocol Buffers (자동 생성 파일)
#include "Packet.pb.h"

// 서버 공통 구조체 및 클래스 (전방 선언으로 처리할 수 없는 경우)
#include "MemoryPool.h" // COverlappedEx, CMemoryPool 정의 등
#include "ServerCore.h" // CSession 클래스 등