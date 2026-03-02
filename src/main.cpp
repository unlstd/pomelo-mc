#include <array>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

int readVarInt(const std::vector<uint8_t> &buffer, size_t &offset);
void handle_client(int client_fd);
std::array<int, 2> init_tcp_server();
uint16_t readUnsignedShort(const std::vector<uint8_t> &data, size_t &offset);
std::string readString(const std::vector<uint8_t> &data, size_t &offset);
std::vector<uint8_t> writeString(const std::string &s);
std::vector<uint8_t> writeVarInt(int value);
void sendLoginSuccess(int client_fd, const std::string &username);
void sendJoinGame(int client_fd);

int main() {
    std::cout << "Hello from pomelo-mc!" << std::endl;

    std::array<int, 2> sockets = init_tcp_server();
    int client_fd = sockets[0];
    int server_fd = sockets[1];

    if (client_fd < 0) {
        std::cerr << "Accept failed\n";
        return 1;
    }
    handle_client(client_fd);

#ifdef _WIN32
    closesocket(client_fd);
    closesocket(server_fd);
    WSACleanup();
#else
    close(client_fd);
    close(server_fd);
#endif

    return 0;
}

enum class ClientState {
    HANDSHAKE,
    LOGIN,
    PLAY
};

void handle_client(int client_fd) {
    ClientState state = ClientState::HANDSHAKE;

    while (true) {
        char buffer[1024];
        int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) break;

        std::vector<uint8_t> data(buffer, buffer + bytes_received);
        size_t offset = 0;

        try {
            int packetLength = readVarInt(data, offset);
            int packetId = readVarInt(data, offset);

            switch (state) {
            case ClientState::HANDSHAKE:
                if (packetId == 0x00) {
                    int protocolVersion = readVarInt(data, offset);
                    std::string address = readString(data, offset);
                    uint16_t port = readUnsignedShort(data, offset);
                    int nextState = readVarInt(data, offset);

                    std::cout << "Client connecting to: " << address << "\n";

                    if (nextState == 2)
                        state = ClientState::LOGIN;
                    else if (nextState == 1)
                        std::cout << "Client wants STATUS (ping)\n";
                    else
                        std::cout << "Unknown next state: " << nextState << "\n";
                }
                break;

            case ClientState::LOGIN:
                if (packetId == 0x00) { // Login Start
                    std::string username = readString(data, offset);
                    sendLoginSuccess(client_fd, username);
                    sendJoinGame(client_fd);
                    std::cout << username << " has joined the game!\n";
                    state = ClientState::PLAY;
                }
                break;

            case ClientState::PLAY:
                int packetId = readVarInt(data, offset);
                if (packetId == 0x0F) { // Keep Alive
                    // send(client_fd, reinterpret_cast<const char *>(data.data()), bytes_received, 0);
                }
                break;
            }
        } catch (std::exception &e) {
            std::cerr << "Error parsing packet: " << e.what() << "\n";
        }
    }
}

void sendJoinGame(int client_fd) {
    std::vector<uint8_t> packet;

    // Packet ID 0x26
    std::vector<uint8_t> id = writeVarInt(0x26);
    packet.insert(packet.end(), id.begin(), id.end());

    // Entity ID
    int entityId = 1;
    packet.push_back((entityId >> 24) & 0xFF);
    packet.push_back((entityId >> 16) & 0xFF);
    packet.push_back((entityId >> 8) & 0xFF);
    packet.push_back(entityId & 0xFF);

    // Gamemode (0 = Survival)
    packet.push_back(0);
    // Previous Gamemode (-1 = 0xFF)
    packet.push_back(0xFF);
    // Hardcore flag
    packet.push_back(0);

    // Dimension Codec (binary NBT)
    const uint8_t dimensionCodecNBT[] = {
        0x0A, 0x00,                                                                                                                         // TAG_Compound, name length 0
        0x0A, 0x0C, 'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':', 'd', 'i', 'm', 'e', 'n', 's', 'i', 'o', 'n', '_', 't', 'y', 'p', 'e', // TAG_Compound "minecraft:dimension_type"
        0x0A, 0x00,                                                                                                                         // empty compound
        0x00                                                                                                                                // TAG_End
    };
    packet.insert(packet.end(), dimensionCodecNBT, dimensionCodecNBT + sizeof(dimensionCodecNBT));

    // Dimension (Overworld)
    const uint8_t dimensionNBT[] = {
        0x0A, 0x00,                                                                                                // TAG_Compound, name length 0
        0x0A, 0x0A, 'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':', 'o', 'v', 'e', 'r', 'w', 'o', 'r', 'l', 'd', // TAG_Compound "minecraft:overworld"
        0x00,                                                                                                      // TAG_End
        0x00                                                                                                       // TAG_End
    };
    packet.insert(packet.end(), dimensionNBT, dimensionNBT + sizeof(dimensionNBT));

    // World name
    std::vector<uint8_t> worldName = writeString("world");
    packet.insert(packet.end(), worldName.begin(), worldName.end());

    // Hashed seed (long)
    for (int i = 0; i < 8; i++) packet.push_back(0);

    // Max players (VarInt)
    std::vector<uint8_t> maxPlayers = writeVarInt(100);
    packet.insert(packet.end(), maxPlayers.begin(), maxPlayers.end());

    // View distance (VarInt)
    std::vector<uint8_t> viewDistance = writeVarInt(10);
    packet.insert(packet.end(), viewDistance.begin(), viewDistance.end());

    // Reduced Debug Info
    packet.push_back(0);
    // Enable Respawn Screen
    packet.push_back(1);
    // Is Debug
    packet.push_back(0);
    // Is Flat
    packet.push_back(0);

    // Długość pakietu
    std::vector<uint8_t> length = writeVarInt(packet.size());

    std::vector<uint8_t> finalPacket;
    finalPacket.insert(finalPacket.end(), length.begin(), length.end());
    finalPacket.insert(finalPacket.end(), packet.begin(), packet.end());

    send(client_fd, reinterpret_cast<const char *>(finalPacket.data()), finalPacket.size(), 0);
    std::cout << "Join Game packet sent!\n";
}

