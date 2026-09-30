/* SPDX-License-Identifier: BSD-2-Clause */

/* asm-generic/unistd.h */
#define SYS_write 64
#define SYS_ppoll 73

void _start(void);

static long syscall5(long nr, long a0, long a1, long a2, long a3, long a4)
{
    register long x8 asm("x8") = nr;
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;

    asm volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return x0;
}

static void write_stdout(const char *s, long len)
{
    syscall5(SYS_write, 1, (long)s, len, 0, 0);
}

static void wait_forever(void)
{
    for (;;) {
        syscall5(SYS_ppoll, 0, 0, 0, 0, 0);
    }
}

void _start(void)
{
    static const char msg[] = "init: hello from userspace\n";

    write_stdout(msg, sizeof(msg) - 1);
    wait_forever();
}
