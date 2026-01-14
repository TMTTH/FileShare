#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "../include/NetworkServer.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

// Глобальные объекты
NetworkServer* g_pServer = nullptr;
NetworkClient* g_pClient = nullptr;
HWND           g_hListBox = nullptr;
HWND           g_hStatusBar = nullptr;
HWND           g_hEdit_IP = nullptr;
HWND           g_hEdit_Path = nullptr;
HWND           g_hMainWindow = nullptr;
HWND           g_hFileListBox = nullptr;

std::queue<std::string> g_logQueue;
std::mutex              g_logMutex;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

#define WM_UPDATE_LOG (WM_USER + 1)

// Вспомогательные функции GUI
void AddLog(const std::string& message)
{
    if (g_hListBox) {
        SendMessageA(g_hListBox, LB_ADDSTRING, 0, (LPARAM)message.c_str());
        int count = static_cast<int>(SendMessage(g_hListBox, LB_GETCOUNT, 0, 0));
        if (count > 0) {
            SendMessage(g_hListBox, LB_SETCURSEL, static_cast<WPARAM>(count - 1), 0);
        }
    }
}

void AddLogThreadSafe(const std::string& message)
{
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logQueue.push(message);
    }
    if (g_hMainWindow) {
        PostMessageA(g_hMainWindow, WM_UPDATE_LOG, 0, 0);
    }
}

void UpdateStatus(const std::string& status)
{
    if (g_hStatusBar) {
        SendMessageA(g_hStatusBar, SB_SETTEXT, 0, (LPARAM)status.c_str());
    }
}

HWND CreateListBox(HWND hParent, int x, int y, int width, int height)
{
    return CreateWindowA(
        "LISTBOX", "",
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL,
        x, y, width, height,
        hParent, (HMENU)(intptr_t)101, GetModuleHandle(nullptr), nullptr);
}

HWND CreateButton(HWND hParent, const char* text,
    int x, int y, int width, int height, int id)
{
    return CreateWindowA(
        "BUTTON", text,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, width, height,
        hParent, (HMENU)(intptr_t)id, GetModuleHandle(nullptr), nullptr);
}

HWND CreateEdit(HWND hParent, int x, int y, int width, int height, int id)
{
    return CreateWindowA(
        "EDIT", "",
        WS_VISIBLE | WS_CHILD | WS_BORDER,
        x, y, width, height,
        hParent, (HMENU)(intptr_t)id, GetModuleHandle(nullptr), nullptr);
}

HWND CreateStatic(HWND hParent, const char* text,
    int x, int y, int width, int height)
{
    return CreateWindowA(
        "STATIC", text,
        WS_VISIBLE | WS_CHILD,
        x, y, width, height,
        hParent, nullptr, GetModuleHandle(nullptr), nullptr);
}

HWND CreateStatusBar(HWND hParent)
{
    return CreateWindowExA(
        0, STATUSCLASSNAME, "",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        hParent, (HMENU)(intptr_t)102, GetModuleHandle(nullptr), nullptr);
}

// Сеть: потоки

// Отправка файла в отдельном потоке
void SendFileThread(const std::string& ip, const std::string& path)
{
    if (!g_pClient) {
        g_pClient = new NetworkClient();
    }

    AddLogThreadSafe("Начало отправки файла...");
    UpdateStatus("Статус: Отправка файла...");

    if (g_pClient->SendFile(ip, 5555, path)) {
        AddLogThreadSafe("Файл успешно отправлен");
        UpdateStatus("Статус: Файл отправлен");
    }
    else {
        AddLogThreadSafe("Ошибка при отправке файла");
        UpdateStatus("Статус: Ошибка отправки");
    }
}

