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