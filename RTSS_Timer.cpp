#include <windows.h>
#include "RTSSSharedMemory.h"
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/NODEFAULTLIB")
#pragma comment(linker, "/INCREMENTAL:NO")

#define UniqueMapName "RTSSSharedMemory_RTSS_Timer"

typedef struct {
	HANDLE hMapFile;
	LPRTSS_SHARED_MEMORY pMem;
} THREAD_PARAM;

THREAD_PARAM param;

LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI ThreadProc(LPVOID param);
BOOL UpdateOSD(LPCSTR lpText);
void ReleaseOSD();
BOOL GetOSD(char** Dest, LPRTSS_SHARED_MEMORY pMem);
void UpdateOSDEx(LPCSTR lpText, const LPRTSS_SHARED_MEMORY pMem, char *OSD);

void WinMainCRTStartup()
{
	LPCTSTR pClassName = TEXT("RTSS_Timer");
	HINSTANCE hinst;
	MSG msg;
	HWND hwnd;

	WNDCLASSEX wc;

	hinst = GetModuleHandle(NULL);

	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = WindowProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hinst;
	wc.hIcon         = NULL;
	wc.hIconSm       = NULL;
	wc.hCursor       = LoadCursor(NULL,IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = pClassName;

	if (!RegisterClassEx(&wc)) return;

	hwnd = CreateWindow(pClassName, TEXT("Timer for RTSS"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		300, GetSystemMetrics(SM_CYMIN) + 26,
		NULL, NULL, hinst, NULL);
	if (!hwnd) return;

	ShowWindow(hwnd, SW_SHOW);

	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return;
}

DWORD WINAPI ThreadProc(LPVOID param)
{
	HANDLE hMapFile = ((THREAD_PARAM*)param)->hMapFile;
	LPRTSS_SHARED_MEMORY pMem = ((THREAD_PARAM*)param)->pMem;
	char *OSD;
	GetOSD(&OSD, pMem);
	
	DWORD st, dt;
	TCHAR szTime[256];

	while (TRUE) {
		UpdateOSDEx("00:00:00.000", pMem, OSD);
		
		while (!GetAsyncKeyState(VK_DECIMAL) || GetAsyncKeyState(VK_NUMPAD0)) {
			Sleep(16);
		}
		st = timeGetTime(); // ‚±‚êˆÈã‚Ì¸“x‚ð‹‚ß‚élŠÔ‚Í‚¢‚È‚¢

		do {
			Sleep(16);
			dt = timeGetTime() - st;
			wsprintf(szTime, TEXT("%.2d:%.2d:%.2d.%.3d"),
				dt / (1000 * 60 * 60),
				dt / (1000 * 60) % 60,
				dt / 1000 % 60,
				dt % 1000
			);
			UpdateOSDEx(szTime, pMem, OSD);
		} while (!GetAsyncKeyState(VK_NUMPAD0) || GetAsyncKeyState(VK_DECIMAL));
		
		while (!GetAsyncKeyState(VK_DECIMAL) || !GetAsyncKeyState(VK_NUMPAD0)) {
			Sleep(16);
		}
		
		UpdateOSDEx("--:--:--.---", pMem, OSD);
		Sleep(500);
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_DESTROY:
		ReleaseOSD();
		ExitProcess(0);
		break;

	case WM_CREATE:
		CreateWindow(
			TEXT("EDIT"), TEXT("BETA TEST VERSION"),
			WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_READONLY,
			2, 2, 275, 22,
			hwnd, (HMENU)1,
			((LPCREATESTRUCT)(lp))->hInstance, NULL
		);
		
		param.hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, TEXT("RTSSSharedMemoryV2"));
		if (NULL == param.hMapFile)
		{
			return 0;
		}
		param.pMem = (LPRTSS_SHARED_MEMORY)MapViewOfFile(param.hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if (NULL == param.pMem)
		{
			CloseHandle(param.hMapFile);
			return 0;
		}
		
		DWORD dwThreadId;
		CreateThread(NULL, 0, ThreadProc, (void*)&param, 0, &dwThreadId);
		break;

	default:
		return DefWindowProc(hwnd, msg, wp, lp);
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////
BOOL UpdateOSD(LPCSTR lpText)
{
	BOOL bResult	= FALSE;

	HANDLE hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, TEXT("RTSSSharedMemoryV2"));

	if (hMapFile)
	{
		LPVOID pMapAddr				= MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		LPRTSS_SHARED_MEMORY pMem	= (LPRTSS_SHARED_MEMORY)pMapAddr;

		if (pMem)
		{
			if ((pMem->dwSignature == 'RTSS') && 
				(pMem->dwVersion >= 0x00020000))
			{
				for (DWORD dwPass=0; dwPass<2; dwPass++)
					//1st pass : find previously captured OSD slot
					//2nd pass : otherwise find the first unused OSD slot and capture it
				{
					for (DWORD dwEntry=1; dwEntry<pMem->dwOSDArrSize; dwEntry++)
						//allow primary OSD clients (i.e. EVGA Precision / MSI Afterburner) to use the first slot exclusively, so third party
						//applications start scanning the slots from the second one
					{
						RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry = (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)((LPBYTE)pMem + pMem->dwOSDArrOffset + dwEntry * pMem->dwOSDEntrySize);

						if (dwPass)
						{
							if (!lstrlen(pEntry->szOSDOwner))
								lstrcpy(pEntry->szOSDOwner, UniqueMapName);
						}

						if (!lstrcmp(pEntry->szOSDOwner, UniqueMapName))
						{
							if (pMem->dwVersion >= 0x00020007)
								//use extended text slot for v2.7 and higher shared memory, it allows displaying 4096 symbols
								//instead of 256 for regular text slot
								lstrcpyn(pEntry->szOSDEx, lpText, sizeof(pEntry->szOSDEx) - 1);
							else
								lstrcpyn(pEntry->szOSD, lpText, sizeof(pEntry->szOSD) - 1);

							pMem->dwOSDFrame++;

							bResult = TRUE;

							break;
						}
					}

					if (bResult)
						break;
				}
			}

			UnmapViewOfFile(pMapAddr);
		}

		CloseHandle(hMapFile);
	}

	return bResult;
}
/////////////////////////////////////////////////////////////////////////////
void ReleaseOSD()
{
	HANDLE hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, TEXT("RTSSSharedMemoryV2"));

	if (hMapFile)
	{
		LPVOID pMapAddr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

		LPRTSS_SHARED_MEMORY pMem = (LPRTSS_SHARED_MEMORY)pMapAddr;

		if (pMem)
		{
			if ((pMem->dwSignature == 'RTSS') && 
				(pMem->dwVersion >= 0x00020000))
			{
				for (DWORD dwEntry=1; dwEntry<pMem->dwOSDArrSize; dwEntry++)
				{
					RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry = (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)((LPBYTE)pMem + pMem->dwOSDArrOffset + dwEntry * pMem->dwOSDEntrySize);

					if (!lstrcmp(pEntry->szOSDOwner, UniqueMapName))
					{
						SecureZeroMemory(pEntry, pMem->dwOSDEntrySize);
						pMem->dwOSDFrame++;
					}
				}
			}

			UnmapViewOfFile(pMapAddr);
		}

		CloseHandle(hMapFile);
	}
}
/////////////////////////////////////////////////////////////////////////////
BOOL GetOSD(char** Dest, LPRTSS_SHARED_MEMORY pMem)
{
	BOOL bResult	= FALSE;

	if (pMem)
	{
		if ((pMem->dwSignature == 'RTSS') && 
			(pMem->dwVersion >= 0x00020000))
		{
			for (DWORD dwPass=0; dwPass<2; dwPass++)
				//1st pass : find previously captured OSD slot
				//2nd pass : otherwise find the first unused OSD slot and capture it
			{
				for (DWORD dwEntry=1; dwEntry<pMem->dwOSDArrSize; dwEntry++)
					//allow primary OSD clients (i.e. EVGA Precision / MSI Afterburner) to use the first slot exclusively, so third party
					//applications start scanning the slots from the second one
				{
					RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry = (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)((LPBYTE)pMem + pMem->dwOSDArrOffset + dwEntry * pMem->dwOSDEntrySize);

					if (dwPass)
					{
						if (!lstrlen(pEntry->szOSDOwner))
							lstrcpy(pEntry->szOSDOwner, UniqueMapName);
					}

					if (!lstrcmp(pEntry->szOSDOwner, UniqueMapName))
					{
						if (pMem->dwVersion >= 0x00020007)
							//use extended text slot for v2.7 and higher shared memory, it allows displaying 4096 symbols
							//instead of 256 for regular text slot
							*Dest = pEntry->szOSDEx;
						else
							*Dest = pEntry->szOSD;

						bResult = TRUE;

						break;
					}
				}

				if (bResult)
					break;
			}
		}

	}

	return bResult;
}
/////////////////////////////////////////////////////////////////////////////
void UpdateOSDEx(LPCSTR lpText, const LPRTSS_SHARED_MEMORY pMem, char *OSD)
{
	lstrcpyn(OSD, lpText, 255);
	pMem->dwOSDFrame++;
	FlushViewOfFile(pMem, 0);
}
