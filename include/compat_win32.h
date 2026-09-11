#ifndef COMPAT_WIN32_H_INCLUDED
#define COMPAT_WIN32_H_INCLUDED

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <limits.h>

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _TRUNCATE ((size_t)-1)

static inline int strncpy_s(char* dest, size_t destsz, const char* src, size_t count)
{
	if (!dest || destsz == 0) return -1;
	if (!src) { dest[0] = '\0'; return -1; }
	size_t n = count;
	if (n == (size_t)-1 || n >= destsz) n = destsz - 1;
	strncpy(dest, src, n);
	dest[n] = '\0';
	return 0;
}

#define CP_UTF8 65001

static inline int MultiByteToWideChar(unsigned int CodePage, unsigned long dwFlags,
                                      const char* lpMultiByteStr, int cbMultiByte,
                                      wchar_t* lpWideCharStr, int cchWideChar)
{
	(void)CodePage; (void)dwFlags; (void)cbMultiByte;
	if (!lpMultiByteStr) return 0;
	if (cchWideChar == 0) return (int)strlen(lpMultiByteStr) + 1;
	size_t n = mbstowcs(lpWideCharStr, lpMultiByteStr, (size_t)cchWideChar);
	if (n == (size_t)-1) return 0;
	return (int)n + 1;
}

#ifdef __cplusplus
#include <chrono>
#include <thread>
#include <atomic>
#endif

// ---------------------------------------------------------------------------
// Basic Win32 Types
// ---------------------------------------------------------------------------
typedef uint32_t      DWORD;
typedef uint16_t      WORD;
typedef uint8_t       BYTE;
typedef int32_t       BOOL;
typedef int32_t       LONG;
typedef int16_t       SHORT;
typedef int64_t       LONGLONG;
typedef uint64_t      ULONGLONG;
typedef void*         HANDLE;
typedef void*         HMODULE;
typedef void*         HINSTANCE;
typedef void*         HWND;
typedef void*         HDC;
typedef void*         HICON;
typedef void*         HKEY;
typedef void*         LPVOID;
typedef const void*   LPCVOID;
typedef char*         LPSTR;
typedef const char*   LPCSTR;
typedef wchar_t*      LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef intptr_t      LPARAM;
typedef uintptr_t     WPARAM;
typedef intptr_t      LRESULT;

#define TRUE  1
#define FALSE 0
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)

#define WINAPI
#define CALLBACK

typedef union _LARGE_INTEGER {
	struct {
		DWORD LowPart;
		LONG HighPart;
	} u;
	LONGLONG QuadPart;
} LARGE_INTEGER;

// ---------------------------------------------------------------------------
// Critical Sections (Recursive pthread_mutex)
// ---------------------------------------------------------------------------
typedef pthread_mutex_t CRITICAL_SECTION;

static inline void InitializeCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(cs, &attr);
	pthread_mutexattr_destroy(&attr);
}

static inline void EnterCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutex_lock(cs);
}

static inline void LeaveCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutex_unlock(cs);
}

static inline void DeleteCriticalSection(CRITICAL_SECTION* cs)
{
	pthread_mutex_destroy(cs);
}

// ---------------------------------------------------------------------------
// Threads & Synchronization
// ---------------------------------------------------------------------------
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);

#ifdef __cplusplus
enum WinHandleType
{
	HANDLE_TYPE_NONE = 0,
	HANDLE_TYPE_THREAD,
	HANDLE_TYPE_EVENT
};

struct WinHandle
{
	WinHandleType type;
	union {
		struct {
			pthread_t th;
			bool joined;
		} thread;
		struct {
			pthread_mutex_t mtx;
			pthread_cond_t cond;
			bool manual_reset;
			bool signaled;
		} event;
	};
};

struct ThreadShimData
{
	LPTHREAD_START_ROUTINE fn;
	LPVOID param;
};

static inline void* ThreadShimProc(void* p)
{
	// Async, not the pthread default of deferred: TerminateThread() below is
	// pthread_cancel() under the hood, and deferred cancellation only takes
	// effect at a cancellation point (a blocking syscall). A core thread stuck
	// in a CPU-bound loop with no such call never hits one, so the "graceful
	// stop timed out, force it" recovery path silently did nothing - the
	// thread kept running and consuming CPU while CoreShutdown() went on to
	// free the memory and unload the DLL out from under it. TerminateThread
	// is already the last-resort, already-known-to-be-risky escape hatch
	// (same as real Windows TerminateThread); this just makes it actually
	// terminate the thread instead of quietly failing to.
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	ThreadShimData* s = (ThreadShimData*)p;
	LPTHREAD_START_ROUTINE fn = s->fn;
	LPVOID param = s->param;
	delete s;
	fn(param);
	return NULL;
}

