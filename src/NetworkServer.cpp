#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define CSIDL_DOWNLOADS 0x0019

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shlobj.h>

#include "../include/NetworkServer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

#define DISCOVERY_PORT 55555

namespace
{
    std::string EnsureFolderExists(const std::string& path)
    {
        try {
            std::filesystem::create_directories(path);
            return path;
        }
        catch (...) {
        }

        const std::string fallback = ".\\received_files";
        try {
            std::filesystem::create_directories(fallback);
        }
        catch (...) {
        }
        return fallback;
    }

    // Отправка строки
    bool SendLine(SOCKET s, const std::string& line)
    {
        std::string data = line + "\n";
        const char* buf = data.c_str();
        int total = static_cast<int>(data.size());
        int sent = 0;

        while (sent < total) {
            int n = send(s, buf + sent, total - sent, 0);
            if (n <= 0) return false;
            sent += n;
        }
        return true;
    }

    // Приём строки
    bool RecvLine(SOCKET s, std::string& out)
    {
        out.clear();
        char ch = 0;
        const size_t MAX_LEN = 4096;

        while (true) {
            int n = recv(s, &ch, 1, 0);
            if (n <= 0) return false;

            if (ch == '\n') break;
            if (ch != '\r') out.push_back(ch);

            if (out.size() > MAX_LEN) {
                return false;
            }
        }
        return !out.empty();
    }

    std::vector<std::string> Split(const std::string& s, char delim)
    {
        std::vector<std::string> tokens;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            tokens.push_back(item);
        }
        return tokens;
    }
}

NetworkServer::NetworkServer(int port)
    : m_serverSocket(INVALID_SOCKET)
    , m_port(port)
    , m_isRunning(false)
{
    char downloadPath[MAX_PATH] = { 0 };

    HRESULT hr = SHGetFolderPathA(nullptr, CSIDL_DOWNLOADS, nullptr, 0, downloadPath);
    if (SUCCEEDED(hr) && strlen(downloadPath) > 0) {
        m_downloadFolder = std::string(downloadPath) + "\\FileShare_Received";
    }
    else {
        char currentPath[MAX_PATH] = { 0 };
        GetCurrentDirectoryA(MAX_PATH, currentPath);
        m_downloadFolder = std::string(currentPath) + "\\FileShare_Received";
    }

    m_downloadFolder = EnsureFolderExists(m_downloadFolder);
}

NetworkServer::~NetworkServer()
{
    Stop();
}

std::string NetworkServer::GetDownloadFolder() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloadFolder;
}

void NetworkServer::SetDownloadFolder(const std::string& folder)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_downloadFolder = EnsureFolderExists(folder);
}

