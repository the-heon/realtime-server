cmake_minimum_required(VERSION 3.16)
project(realtime-server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# GNU 확장(-std=gnu++17)을 끄고 표준 모드(-std=c++17)를 강제합니다.
# 안 그러면 Cygwin/MinGW에서 winsock2.h의 select() 선언이 <pthread.h>를 통해
# 들어오는 POSIX sys/select.h의 select() 선언과 충돌합니다.
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Protobuf REQUIRED)

set(SERVER_SOURCES
    main.cpp
    Core/ServerCore.cpp
    Core/ServerSession.cpp
    Packet/PacketManager.cpp
    Packet/RecvBuffer.cpp
    Packet/Logic/Logic.cpp
    Packet/Logic/Packet.pb.cc
    Game/SessionManager.cpp
    Game/RoomManager.cpp
)

add_executable(realtime-server ${SERVER_SOURCES})

target_include_directories(realtime-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Packet/Logic
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(realtime-server PRIVATE
    ${Protobuf_LIBRARIES}
    ws2_32
    mswsock
)

if(MSVC)
    target_compile_options(realtime-server PRIVATE /W4)
else()
    target_compile_options(realtime-server PRIVATE -Wall -Wextra)
endif()

# Packet.proto가 바뀌면 protoc로 Packet.pb.h/.pb.cc를 다시 만들어야 합니다:
#   protoc --cpp_out=Packet/Logic Packet.proto   (Packet/Logic 디렉토리에서 실행)

option(BUILD_TEST_CLIENT "수동 검증용 TestClient도 같이 빌드" ON)
if(BUILD_TEST_CLIENT)
    add_executable(test-client TestClient.cpp Packet/Logic/Packet.pb.cc)
    target_include_directories(test-client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Packet/Logic
        ${Protobuf_INCLUDE_DIRS}
    )
    target_link_libraries(test-client PRIVATE ${Protobuf_LIBRARIES} ws2_32)
endif()