static inline HANDLE CreateThread(void* sec, size_t stack, LPTHREAD_START_ROUTINE fn, LPVOID param, DWORD flags, DWORD* id)
{
	(void)sec; (void)stack; (void)flags; (void)id;
	WinHandle* h = new WinHandle();
	h->type = HANDLE_TYPE_THREAD;
	h->thread.joined = false;
	ThreadShimData* s = new ThreadShimData{ fn, param };
	if (pthread_create(&h->thread.th, NULL, ThreadShimProc, s) != 0)
	{
		delete s;
		delete h;
		return NULL;
	}
	return (HANDLE)h;
}

static inline HANDLE CreateEvent(void* sa, BOOL bManualReset, BOOL bInitialState, const char* name)
{
	(void)sa; (void)name;
	WinHandle* h = new WinHandle();
	h->type = HANDLE_TYPE_EVENT;
	pthread_mutex_init(&h->event.mtx, NULL);
	pthread_cond_init(&h->event.cond, NULL);
	h->event.manual_reset = (bManualReset != FALSE);
	h->event.signaled = (bInitialState != FALSE);
	return (HANDLE)h;
}

static inline BOOL SetEvent(HANDLE handle)
{
	if (!handle || (uintptr_t)handle < 4096) return FALSE;
	WinHandle* h = (WinHandle*)handle;
	if (h->type != HANDLE_TYPE_EVENT) return FALSE;
	pthread_mutex_lock(&h->event.mtx);
	h->event.signaled = true;
	if (h->event.manual_reset)
		pthread_cond_broadcast(&h->event.cond);
	else
		pthread_cond_signal(&h->event.cond);
	pthread_mutex_unlock(&h->event.mtx);
	return TRUE;
}

static inline BOOL ResetEvent(HANDLE handle)
{
	if (!handle || (uintptr_t)handle < 4096) return FALSE;
	WinHandle* h = (WinHandle*)handle;
	if (h->type != HANDLE_TYPE_EVENT) return FALSE;
	pthread_mutex_lock(&h->event.mtx);
	h->event.signaled = false;
	pthread_mutex_unlock(&h->event.mtx);
	return TRUE;
}

static inline DWORD WaitForSingleObject(HANDLE handle, DWORD ms)
{
	if (!handle || (uintptr_t)handle < 4096) return WAIT_FAILED;
	WinHandle* h = (WinHandle*)handle;
	if (h->type == HANDLE_TYPE_EVENT)
	{
		pthread_mutex_lock(&h->event.mtx);
		if (h->event.signaled)
		{
			if (!h->event.manual_reset) h->event.signaled = false;
			pthread_mutex_unlock(&h->event.mtx);
			return WAIT_OBJECT_0;
		}
		if (ms == 0)
		{
			pthread_mutex_unlock(&h->event.mtx);
			return WAIT_TIMEOUT;
		}

		int ret = 0;
		if (ms == INFINITE)
		{
			while (!h->event.signaled)
			{
				pthread_cond_wait(&h->event.cond, &h->event.mtx);
			}
		}
		else
		{
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += ms / 1000;
			ts.tv_nsec += (ms % 1000) * 1000000;
			if (ts.tv_nsec >= 1000000000)
			{
				ts.tv_sec += 1;
				ts.tv_nsec -= 1000000000;
			}
			while (!h->event.signaled && ret == 0)
			{
				ret = pthread_cond_timedwait(&h->event.cond, &h->event.mtx, &ts);
			}
		}

		if (h->event.signaled)
		{
			if (!h->event.manual_reset) h->event.signaled = false;
			pthread_mutex_unlock(&h->event.mtx);
			return WAIT_OBJECT_0;
		}
		pthread_mutex_unlock(&h->event.mtx);
		return (ret == ETIMEDOUT) ? WAIT_TIMEOUT : WAIT_FAILED;
	}
	else if (h->type == HANDLE_TYPE_THREAD)
	{
		if (h->thread.joined) return WAIT_OBJECT_0;
		if (ms == INFINITE)
		{
			pthread_join(h->thread.th, NULL);
			h->thread.joined = true;
			return WAIT_OBJECT_0;
		}
		if (ms == 0)
		{
			int tj = pthread_tryjoin_np(h->thread.th, NULL);
			if (tj == 0)
			{
				h->thread.joined = true;
				return WAIT_OBJECT_0;
			}
			return WAIT_TIMEOUT;
		}

		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += ms / 1000;
		ts.tv_nsec += (ms % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000)
		{
			ts.tv_sec += 1;
			ts.tv_nsec -= 1000000000;
		}
		int ret = pthread_timedjoin_np(h->thread.th, NULL, &ts);
		if (ret == 0)
		{
			h->thread.joined = true;
			return WAIT_OBJECT_0;
		}
		if (ret == ETIMEDOUT) return WAIT_TIMEOUT;
		return WAIT_FAILED;
	}
	return WAIT_FAILED;
}

