// The only file that touches the Windows API (windows.h defines macros that would clash with the sketch).
#define _WIN32_WINNT 0x0601   // GetTickCount64
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>

// Non-blocking read from stdin when it is a pipe. Returns bytes read, 0 if nothing is waiting, -1 on EOF/error.
int osReadStdin(char* buf, int max) {
  HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
  DWORD avail = 0;
  if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) return -1;
  if (avail == 0) return 0;
  DWORD n = 0;
  if (!ReadFile(h, buf, avail < (DWORD)max ? avail : (DWORD)max, &n, NULL)) return -1;
  return (int)n;
}

unsigned long osNowMs() { return (unsigned long)GetTickCount64(); }
void osSleepMs(unsigned ms) { Sleep(ms); }
void osBinaryStdout() { _setmode(_fileno(stdout), _O_BINARY); }
