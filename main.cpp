#include "Core/ServerCore.h"
#include "Utils/Config.h"
#include "Utils/Logger.h"
#include <iostream>

int main()
{
    // 설정 파일 로드 (없으면 기본값 유지)
    if (!Config::Instance().Load("server.cfg")) {
        std::cout << "[INFO] server.cfg not found, using default config\n";
    }

    const auto& cfg = Config::Instance().Get();

    try {
        ServerCore server;
        if (server.Init(cfg.port)) {
            server.Start();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
