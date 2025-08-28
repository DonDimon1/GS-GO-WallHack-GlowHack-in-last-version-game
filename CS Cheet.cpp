#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include <thread>

// Оффсеты. CS:GO 32-битная, поэтому всё на DWORD, хоть компилятор и ругается
const DWORD dwLocalPlayer = 0xDEF97C;               // список игроков
const DWORD dwEntityList = 0x4E051DC;               // локальный игрок
const DWORD m_iTeamNum = 0xF4;                      // команда
const DWORD m_iGlowIndex = 0x10488;                 // индекс glow
const DWORD dwGlowObjectManager = 0x535FCB8;        // glow manager

HANDLE process;                                     // непосредственно сам процесс CSGO
DWORD clientBase;                                   // для работы с client_panorama.dll
DWORD engineBase;                                   // для работы с engine.dll
std::atomic<bool> isWorkWH(false);                  // Флаг работы WH


// Получение базового адреса модуля dll
DWORD GetModuleBaseAddress(DWORD pid, const wchar_t* moduleName) {
    // Создаём снимок процесса
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);    
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "\n\n\n--------------------ERROR--------------------\n";
        std::cerr << "snapshot == INVALID_HANDLE_VALUE method:GetModuleBaseAddress\n";
        return 0;
    }

    MODULEENTRY32W mEntry;                            // Структура для хранения информации о процессе
    mEntry.dwSize = sizeof(mEntry);
    if (Module32FirstW(snapshot, &mEntry)) {          // Получаем информацию о первом процессе
        do {
            if (!_wcsicmp(mEntry.szModule, moduleName)) {
                CloseHandle(snapshot);
                return reinterpret_cast<DWORD>(mEntry.modBaseAddr);
            }
        } while (Module32NextW(snapshot, &mEntry));
    }

    std::cout << "Don't find module\n";
    CloseHandle(snapshot);
    return 0;
}

// Читаем данные из памяти
template <typename T>
T readMem(DWORD address)
{
    T buffer;
    if (!ReadProcessMemory(process, reinterpret_cast<LPVOID>(address), &buffer, sizeof(buffer), 0))
        std::cerr << "ReadProcessMemory error; address: " << std::hex << address << ", Error: " << GetLastError() << "\n";
    return buffer;
}

// Записываем данные в память
template <typename T>
void writeMem(DWORD address, T value)
{
    if (!WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), &value, sizeof(value), 0))
        std::cerr << "WriteProcessMemory error; address: " << std::hex << address << ", Error: " << GetLastError() << "\n";
}

// Функция чита
void wallhack()
{
    while (true) // создаем бесконечный цикл
    {
        Sleep(10); // таймаут 10 мс, чтобы не грузить процессор под 100
        if (!isWorkWH)
            continue;

        DWORD glowObj = readMem<DWORD>(clientBase + dwGlowObjectManager);                               // адрес объекта glowObj
        DWORD myTeam = readMem<DWORD>(readMem<DWORD>(clientBase + dwLocalPlayer) + m_iTeamNum);         // адрес команды локального игрока

        for (int playerIndex = 0; playerIndex < 32; ++playerIndex)                                      // Обход всех игроков
        {
            DWORD currentPlayer = readMem<DWORD>(clientBase + dwEntityList + playerIndex * 0x10);       // Текущий игрок
            if (currentPlayer == 0)
                continue;

            bool dormant = readMem<bool>(currentPlayer + 0xED);                                         // спектатор
            if (dormant)
                continue;

            DWORD team = readMem<DWORD>(currentPlayer + m_iTeamNum);                                    // тиммейт
            if (team != 2 && team != 3)
                continue;

            DWORD currentGlowIndex = readMem<DWORD>(currentPlayer + m_iGlowIndex);                      // текущий Glow индекс игрока

            if (team == myTeam) // если игрок тиммейт
            {
                // делаем его обводку синим
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0x8, 0); // red
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0xC, 0); // green
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0x10, 1.0f); // blue 255
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0x14, 1.0f);
                writeMem<bool>(glowObj + currentGlowIndex * 0x38 + 0x28, true);
                writeMem<bool>(glowObj + currentGlowIndex * 0x38 + 0x29, false);
            }
            else // если игрок не тиммейт
            {
                // делаем его обводку красным
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0x8, 1.0f); // red
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0xC, 0); // green
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0x10, 0); // blue
                writeMem<float>(glowObj + currentGlowIndex * 0x38 + 0x14, 1.0f);
                writeMem<bool>(glowObj + currentGlowIndex * 0x38 + 0x28, true);
                writeMem<bool>(glowObj + currentGlowIndex * 0x38 + 0x29, false);
            }
        }
    }
}