// Обновление списка файлов на удалённом сервере (по IP из поля)
void RefreshFilesList()
{
    if (!g_hFileListBox) return;

    char ip[64] = { 0 };
    GetWindowTextA(g_hEdit_IP, ip, sizeof(ip));
    if (strlen(ip) == 0) {
        AddLog("Введите IP сервера для обновления списка файлов");
        return;
    }

    if (!g_pClient) {
        g_pClient = new NetworkClient();
    }

    AddLog("Запрос списка файлов у сервера: " + std::string(ip));
    UpdateStatus("Статус: Получение списка файлов...");

    auto files = g_pClient->GetServerFiles(ip, 5555);

    SendMessage(g_hFileListBox, LB_RESETCONTENT, 0, 0);

    for (const auto& f : files) {
        SendMessageA(g_hFileListBox, LB_ADDSTRING, 0, (LPARAM)f.c_str());
    }

    if (files.empty()) {
        AddLog("Список файлов пуст или сервер не ответил");
    }
    else {
        AddLog("Получен список файлов (" + std::to_string(files.size()) + " шт.)");
    }

    UpdateStatus("Статус: Список файлов обновлён");
}

// Диалог выбора папки для скачивания
std::string SelectFolderForDownload(HWND hWnd)
{
    char folderPath[MAX_PATH] = { 0 };

    BROWSEINFOA bi{};
    bi.hwndOwner = hWnd;
    bi.lpszTitle = "Выберите папку для сохранения файла:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr) {
        SHGetPathFromIDListA(pidl, folderPath);
        CoTaskMemFree(pidl);
        return std::string(folderPath);
    }

    return {};
}

// Скачивание файла в отдельном потоке
void DownloadFileThread(const std::string& ip,
    const std::string& filename,
    const std::string& saveFolder)
{
    if (!g_pClient) {
        g_pClient = new NetworkClient();
    }

    AddLogThreadSafe("Начало скачивания с сервера " + ip + ": " + filename);
    UpdateStatus("Статус: Скачивание...");

    std::string fullSavePath = saveFolder + "\\" + filename;

    if (g_pClient->ReceiveFile(ip, 5555, fullSavePath)) {
        AddLogThreadSafe("Файл успешно скачан: " + filename);
        AddLogThreadSafe("Сохранено в: " + fullSavePath);
        UpdateStatus("Статус: Файл скачан");
    }
    else {
        AddLogThreadSafe("Ошибка при скачивании файла: " + filename);
        UpdateStatus("Статус: Ошибка скачивания");
    }
}

// Поиск сервера в локальной сети
void DiscoverServerThread()
{
    if (!g_pClient) {
        g_pClient = new NetworkClient();
    }

    AddLogThreadSafe("Поиск сервера в локальной сети...");
    UpdateStatus("Статус: Поиск сервера...");

    std::string ip = g_pClient->DiscoverServer();
    if (!ip.empty()) {
        AddLogThreadSafe("Найден сервер: " + ip);
        if (g_hEdit_IP) {
            SetWindowTextA(g_hEdit_IP, ip.c_str());
        }
        UpdateStatus("Статус: Сервер найден");
    }
    else {
        AddLogThreadSafe("Сервер не найден");
        UpdateStatus("Статус: Сервер не найден");
    }
}

// WinMain

