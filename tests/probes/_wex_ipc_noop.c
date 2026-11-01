/* _wex_ipc_noop.c - trivial target that exits 0; used as the child image for token-launch tests. */
#include <windows.h>
int main(void) { Sleep(50); return 0; }
