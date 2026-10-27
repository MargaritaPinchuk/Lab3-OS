#include <windows.h>
#include <process.h>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <clocale>
#include <cstdint>
#include "SharedArray.h"

using namespace std;

static SharedArray* g_shared = nullptr;
static int g_n = 0;
static int g_m = 0;

static CRITICAL_SECTION g_csState;
static CRITICAL_SECTION g_csConsole;

static HANDLE g_hStart = nullptr;
static HANDLE g_hAllStuck = nullptr;

static int g_stuckCount = 0;
static int g_aliveCount = 0;

enum class Action { None, Continue, Terminate };

struct MarkerInfo {
    int    id = 0;
    HANDLE hThread = nullptr;
    HANDLE hResponse = nullptr;
    Action action = Action::None;
    bool   alive = true;
    vector<int> marked;
};

static vector<MarkerInfo> g_markers;

static unsigned __stdcall MarkerProc(void* param)
{
    int self = static_cast<int>(reinterpret_cast<intptr_t>(param));
    MarkerInfo& me = g_markers[self];

    WaitForSingleObject(g_hStart, INFINITE);
    srand(me.id);

    for (;;) {
        int r = rand() % g_n;

        if (g_shared->TryClaim(r, me.id)) {
            Sleep(5);
            me.marked.push_back(r);
            Sleep(5);
            continue;
        }

        {
            EnterCriticalSection(&g_csConsole);
            cout << "[marker " << me.id << "] помечено: " << me.marked.size()
                << ", занят индекс: " << r << endl;
            LeaveCriticalSection(&g_csConsole);
        }

        bool allStuck;
        EnterCriticalSection(&g_csState);
        g_stuckCount++;
        allStuck = (g_stuckCount == g_aliveCount);
        LeaveCriticalSection(&g_csState);
        if (allStuck) SetEvent(g_hAllStuck);

        WaitForSingleObject(me.hResponse, INFINITE);

        if (me.action == Action::Terminate) {
            g_shared->ResetOwned(me.marked);
            break;
        }
    }
    return 0;
}

static void PrintArray()
{
    EnterCriticalSection(&g_csConsole);
    cout << "[ ";
    for (int i = 0; i < g_shared->Size(); ++i) cout << g_shared->Get(i) << " ";
    cout << "]" << endl;
    LeaveCriticalSection(&g_csConsole);
}

static int AskVictimId()
{
    while (true) {
        int id = -1;
        {
            EnterCriticalSection(&g_csConsole);
            cout << "Номер marker-потока для завершения: ";
            if (!(cin >> id)) {
                cin.clear();
                cin.ignore(10000, '\n');
                LeaveCriticalSection(&g_csConsole);
                continue;
            }
            LeaveCriticalSection(&g_csConsole);
        }
        for (auto& mk : g_markers)
            if (mk.alive && mk.id == id) return id;
        cout << "Такого живого потока нет, повторите.\n";
    }
}

static int FindMarkerIndexById(int id)
{
    for (size_t i = 0; i < g_markers.size(); ++i)
        if (g_markers[i].id == id) return static_cast<int>(i);
    return -1;
}

int main()
{
    setlocale(LC_ALL, "");

    cout << "Размерность массива: ";
    if (!(cin >> g_n) || g_n <= 0) {
        cerr << "Некорректный размер массива.\n";
        return 1;
    }
    g_shared = new SharedArray(g_n);

    cout << "Количество потоков marker: ";
    if (!(cin >> g_m) || g_m <= 0) {
        cerr << "Некорректное число потоков.\n";
        delete g_shared;
        return 1;
    }

    InitializeCriticalSection(&g_csState);
    InitializeCriticalSection(&g_csConsole);

    g_hStart = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    g_hAllStuck = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_hStart == nullptr || g_hAllStuck == nullptr) {
        cerr << "CreateEvent failed: " << GetLastError() << "\n";
        if (g_hStart)    CloseHandle(g_hStart);
        if (g_hAllStuck) CloseHandle(g_hAllStuck);
        DeleteCriticalSection(&g_csState);
        DeleteCriticalSection(&g_csConsole);
        delete g_shared;
        return 1;
    }

    g_markers.resize(static_cast<size_t>(g_m));
    g_aliveCount = g_m;

    for (int i = 0; i < g_m; ++i) {
        g_markers[i].id = i + 1;
        g_markers[i].hResponse = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (g_markers[i].hResponse == nullptr) {
            cerr << "CreateEvent(hResponse) failed: " << GetLastError() << "\n";
            return 1;
        }
        unsigned tid = 0;
        g_markers[i].hThread = reinterpret_cast<HANDLE>(_beginthreadex(
            nullptr, 0, MarkerProc,
            reinterpret_cast<void*>(static_cast<intptr_t>(i)), 0, &tid));
        if (g_markers[i].hThread == nullptr) {
            cerr << "_beginthreadex failed\n";
            return 1;
        }
    }

    SetEvent(g_hStart);

    while (g_aliveCount > 0) {
        DWORD wr = WaitForSingleObject(g_hAllStuck, INFINITE);
        if (wr != WAIT_OBJECT_0) break;

        cout << "\nВсе активные потоки (" << g_aliveCount << ") заблокированы:\n";
        PrintArray();

        int victimId = AskVictimId();
        int vIdx = FindMarkerIndexById(victimId);

        g_markers[vIdx].action = Action::Terminate;
        SetEvent(g_markers[vIdx].hResponse);

        WaitForSingleObject(g_markers[vIdx].hThread, INFINITE);
        CloseHandle(g_markers[vIdx].hThread);
        CloseHandle(g_markers[vIdx].hResponse);
        g_markers[vIdx].alive = false;
        g_aliveCount--;

        cout << "Поток " << victimId << " завершён:\n";
        PrintArray();

        EnterCriticalSection(&g_csState);
        g_stuckCount = 0;
        LeaveCriticalSection(&g_csState);

        for (auto& mk : g_markers)
            if (mk.alive) {
                mk.action = Action::Continue;
                SetEvent(mk.hResponse);
            }
    }

    cout << "\nВсе потоки marker завершили работу.\n";

    DeleteCriticalSection(&g_csState);
    DeleteCriticalSection(&g_csConsole);
    CloseHandle(g_hStart);
    CloseHandle(g_hAllStuck);
    delete g_shared;
    return 0;
}