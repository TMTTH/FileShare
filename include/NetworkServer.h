#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

// Команды клиента:
//   FS1|LIST
//   FS1|GET|filename
//   FS1|PUT|filename|filesize
//   FS1|DEL|filename
//
// Ответы сервера:
//   FS1|LIST|file1|file2|...
//   FS1|SIZE|<filesize>
//   FS1|READY
//   FS1|DONE
//   FS1|DEL|OK / FS1|DEL|FAIL
//   FS1|ERROR|<message>

class NetworkServer {
public:
    explicit NetworkServer(int port = 5555);
    ~NetworkServer();

    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;

    bool Start();
    void Stop();

    bool IsRunning() const noexcept { return m_isRunning.load(); }
    int  GetPort()   const noexcept { return m_port; }

    std::string GetDownloadFolder() const;
    void SetDownloadFolder(const std::string& folder);

    std::vector<std::string> GetFilesList() const;
    bool        DeleteFile(const std::string& filename);
    std::string GetFilePath(const std::string& filename) const;

private:
    void ServerLoop();      // TCP: обработка клиентов
    void DiscoveryLoop();   // UDP: ответы на broadcast

    void HandleClient(SOCKET clientSocket);
    void SendFile(SOCKET socket, const std::string& filePath);
    void ReceiveFile(SOCKET socket, const std::string& filename, long fileSize);

    SOCKET              m_serverSocket;
    int                 m_port;
    std::atomic_bool    m_isRunning;
    std::thread         m_serverThread;
    std::thread         m_discoveryThread;

    mutable std::mutex  m_mutex;
    std::string         m_downloadFolder;

    static constexpr int BUFFER_SIZE = 65536;
};

// Клиент протокола FS1
class NetworkClient {
public:
    NetworkClient() = default;
    ~NetworkClient() = default;

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    bool SendFile(
        const std::string& serverAddr,
        int                 port,
        const std::string& filePath);

    bool ReceiveFile(
        const std::string& serverAddr,
        int                 port,
        const std::string& savePath);

    // UDP-поиск сервера
    std::string DiscoverServer();

    // Список файлов на сервере
    std::vector<std::string> GetServerFiles(
        const std::string& serverAddr,
        int                 port);

    // Удаление файла на сервере
    bool DeleteServerFile(
        const std::string& serverAddr,
        int                 port,
        const std::string& filename);

private:
    static constexpr int BUFFER_SIZE = 65536;
};