bool NetworkServer::Start()
{
    if (m_isRunning.load())
        return true;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_serverSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(m_port);

    if (InetPtonA(AF_INET, "0.0.0.0", &serverAddr.sin_addr) != 1) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    int reuse = 1;
    setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    int bufSize = 1024 * 1024;
    setsockopt(m_serverSocket, SOL_SOCKET, SO_RCVBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(m_serverSocket, SOL_SOCKET, SO_SNDBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    if (bind(m_serverSocket,
        reinterpret_cast<sockaddr*>(&serverAddr),
        sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    if (listen(m_serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    m_isRunning.store(true);

    m_serverThread = std::thread(&NetworkServer::ServerLoop, this);
    m_discoveryThread = std::thread(&NetworkServer::DiscoveryLoop, this);

    return true;
}

void NetworkServer::Stop()
{
    if (!m_isRunning.exchange(false) &&
        m_serverSocket == INVALID_SOCKET &&
        !m_serverThread.joinable() &&
        !m_discoveryThread.joinable()) {
        return;
    }

    if (m_serverSocket != INVALID_SOCKET) {
        shutdown(m_serverSocket, SD_BOTH);
        closesocket(m_serverSocket);
        m_serverSocket = INVALID_SOCKET;
    }

    if (m_serverThread.joinable())
        m_serverThread.join();
    if (m_discoveryThread.joinable())
        m_discoveryThread.join();

    WSACleanup();
}

// UDP discovery
void NetworkServer::DiscoveryLoop()
{
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET)
        return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DISCOVERY_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(udpSocket);
        return;
    }

    int timeout = 500;
    setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<char*>(&timeout), sizeof(timeout));

    char buffer[128]{};

    while (m_isRunning.load()) {
        sockaddr_in from{};
        int fromLen = sizeof(from);

        int len = recvfrom(udpSocket,
            buffer,
            static_cast<int>(sizeof(buffer) - 1),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLen);

        if (!m_isRunning.load())
            break;

        if (len == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT)
                continue;
            break;
        }

        if (len <= 0)
            continue;

        buffer[len] = '\0';
        if (strcmp(buffer, "FILESERVE_DISCOVERY") == 0) {
            const char* reply = "FILESERVE_OK";
            sendto(udpSocket,
                reply,
                static_cast<int>(strlen(reply)),
                0,
                reinterpret_cast<sockaddr*>(&from),
                fromLen);
        }
    }

    closesocket(udpSocket);
}

// TCP-сервер
void NetworkServer::ServerLoop()
{
    while (m_isRunning.load()) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(
            m_serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientAddrLen);

        if (clientSocket == INVALID_SOCKET) {
            if (!m_isRunning.load())
                break;
            continue;
        }

        std::thread clientThread(&NetworkServer::HandleClient, this, clientSocket);
        clientThread.detach();
    }
}

void NetworkServer::HandleClient(SOCKET clientSocket)
{
    std::string line;
    if (!RecvLine(clientSocket, line)) {
        closesocket(clientSocket);
        return;
    }

    auto tokens = Split(line, '|');
    if (tokens.size() < 2 || tokens[0] != "FS1") {
        SendLine(clientSocket, "FS1|ERROR|BadProtocol");
        closesocket(clientSocket);
        return;
    }

    const std::string& cmd = tokens[1];

    if (cmd == "LIST") {
        auto files = GetFilesList();
        std::string resp = "FS1|LIST";
        for (const auto& f : files) {
            resp += "|";
            resp += f;
        }
        SendLine(clientSocket, resp);
    }
    else if (cmd == "GET") {
        if (tokens.size() < 3) {
            SendLine(clientSocket, "FS1|ERROR|NoFilename");
        }
        else {
            std::string filename = tokens[2];
            SendFile(clientSocket, GetFilePath(filename));
        }
    }
    else if (cmd == "PUT") {
        if (tokens.size() < 4) {
            SendLine(clientSocket, "FS1|ERROR|BadPUT");
        }
        else {
            std::string filename = tokens[2];
            long fileSize = 0;
            try {
                fileSize = std::stol(tokens[3]);
            }
            catch (...) {
                SendLine(clientSocket, "FS1|ERROR|BadSize");
                closesocket(clientSocket);
                return;
            }
            ReceiveFile(clientSocket, filename, fileSize);
        }
    }
    else if (cmd == "DEL") {
        if (tokens.size() < 3) {
            SendLine(clientSocket, "FS1|ERROR|NoFilename");
        }
        else {
            std::string filename = tokens[2];
            bool ok = DeleteFile(filename);
            if (ok)
                SendLine(clientSocket, "FS1|DEL|OK");
            else
                SendLine(clientSocket, "FS1|DEL|FAIL");
        }
    }
    else {
        SendLine(clientSocket, "FS1|ERROR|UnknownCommand");
    }

    closesocket(clientSocket);
}

void NetworkServer::SendFile(SOCKET socket, const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        SendLine(socket, "FS1|ERROR|FileNotFound");
        return;
    }

    long fileSize = static_cast<long>(file.tellg());
    file.seekg(0, std::ios::beg);

    if (!SendLine(socket, "FS1|SIZE|" + std::to_string(fileSize))) {
        file.close();
        return;
    }

    std::string line;
    if (!RecvLine(socket, line)) {
        file.close();
        return;
    }

    auto tokens = Split(line, '|');
    if (tokens.size() < 2 || tokens[0] != "FS1" || tokens[1] != "READY") {
        file.close();
        return;
    }

    std::vector<char> buffer(BUFFER_SIZE);
    while (file) {
        file.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0)
            break;

        int offset = 0;
        while (offset < bytesRead) {
            int bytesSent = send(socket,
                buffer.data() + offset,
                static_cast<int>(bytesRead - offset),
                0);
            if (bytesSent <= 0) {
                file.close();
                return;
            }
            offset += bytesSent;
        }
    }

    file.close();
    SendLine(socket, "FS1|DONE");
}

