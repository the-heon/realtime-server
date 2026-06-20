#include "PacketManager.h"
#include "PacketHeader.h"
#include "../Utils/Logger.h"

void PacketManager::RegisterHandler(uint16_t packetId, PacketHandlerFunc handler)
{
    m_handlers[packetId] = std::move(handler);
}

void PacketManager::HandlePacket(const std::shared_ptr<ServerSession>& session, char* packetData, int size)
{
    if (size < (int)sizeof(PacketHeader)) {
        LOG_WARN("Packet too small to contain a header: " << size << " bytes");
        return;
    }

    PacketHeader* header = reinterpret_cast<PacketHeader*>(packetData);
    uint16_t packetId = header->packetId;
    int payloadSize = size - sizeof(PacketHeader);
    char* payload = packetData + sizeof(PacketHeader);

    auto it = m_handlers.find(packetId);
    if (it == m_handlers.end()) {
        LOG_WARN("Unhandled packet id: " << packetId);
        return;
    }

    it->second(session, payload, payloadSize);
}
