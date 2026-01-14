std::string NetworkClient::DiscoverServer()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return {};

    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return {};
    }

    // Разрешаем broadcast
    int broadcast = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST,
        (char*)&broadcast, sizeof(broadcast));

    // ОБЯЗАТЕЛЬНО биндим сокет
    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0);              // любой свободный порт
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpSocket, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return {};
    }

    // Адрес широковещания
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DISCOVERY_PORT);
    addr.sin_addr.s_addr = INADDR_BROADCAST;

    const char* msg = "FILESERVE_DISCOVERY";
    sendto(udpSocket, msg, (int)strlen(msg), 0,
        (sockaddr*)&addr, sizeof(addr));

    // Таймаут ожидания ответа
    int timeout = 3000;
    setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO,
        (char*)&timeout, sizeof(timeout));

    char buffer[128]{};
    sockaddr_in from{};
    int fromLen = sizeof(from);

    int len = recvfrom(udpSocket, buffer, sizeof(buffer) - 1, 0,
        (sockaddr*)&from, &fromLen);

    std::string result;
    if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "FILESERVE_OK") == 0) {
            char ip[32]{};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            result = ip;
        }
    }

    closesocket(udpSocket);
    WSACleanup();
    return result;
}

SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

int reuse = 1;
setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR,
    (char*)&reuse, sizeof(reuse));
