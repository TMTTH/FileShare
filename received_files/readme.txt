std::string NetworkClient::DiscoverServer()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return {};

    std::string base = "192.168.0.";   // твоя подсеть

    for (int i = 1; i <= 254; i++) {
        std::string ip = base + std::to_string(i);

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            continue;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5555);
        InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr);

        // маленький таймаут, чтобы не тормозило
        int timeout = 200;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            closesocket(s);
            WSACleanup();
            return ip;   // НАШЛИ сервер
        }

        closesocket(s);
    }

    WSACleanup();
    return {};
}