void NetworkServer::ReceiveFile(SOCKET socket,
    const std::string& filename,
    long               fileSize)
{
    std::string savePath = GetFilePath(filename);

    std::ofstream file(savePath, std::ios::binary);
    if (!file.is_open()) {
        SendLine(socket, "FS1|ERROR|CannotOpenFile");
        return;
    }

    file.rdbuf()->pubsetbuf(nullptr, 0);

    if (!SendLine(socket, "FS1|READY")) {
        file.close();
        return;
    }

    std::vector<char> buffer(BUFFER_SIZE);
    long bytesToReceive = fileSize;

    while (bytesToReceive > 0) {
        int chunk = bytesToReceive > BUFFER_SIZE
            ? BUFFER_SIZE
            : static_cast<int>(bytesToReceive);

        int bytesReceived = recv(socket, buffer.data(), chunk, 0);
        if (bytesReceived <= 0) {
            break;
        }

        file.write(buffer.data(), bytesReceived);
        bytesToReceive -= bytesReceived;
    }

    file.flush();
    file.close();

    SendLine(socket, "FS1|DONE");
}

// NetworkClient

bool NetworkClient::SendFile(const std::string& serverAddr,
    int                port,
    const std::string& filePath)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr_{};
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port);

    int bufSize = 1024 * 1024;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    if (InetPtonA(AF_INET, serverAddr.c_str(), &serverAddr_.sin_addr) != 1) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (connect(clientSocket,
        reinterpret_cast<sockaddr*>(&serverAddr_),
        sizeof(serverAddr_)) == SOCKET_ERROR) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    long fileSize = static_cast<long>(file.tellg());
    file.seekg(0, std::ios::beg);

    size_t lastSlash = filePath.find_last_of("\\/");
    std::string filename =
        (lastSlash != std::string::npos)
        ? filePath.substr(lastSlash + 1)
        : filePath;

    std::string cmd = "FS1|PUT|" + filename + "|" + std::to_string(fileSize);
    if (!SendLine(clientSocket, cmd)) {
        file.close();
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    std::string line;
    if (!RecvLine(clientSocket, line)) {
        file.close();
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    auto tokens = Split(line, '|');
    if (tokens.size() < 2 || tokens[0] != "FS1") {
        file.close();
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (tokens[1] == "ERROR") {
        file.close();
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (tokens[1] != "READY") {
        file.close();
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    std::vector<char> buffer(BUFFER_SIZE);
    while (file) {
        file.read(buffer.data(), buffer.size());
        std::streamsize bytesRead = file.gcount();
        if (bytesRead <= 0)
            break;

        int offset = 0;
        while (offset < bytesRead) {
            int bytesSent = send(clientSocket,
                buffer.data() + offset,
                static_cast<int>(bytesRead - offset),
                0);
            if (bytesSent <= 0) {
                file.close();
                closesocket(clientSocket);
                WSACleanup();
                return false;
            }
            offset += bytesSent;
        }
    }

    file.close();

    if (!RecvLine(clientSocket, line)) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    closesocket(clientSocket);
    WSACleanup();
    return true;
}

bool NetworkClient::ReceiveFile(const std::string& serverAddr,
    int                port,
    const std::string& savePath)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr_{};
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port);

    int bufSize = 1024 * 1024;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    if (InetPtonA(AF_INET, serverAddr.c_str(), &serverAddr_.sin_addr) != 1) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (connect(clientSocket,
        reinterpret_cast<sockaddr*>(&serverAddr_),
        sizeof(serverAddr_)) == SOCKET_ERROR) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    size_t lastSlash = savePath.find_last_of("\\/");
    std::string filename =
        (lastSlash != std::string::npos)
        ? savePath.substr(lastSlash + 1)
        : savePath;

    std::string getCommand = "FS1|GET|" + filename;
    if (!SendLine(clientSocket, getCommand)) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    std::string line;
    if (!RecvLine(clientSocket, line)) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    auto tokens = Split(line, '|');
    if (tokens.size() < 2 || tokens[0] != "FS1") {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (tokens[1] == "ERROR") {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (tokens[1] != "SIZE" || tokens.size() < 3) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    long fileSize = 0;
    try {
        fileSize = std::stol(tokens[2]);
    }
    catch (...) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (!SendLine(clientSocket, "FS1|READY")) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    std::ofstream file(savePath, std::ios::binary);
    if (!file.is_open()) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    file.rdbuf()->pubsetbuf(nullptr, 0);

    std::vector<char> buffer(BUFFER_SIZE);
    long bytesToReceive = fileSize;
    long totalBytesReceived = 0;

    while (bytesToReceive > 0) {
        int chunk = bytesToReceive > BUFFER_SIZE
            ? BUFFER_SIZE
            : static_cast<int>(bytesToReceive);
        int bytesReceived = recv(clientSocket, buffer.data(), chunk, 0);
        if (bytesReceived <= 0) {
            break;
        }
        file.write(buffer.data(), bytesReceived);
        totalBytesReceived += bytesReceived;
        bytesToReceive -= bytesReceived;
    }

    file.flush();
    file.close();

    if (!RecvLine(clientSocket, line)) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    closesocket(clientSocket);
    WSACleanup();
    return totalBytesReceived == fileSize;
}

// Удаление файла на сервере
bool NetworkClient::DeleteServerFile(const std::string& serverAddr,
    int                port,
    const std::string& filename)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in serverAddr_{};
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port);

    int bufSize = 1024 * 1024;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF,
        reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));

    if (InetPtonA(AF_INET, serverAddr.c_str(), &serverAddr_.sin_addr) != 1) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    if (connect(clientSocket,
        reinterpret_cast<sockaddr*>(&serverAddr_),
        sizeof(serverAddr_)) == SOCKET_ERROR) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    std::string cmd = "FS1|DEL|" + filename;
    if (!SendLine(clientSocket, cmd)) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    std::string line;
    if (!RecvLine(clientSocket, line)) {
        closesocket(clientSocket);
        WSACleanup();
        return false;
    }

    closesocket(clientSocket);
    WSACleanup();

    auto tokens = Split(line, '|');
    if (tokens.size() < 2 || tokens[0] != "FS1")
        return false;

    if (tokens[1] == "ERROR")
        return false;

    if (tokens[1] != "DEL" || tokens.size() < 3)
        return false;

    return tokens[2] == "OK";
}