static inline BOOL CloseHandle(HANDLE handle)
{
	if (!handle || (uintptr_t)handle < 4096) return FALSE;
	WinHandle* h = (WinHandle*)handle;
	if (h->type == HANDLE_TYPE_EVENT)
	{
		pthread_mutex_destroy(&h->event.mtx);
		pthread_cond_destroy(&h->event.cond);
	}
	else if (h->type == HANDLE_TYPE_THREAD)
	{
		if (!h->thread.joined)
		{
			pthread_detach(h->thread.th);
		}
	}
	delete h;
	return TRUE;
}

static inline BOOL TerminateThread(HANDLE handle, DWORD exitCode)
{
	(void)exitCode;
	if (!handle || (uintptr_t)handle < 4096) return FALSE;
	WinHandle* h = (WinHandle*)handle;
	if (h->type == HANDLE_TYPE_THREAD)
	{
		return pthread_cancel(h->thread.th) == 0 ? TRUE : FALSE;
	}
	return FALSE;
}

#define THREAD_PRIORITY_TIME_CRITICAL 2
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_NORMAL 0
static inline HANDLE GetCurrentThread(void) { return (HANDLE)(uintptr_t)pthread_self(); }
static inline BOOL SetThreadPriority(HANDLE hThread, int nPriority) { (void)hThread; (void)nPriority; return TRUE; }
#endif

static inline HWND GetForegroundWindow(void) { return (HWND)1; }
static inline DWORD GetCurrentProcessId(void) { return (DWORD)getpid(); }
static inline DWORD GetWindowThreadProcessId(HWND hWnd, DWORD* lpdwProcessId)
{
	(void)hWnd;
	if (lpdwProcessId) *lpdwProcessId = (DWORD)getpid();
	return (DWORD)getpid();
}

#define _fseeki64 fseeko64
#define _ftelli64 ftello64

// ---------------------------------------------------------------------------
// Clocks & Timing
// ---------------------------------------------------------------------------
static inline DWORD GetTickCount()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (DWORD)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static inline ULONGLONG GetTickCount64()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ULONGLONG)ts.tv_sec * 1000ULL + (ULONGLONG)ts.tv_nsec / 1000000ULL;
}

static inline void Sleep(DWORD ms)
{
	usleep(ms * 1000);
}

static inline BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	lpPerformanceCount->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
	return TRUE;
}

static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency)
{
	lpFrequency->QuadPart = 1000000000LL;
	return TRUE;
}

static inline void YieldProcessor()
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
	__builtin_ia32_pause();
#else
	sched_yield();
#endif
}

// ---------------------------------------------------------------------------
// Atomics
// ---------------------------------------------------------------------------
#ifdef __cplusplus
template <typename T, typename U>
static inline T InterlockedExchangeCompat(T volatile* dest, U val)
{
	return __atomic_exchange_n(dest, (T)val, __ATOMIC_SEQ_CST);
}
#define InterlockedExchange(dest, val) InterlockedExchangeCompat((dest), (val))

template <typename T, typename U, typename V>
static inline T InterlockedCompareExchangeCompat(T volatile* dest, U exch, V comp)
{
	T expected = (T)comp;
	__atomic_compare_exchange_n(dest, &expected, (T)exch, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return expected;
}
#define InterlockedCompareExchange(dest, exch, comp) InterlockedCompareExchangeCompat((dest), (exch), (comp))
#else
#define InterlockedExchange(target, val) \
	__atomic_exchange_n((target), (val), __ATOMIC_SEQ_CST)

#define InterlockedCompareExchange(dest, exch, comp) ({ \
	__typeof__(*(dest)) __comp = (comp); \
	__atomic_compare_exchange_n((dest), (void*)&__comp, (exch), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
	__comp; \
})
#endif

#define InterlockedCompareExchange64(dest, exch, comp) InterlockedCompareExchangeCompat((dest), (exch), (comp))
#define InterlockedExchange64(dest, val) InterlockedExchangeCompat((dest), (val))

#define InterlockedIncrement(target) \
	__atomic_add_fetch((target), 1, __ATOMIC_SEQ_CST)

#define InterlockedDecrement(target) \
	__atomic_sub_fetch((target), 1, __ATOMIC_SEQ_CST)

#define InterlockedAdd(target, val) \
	__atomic_add_fetch((target), (val), __ATOMIC_SEQ_CST)

// ---------------------------------------------------------------------------
// Dynamic Library Loading
// ---------------------------------------------------------------------------
#define LoadLibraryA(path) dlopen(path, RTLD_NOW)
#define GetProcAddress(h, name) dlsym(h, name)
#define FreeLibrary(h) dlclose(h)

