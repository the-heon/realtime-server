# Window Vscode 실행 방법
- https://www.msys2.org/ 설치
- c_cpp_properties.json 파일 설정 (window API)
  - MSYS2 MinGW 64-bit 터미널 실행
  - pacman -Syu
  - pacman -S mingw-w64-ucrt-x86_64-toolchain
  -  C:\msys64\ucrt64\bin (Path 환경변수 등록)
```json
{
  "configurations": [
    {
      "name": "windows-gcc-x64",
      "includePath": [
        "${workspaceFolder}/**",
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/include/**"
      ],
      "compilerPath": "gcc",
      "cStandard": "${default}",
      "cppStandard": "${default}",
      "intelliSenseMode": "windows-gcc-x64",
      "compilerArgs": [
        ""
      ]
    }
  ],
  "version": 4
}
```


# protoc 설치 (v33.1)
- https://github.com/protocolbuffers/protobuf/releases/tag/v33.2
- 시스템 환경변수 Path 설정
- ```protoc --cpp_out=. Packet.proto ``` 

## Realtime Control-Plane 연동

`realtime-server`는 실행 중 다음 두 가지 운영 연동을 수행합니다.

1. `GET /health`, `GET /metrics`를 `health_server_port`(기본 8081)에서 노출
2. `POST /api/game-servers/heartbeat`를 api-gateway로 주기 전송

필수 설정(`server.cfg`):

1. `api_gateway_host`
2. `api_gateway_port`
3. `game_server_api_key` (웹 관리 화면에서 발급한 키)
4. `game_server_version`

heartbeat payload에는 현재 접속자 수, 최대 인원, 최근 연결/거부 통계가 포함됩니다.

추가 동작:

1. `GET /api/game-servers/traffic-policy`를 주기적으로 조회합니다.
2. 응답의 `maxConnectionsPerMinuteTotal`이 설정되어 있으면,
   실시간 서버는 분당 신규 연결 수를 런타임에서 제한합니다.
3. 제한 초과 시 연결은 즉시 종료되며 `realtime_policy_connection_rejections_total`로 집계됩니다.