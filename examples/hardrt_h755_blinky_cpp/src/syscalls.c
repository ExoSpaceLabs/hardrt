// vendor/stm32/Templates/syscalls.c
// Minimal newlib syscalls for bare-metal STM32H7 (no OS, no semihosting).
// SPDX-License-Identifier: Apache-2.0

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdint.h>

__attribute__((weak)) void _exit(int status) { (void)status; for(;;){} }
__attribute__((weak)) int  _close(int fd)    { (void)fd; errno = EBADF; return -1; }
__attribute__((weak)) int  _fstat(int fd, struct stat *st)
{ (void)fd; if (st) st->st_mode = S_IFCHR; return 0; }
__attribute__((weak)) int  _isatty(int fd)   { (void)fd; return 1; }
__attribute__((weak)) off_t _lseek(int fd, off_t ptr, int dir)
{ (void)fd; (void)ptr; (void)dir; errno = ESPIPE; return (off_t)-1; }
__attribute__((weak)) int  _read(int fd, char *buf, int cnt)
{ (void)fd; (void)buf; (void)cnt; errno = EAGAIN; return -1; }
__attribute__((weak)) int  _write(int fd, const char *buf, int cnt)
{ (void)fd; (void)buf; return cnt; } // pretend it all wrote

// Some newlib builds weakly reference these. No-ops are fine on bare metal.
__attribute__((weak)) void _init(void) {}
__attribute__((weak)) void _fini(void) {}
