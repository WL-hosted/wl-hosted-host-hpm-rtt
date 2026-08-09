/*
 * Minimal newlib syscall stubs for the bare-metal bootloader.
 *
 * Built without --specs=nosys.specs, so newlib expects these symbols to be
 * provided by the application. _sbrk is provided by the HPM SDK
 * (packages/hpm_sdk-v1.10.0/utils/hpm_sbrk.c).
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void _exit(int status)
{
    (void)status;
    for (;;)
        ;
}

int _close(int file)
{
    (void)file;
    return -1;
}

/* _fstat, _read and _write are provided as strong symbols by the HPM SDK
 * debug console component (components/debug_console/hpm_debug_console.c),
 * which routes stdio to the board UART once board_init_console() has run.
 * They used to be stubbed here; do not re-add them or the link will fail with
 * duplicate symbols. */

int _isatty(int file)
{
    (void)file;
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

/* _read/_write are provided as strong symbols by the HPM SDK debug console
 * component (components/debug_console/hpm_debug_console.c), which routes
 * stdio to the board UART once board_init_console() has run. They used to be
 * stubbed here; do not re-add them or the link will fail with duplicate
 * symbols. */

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}