int main() {
    std::cout << "Start WH CS:GO\n"; // выводим в консоль сообщение о том, что надо открыть ксго
    HWND hwnd;
    DWORD pid = 0;

    // -----------------------
    // Поиск и запуск CS:GO
    // -----------------------
    short tryToFindCSGO = 0;
    short finishToFindCSGO = 5;
    do {
        std::cout << "\nTry to find CS:GO. Attempt: " << ++tryToFindCSGO << ":" << finishToFindCSGO << "\n";
        hwnd = FindWindowA(0, "Counter-Strike: Global Offensive - Direct3D 9"); // Ищем ксго, если находим - выходим из цикла
        Sleep(1000);                                                            // Таймаут (чтобы не грузить процессор)
        std::cout << "Need to run the game.\n";
    } while (!hwnd && tryToFindCSGO != finishToFindCSGO);

    if (hwnd) {
        std::cout << "\nTry find pid\n";
        GetWindowThreadProcessId(hwnd, &pid);                                   // Получаем id приложения
    }
    if (pid)
        std::cout << "CS:GO founded, pid: " << pid << "\n";
    else {
        std::cerr << "\n\n\n--------------------ERROR--------------------\n";
        std::cout << "CS:GO not find; Enter pid the process manually\n";
        std::cin >> pid;
    }

    std::cout << "\nTry open CS:GO process\n";
    process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);                      // Заходим в кс го по его id
    if (process == NULL) {
        std::cerr << "\n\n\n--------------------ERROR--------------------\n";
        std::cerr << "Error open process: " << GetLastError() << "\n";
        std::cerr << "Possible errors:\nIf error code = 5 -> ERROR_ACCESS_DENIED\nIf error code = 87 -> ERROR_INVALID_PARAMETER";
        std::cerr << ", most likely don't find pid the process CS:GO\n";
        std::cerr << "Else other error -> check MSDN.\n";
        return 1;
    }
    std::cout << "CS:GO started, pid: " << pid << ".\n\n\n";                    // выводим сообщение о том, что ксго запущена


    // -----------------------
    // Поиск модулей
    // -----------------------
    std::cout << "Start find module \"client.dll\"\n";
    do {
        clientBase = GetModuleBaseAddress(pid, L"client.dll"); // ищем клиент кс го // Раньше было client_panorama.dll
        Sleep(100);
    } while (!clientBase);
    std::cout << "Successfully found module: client.dll\n\nStart find module \"engine.dll\"\n";

    do {
        engineBase = GetModuleBaseAddress(pid, L"engine.dll"); // ищем движок кс го
        Sleep(100);
    } while (!engineBase);
    std::cout << "Successfully found module: engine.dll\n\n\n\n";


    // -----------------------
    // Запуск WH
    // -----------------------
    std::cout << "########################################################################\n";
    std::cout << "START WH\nPress F9 to ON or OFF WH\n";
    std::thread whThread(wallhack);                             // Запуск потока WH

    while (true)
    {
        if (GetAsyncKeyState(VK_F9))
        {
            isWorkWH.store(!isWorkWH.load());
            if (isWorkWH)
                std::cout << "WH: ON\n";
            else
                std::cout << "WH: OFF\n";
            Sleep(200);                                         // Чтобы не просчитывало  несколько раз за нажатие
        }


        Sleep(200);                                             //Уменьшили нагрузку...
    }

    whThread.join();
    return 0;
}
