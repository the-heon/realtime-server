// main.cpp
#include "Core/ServerCore.h"

int main()
{
    try {
        CIOCPServer server;
        if (server.Init(7777))
        {
            server.Start();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}