int WINAPI WinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPSTR     lpCmdLine,
    _In_     int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    InitCommonControls();

    const char CLASS_NAME[] = "FileShareWindow";

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(
        0, CLASS_NAME,
        "FileShare - Локальный файлообменник",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        900, 700,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        return 0;
    }

    g_hMainWindow = hwnd;

    // Левая часть: журнал событий
    CreateStatic(hwnd, "ЖУРНАЛ СОБЫТИЙ", 10, 10, 300, 20);
    g_hListBox = CreateListBox(hwnd, 10, 30, 380, 200);

    // Правая часть: файлы на удалённом сервере
    CreateStatic(hwnd, "ФАЙЛЫ НА СЕРВЕРЕ", 410, 10, 300, 20);
    g_hFileListBox = CreateListBox(hwnd, 410, 30, 470, 200);

    CreateButton(hwnd, "Скачать файл", 410, 235, 130, 25, 3000);
    CreateButton(hwnd, "Удалить файл", 550, 235, 120, 25, 3001);
    CreateButton(hwnd, "Обновить список", 680, 235, 150, 25, 3002);

    // Блок сервера (локальный)
    CreateStatic(hwnd, "СЕРВЕР (этот компьютер)", 10, 270, 300, 20);
    CreateButton(hwnd, "Запустить сервер", 10, 295, 140, 30, 1001);
    CreateButton(hwnd, "Остановить сервер", 160, 295, 140, 30, 1002);
    CreateButton(hwnd, "Открыть папку", 310, 295, 130, 30, 1004);

    // Блок клиента (отправка/получение)
    CreateStatic(hwnd, "КЛИЕНТ: ОТПРАВКА / СКАЧИВАНИЕ", 10, 335, 450, 20);
    CreateStatic(hwnd, "IP сервера:", 10, 360, 100, 20);
    g_hEdit_IP = CreateEdit(hwnd, 110, 360, 120, 25, 2001);
    SetWindowTextA(g_hEdit_IP, "127.0.0.1");
    CreateButton(hwnd, "Найти сервер", 240, 360, 130, 25, 2005);

    CreateStatic(hwnd, "Путь к файлу:", 10, 390, 100, 20);
    g_hEdit_Path = CreateEdit(hwnd, 110, 390, 280, 25, 2002);
    CreateButton(hwnd, "Обзор...", 400, 390, 70, 25, 2003);

    CreateButton(hwnd, "Отправить файл", 10, 420, 150, 30, 2004);
    CreateButton(hwnd, "Очистить журнал", 170, 420, 130, 30, 1003);

    g_hStatusBar = CreateStatusBar(hwnd);

    ShowWindow(hwnd, SW_SHOWNORMAL);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    delete g_pServer;
    g_pServer = nullptr;

    delete g_pClient;
    g_pClient = nullptr;

    return 0;
}

// Обработчик окна