void sendLoginSuccess(int client_fd, const std::string &username) {
    std::vector<uint8_t> packet;

    // Login Success always has id 0x02
    std::vector<uint8_t> id = writeVarInt(0x02);
    packet.insert(packet.end(), id.begin(), id.end());

    // UUID offline doesn't really matter so it can be anything
    for (int i = 0; i < 16; i++) packet.push_back(0);

    std::vector<uint8_t> name = writeString(username);
    packet.insert(packet.end(), name.begin(), name.end());

    std::vector<uint8_t> propertiesCount = writeVarInt(0); // 0 properties
    packet.insert(packet.end(), propertiesCount.begin(), propertiesCount.end());

    std::vector<uint8_t> length = writeVarInt(packet.size());

    std::vector<uint8_t> finalPacket;
    finalPacket.insert(finalPacket.end(), length.begin(), length.end());
    finalPacket.insert(finalPacket.end(), packet.begin(), packet.end());
    std::cout << "login success packet: ";
    for (auto byte_r : finalPacket) {
        std::cout << std::hex << (int)byte_r << " ";
    }
    std::cout << "\n";
    send(client_fd, reinterpret_cast<const char *>(finalPacket.data()), finalPacket.size(), 0);
}

std::vector<uint8_t> writeVarInt(int value) {
    std::vector<uint8_t> bytes;
    do {
        uint8_t temp = value & 0b01111111;
        value >>= 7;
        if (value != 0) temp |= 0b10000000;
        bytes.push_back(temp);
    } while (value != 0);
    return bytes;
}

std::vector<uint8_t> writeString(const std::string &s) {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> len = writeVarInt(s.size());
    bytes.insert(bytes.end(), len.begin(), len.end());
    bytes.insert(bytes.end(), s.begin(), s.end());
    return bytes;
}

std::string readString(const std::vector<uint8_t> &data, size_t &offset) {
    int length = readVarInt(data, offset);

    if (length < 0) {
        throw std::runtime_error("Invalid string length");
    }

    if (offset + length > data.size()) {
        throw std::runtime_error("Not enough bytes for string");
    }

    std::string result(
        data.begin() + offset,
        data.begin() + offset + length);

    offset += length;

    return result;
}

uint16_t readUnsignedShort(const std::vector<uint8_t> &data, size_t &offset) {
    if (offset + 1 >= data.size()) {
        throw std::runtime_error("Not enough bytes for Unsigned Short");
    }

    uint16_t value = (data[offset] << 8) | data[offset + 1];
    offset += 2;

    return value;
}

int readVarInt(const std::vector<uint8_t> &buffer, size_t &offset) {
    int numRead = 0;
    int result = 0;
    uint8_t read;

    do {
        if (offset >= buffer.size()) throw std::runtime_error("VarInt out of bounds");
        read = buffer[offset++];
        int value = (read & 0b01111111);
        result |= (value << (7 * numRead));

        numRead++;
        if (numRead > 5) throw std::runtime_error("VarInt too big");
    } while ((read & 0b10000000) != 0);

    return result;
}

std::array<int, 2> init_tcp_server() {

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return {1, 1};
    }
#endif

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return {1, 1};
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(25565);

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed\n";
        return {1, 1};
    }

    if (listen(server_fd, 1) < 0) {
        std::cerr << "Listen failed\n";
        return {1, 1};
    }

    std::cout << "Server listening on port 25565...\n";

    sockaddr_in client_addr{};
#ifdef _WIN32
    int client_size = sizeof(client_addr);
#else
    socklen_t client_size = sizeof(client_addr);
#endif

    int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_size);

    std::cout << "Client connected!\n";

    return {client_fd, server_fd};
}