// ---------------------------------------------------------------------------
// SRWLOCK (Read-Write Locks)
// ---------------------------------------------------------------------------
typedef pthread_rwlock_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
#define AcquireSRWLockExclusive(l) pthread_rwlock_wrlock(l)
#define ReleaseSRWLockExclusive(l) pthread_rwlock_unlock(l)
#define AcquireSRWLockShared(l) pthread_rwlock_rdlock(l)
#define ReleaseSRWLockShared(l) pthread_rwlock_unlock(l)

// ---------------------------------------------------------------------------
// Virtual Key Codes
// ---------------------------------------------------------------------------
#ifndef VK_UP
#define VK_LBUTTON        0x01
#define VK_RBUTTON        0x02
#define VK_MBUTTON        0x04
#define VK_XBUTTON1       0x05
#define VK_XBUTTON2       0x06
#define VK_BACK           0x08
#define VK_TAB            0x09
#define VK_RETURN         0x0D
#define VK_SHIFT          0x10
#define VK_CONTROL        0x11
#define VK_MENU           0x12
#define VK_CAPITAL        0x14
#define VK_ESCAPE         0x1B
#define VK_SPACE          0x20
#define VK_LEFT           0x25
#define VK_UP             0x26
#define VK_RIGHT          0x27
#define VK_DOWN           0x28
#define VK_LWIN           0x5B
#define VK_RWIN           0x5C
#define VK_NUMPAD0        0x60
#define VK_NUMPAD1        0x61
#define VK_NUMPAD2        0x62
#define VK_NUMPAD3        0x63
#define VK_NUMPAD4        0x64
#define VK_NUMPAD5        0x65
#define VK_NUMPAD6        0x66
#define VK_NUMPAD7        0x67
#define VK_NUMPAD8        0x68
#define VK_NUMPAD9        0x69
#define VK_F1             0x70
#define VK_F2             0x71
#define VK_F3             0x72
#define VK_F4             0x73
#define VK_F5             0x74
#define VK_F6             0x75
#define VK_F7             0x76
#define VK_F8             0x77
#define VK_F9             0x78
#define VK_F10            0x79
#define VK_F11            0x7A
#define VK_F12            0x7B
#define VK_F24            0x87
#define VK_LSHIFT         0xA0
#define VK_RSHIFT         0xA1
#define VK_LCONTROL       0xA2
#define VK_RCONTROL       0xA3
#define VK_LMENU          0xA4
#define VK_RMENU          0xA5
#define VK_CLEAR          0x0C
#define VK_PAUSE          0x13
#define VK_PRIOR          0x21
#define VK_NEXT           0x22
#define VK_END            0x23
#define VK_HOME           0x24
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_MULTIPLY       0x6A
#define VK_ADD            0x6B
#define VK_SUBTRACT       0x6D
#define VK_DECIMAL        0x6E
#define VK_DIVIDE         0x6F
#define VK_NUMLOCK        0x90
#define VK_SCROLL         0x91
#define VK_OEM_1          0xBA
#define VK_OEM_PLUS       0xBB
#define VK_OEM_COMMA      0xBC
#define VK_OEM_MINUS      0xBD
#define VK_OEM_PERIOD     0xBE
#define VK_OEM_2          0xBF
#define VK_OEM_3          0xC0
#define VK_OEM_4          0xDB
#define VK_OEM_5          0xDC
#define VK_OEM_6          0xDD
#define VK_OEM_7          0xDE
#endif

// ---------------------------------------------------------------------------
// XInput Types & Constants
// ---------------------------------------------------------------------------
typedef struct _XINPUT_GAMEPAD {
	WORD  wButtons;
	BYTE  bLeftTrigger;
	BYTE  bRightTrigger;
	SHORT sThumbLX;
	SHORT sThumbLY;
	SHORT sThumbRX;
	SHORT sThumbRY;
} XINPUT_GAMEPAD;

typedef struct _XINPUT_STATE {
	DWORD          dwPacketNumber;
	XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000

#ifdef __cplusplus
extern "C" {
#endif
SHORT GetAsyncKeyState(int vk);
#ifdef __cplusplus
}
#endif

static inline DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize)
{
	(void)hModule;
	ssize_t len = readlink("/proc/self/exe", lpFilename, nSize - 1);
	if (len != -1) {
		lpFilename[len] = '\0';
		return (DWORD)len;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Exception Handling Macros
// ---------------------------------------------------------------------------
#define VK_TRY try
#define VK_EXCEPT_ALL catch (...)
#define __try try
#define __except(x) catch (...)
#define EXCEPTION_EXECUTE_HANDLER 1

#endif // !_WIN32

#endif // COMPAT_WIN32_H_INCLUDED