LRESULT CALLBACK WndProc(HWND hWnd,
    UINT  message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message) {
    case WM_UPDATE_LOG: {
        std::lock_guard<std::mutex> lock(g_logMutex);
        while (!g_logQueue.empty()) {
            AddLog(g_logQueue.front());
            g_logQueue.pop();
        }
        return 0;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case 1001: { // Запустить сервер
            if (!g_pServer) {
                g_pServer = new NetworkServer(5555);
            }

            if (!g_pServer->IsRunning() && g_pServer->Start()) {
                std::string msg =
                    "Сервер запущен на порту 5555. Файлы сохраняются в: " +
                    g_pServer->GetDownloadFolder();
                AddLog(msg);
                UpdateStatus("Статус: Сервер запущен");
                RefreshFilesList();
            }
            else if (!g_pServer->IsRunning()) {
                AddLog("Ошибка: не удалось запустить сервер");
                UpdateStatus("Статус: Ошибка запуска");
            }
            else {
                AddLog("Сервер уже запущен");
            }
            break;
        }

        case 1002: { // Остановить сервер
            if (g_pServer) {
                g_pServer->Stop();
                AddLog("Сервер остановлен");
                UpdateStatus("Статус: Сервер остановлен");
                SendMessage(g_hFileListBox, LB_RESETCONTENT, 0, 0);
            }
            else {
                AddLog("Сервер ещё не был запущен");
            }
            break;
        }

        case 1003: { // Очистить журнал
            SendMessage(g_hListBox, LB_RESETCONTENT, 0, 0);
            break;
        }

        case 1004: { // Открыть папку загрузок локального сервера
            if (g_pServer) {
                std::string folderPath = g_pServer->GetDownloadFolder();

                SHELLEXECUTEINFOA sei{};
                sei.cbSize = sizeof(sei);
                sei.fMask = SEE_MASK_NOASYNC;
                sei.hwnd = hWnd;
                sei.lpVerb = "open";
                sei.lpFile = folderPath.c_str();
                sei.nShow = SW_SHOW;

                if (ShellExecuteExA(&sei)) {
                    AddLog("Папка загрузок открыта");
                }
                else {
                    ShellExecuteA(nullptr, "open", "explorer.exe",
                        ("/select,\"" + folderPath + "\"").c_str(),
                        nullptr, SW_SHOW);
                    AddLog("Папка загрузок открыта (через explorer)");
                }
            }
            else {
                AddLog("Сначала запустите сервер");
            }
            break;
        }

        case 2003: { // Обзор файла
            char filename[MAX_PATH] = { 0 };

            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = "Все файлы (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileNameA(&ofn)) {
                SetWindowTextA(g_hEdit_Path, filename);
            }
            break;
        }

        case 2004: { // Отправить файл
            char ip[64] = { 0 };
            char path[MAX_PATH] = { 0 };

            GetWindowTextA(g_hEdit_IP, ip, sizeof(ip));
            GetWindowTextA(g_hEdit_Path, path, sizeof(path));

            if (strlen(ip) == 0 || strlen(path) == 0) {
                AddLog("Ошибка: заполните IP сервера и путь к файлу");
                break;
            }

            AddLog(std::string("Подключение к ") + ip);
            UpdateStatus("Статус: Подключение...");

            std::thread sendThread(SendFileThread,
                std::string(ip),
                std::string(path));
            sendThread.detach();
            break;
        }

        case 2005: { // Найти сервер (UDP discovery)
            std::thread discoverThread(DiscoverServerThread);
            discoverThread.detach();
            break;
        }

        case 3000: { // Скачать файл
            int selected = static_cast<int>(SendMessage(
                g_hFileListBox, LB_GETCURSEL, 0, 0));
            if (selected == LB_ERR) {
                AddLog("Выберите файл для скачивания");
                break;
            }

            char filename[MAX_PATH] = { 0 };
            SendMessageA(g_hFileListBox,
                LB_GETTEXT,
                selected,
                (LPARAM)filename);

            std::string saveFolder = SelectFolderForDownload(hWnd);
            if (saveFolder.empty()) {
                AddLog("Скачивание отменено пользователем");
                break;
            }

            char ip[64] = { 0 };
            GetWindowTextA(g_hEdit_IP, ip, sizeof(ip));
            if (strlen(ip) == 0) {
                AddLog("Введите IP сервера перед скачиванием");
                break;
            }

            std::thread downloadThread(DownloadFileThread,
                std::string(ip),
                std::string(filename),
                saveFolder);
            downloadThread.detach();
            break;
        }

        case 3001: { // Удалить файл на сервере
            int selected = static_cast<int>(SendMessage(
                g_hFileListBox, LB_GETCURSEL, 0, 0));
            if (selected == LB_ERR) {
                AddLog("Выберите файл для удаления");
                break;
            }

            char filename[MAX_PATH] = { 0 };
            SendMessageA(g_hFileListBox,
                LB_GETTEXT,
                selected,
                (LPARAM)filename);

            char ip[64] = { 0 };
            GetWindowTextA(g_hEdit_IP, ip, sizeof(ip));
            if (strlen(ip) == 0) {
                AddLog("Введите IP сервера перед удалением");
                break;
            }

            if (!g_pClient) {
                g_pClient = new NetworkClient();
            }

            AddLog(std::string("Удаление файла на сервере ") + ip + ": " + filename);
            UpdateStatus("Статус: Удаление файла...");

            bool ok = g_pClient->DeleteServerFile(ip, 5555, filename);
            if (ok) {
                AddLog(std::string("Файл удалён на сервере: ") + filename);
                UpdateStatus("Статус: Файл удалён");
                RefreshFilesList();
            }
            else {
                AddLog(std::string("Ошибка при удалении файла: ") + filename);
                UpdateStatus("Статус: Ошибка удаления");
            }
            break;
        }

        case 3002: { // Обновить список файлов
            RefreshFilesList();
            break;
        }

        default:
            break;
        }
        return 0;
    }

    case WM_DESTROY:
        if (g_pServer) {
            g_pServer->Stop();
            delete g_pServer;
            g_pServer = nullptr;
        }
        if (g_pClient) {
            delete g_pClient;
            g_pClient = nullptr;
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