// UDP discovery

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

    int broadcast = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<char*>(&broadcast), sizeof(broadcast));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DISCOVERY_PORT);
    addr.sin_addr.s_addr = INADDR_BROADCAST;

    const char* msg = "FILESERVE_DISCOVERY";
    sendto(udpSocket, msg,
        static_cast<int>(strlen(msg)),
        0,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr));

    int timeout = 2000;
    setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<char*>(&timeout), sizeof(timeout));

    char buffer[128]{};
    sockaddr_in from{};
    int fromLen = sizeof(from);

    int len = recvfrom(udpSocket,
        buffer,
        static_cast<int>(sizeof(buffer) - 1),
        0,
        reinterpret_cast<sockaddr*>(&from),
        &fromLen);

    std::string result;
    if (len > 0 && strncmp(buffer, "FILESERVE_OK", 12) == 0) {
        char ip[32]{};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        result = ip;
    }

    closesocket(udpSocket);
    WSACleanup();
    return result;
}

std::vector<std::string> NetworkClient::GetServerFiles(
    const std::string& serverAddr,
    int                port)
{
    std::vector<std::string> files;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return files;

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        return files;
    }

    sockaddr_in serverAddr_{};
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port);
    if (InetPtonA(AF_INET, serverAddr.c_str(), &serverAddr_.sin_addr) != 1) {
        closesocket(clientSocket);
        WSACleanup();
        return files;
    }

    if (connect(clientSocket,
        reinterpret_cast<sockaddr*>(&serverAddr_),
        sizeof(serverAddr_)) == SOCKET_ERROR) {
        closesocket(clientSocket);
        WSACleanup();
        return files;
    }

    if (!SendLine(clientSocket, "FS1|LIST")) {
        closesocket(clientSocket);
        WSACleanup();
        return files;
    }

    std::string line;
    if (!RecvLine(clientSocket, line)) {
        closesocket(clientSocket);
        WSACleanup();
        return files;
    }

    closesocket(clientSocket);
    WSACleanup();

    auto tokens = Split(line, '|');
    if (tokens.size() < 2 || tokens[0] != "FS1")
        return files;

    if (tokens[1] == "ERROR")
        return files;

    if (tokens[1] != "LIST")
        return files;

    for (size_t i = 2; i < tokens.size(); ++i) {
        if (!tokens[i].empty())
            files.push_back(tokens[i]);
    }
    return files;
}

// Файлы на диске

std::vector<std::string> NetworkServer::GetFilesList() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::string> files;
    try {
        for (const auto& entry :
            std::filesystem::directory_iterator(m_downloadFolder)) {
            if (entry.is_regular_file()) {
                files.push_back(entry.path().filename().string());
            }
        }
    }
    catch (...) {
    }
    return files;
}

bool NetworkServer::DeleteFile(const std::string& filename)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    try {
        std::string filePath = m_downloadFolder + "\\" + filename;
        if (std::filesystem::exists(filePath) &&
            std::filesystem::is_regular_file(filePath)) {
            return std::filesystem::remove(filePath);
        }
    }
    catch (...) {
        return false;
    }
    return false;
}

std::string NetworkServer::GetFilePath(const std::string& filename) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloadFolder + "\\" + filename;
}
