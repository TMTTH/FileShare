std::string NetworkClient::DiscoverServer()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
        return {};

    std::string base = "192.168.0.";   // твоя подсеть
    const int port = 5555;

    for (int i = 1; i <= 254; i++)
    {
        std::string ip = base + std::to_string(i);

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            continue;

        // Делаем сокет неблокирующим
        u_long mode = 1;
        ioctlsocket(s, FIONBIO, &mode);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr);

        connect(s, (sockaddr*)&addr, sizeof(addr));

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(s, &writeSet);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 150000; // 150 мс

        int sel = select(0, nullptr, &writeSet, nullptr, &tv);
        if (sel > 0 && FD_ISSET(s, &writeSet))
        {
            int err = 0;
            int len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);

            if (err == 0) {
                closesocket(s);
                WSACleanup();
                return ip;   // сервер найден
            }
        }

        closesocket(s);
    }

    WSACleanup();
    return {};
}
