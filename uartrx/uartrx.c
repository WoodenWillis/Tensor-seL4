/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>

#include <hw/exynos_uart.h>
#include <hw/zumapro_coresight.h>
#include <trace/cmd_ring.h>
#include <trace/console_ring.h>
#include <trace/trace.h>
#include <trace/trace_archive.h>

#include "trace_producer.h"

#define TRACER_CH 1
#define VMM_CH 2

#define VMM_REPLY_TIMEOUT_MS 2000

#define LINE_MAX 128

#define ASCII_BS  0x08
#define ASCII_DEL 0x7f

uintptr_t uart_vaddr;
uintptr_t trace_ring_vaddr;
uintptr_t console_ring_vaddr;
uintptr_t trace_control_vaddr;
uintptr_t cmd_ring_vaddr;

static char line[LINE_MAX];
static size_t line_len;
static uint32_t line_errors;
static bool last_was_cr;
static uint64_t commands_received;

static uint32_t uart_read(uint32_t reg)
{
    return *(volatile uint32_t *)(uart_vaddr + reg);
}

static void console_putc(char c)
{
    struct console_ring *ring = (struct console_ring *)console_ring_vaddr;
    uint64_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);

    while (head - __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE) >= CONSOLE_RING_CAPACITY) {
        microkit_notify(TRACER_CH);
        seL4_Yield();
    }
    ring->buf[head % CONSOLE_RING_CAPACITY] = c;
    __atomic_store_n(&ring->head, head + 1, __ATOMIC_RELEASE);
}

static void console_flush(void)
{
    microkit_notify(TRACER_CH);
}

static void console_puts(const char *s)
{
    while (*s) {
        console_putc(*s++);
    }
}

static void console_puthex32(uint32_t val)
{
    console_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        console_putc("0123456789abcdef"[(val >> shift) & 0xf]);
    }
}

static void console_puthex64(uint64_t val)
{
    console_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        console_putc("0123456789abcdef"[(val >> shift) & 0xf]);
    }
}

static void console_putdec(uint64_t val)
{
    char buf[21];
    int pos = sizeof(buf) - 1;

    buf[pos] = '\0';
    do {
        buf[--pos] = (char)('0' + val % 10);
        val /= 10;
    } while (val != 0);
    console_puts(&buf[pos]);
}

static void prompt(void)
{
    console_puts("> ");
    console_flush();
}

static void log_uart_reg(const char *name, uint32_t reg)
{
    console_puts("UARTRX|INFO: ");
    console_puts(name);
    console_puts(" ");
    console_puthex32(uart_read(reg));
    console_puts("\n");
}

static void log_uart_config(void)
{
    log_uart_reg("ULCON", EXYNOS_UART_ULCON);
    log_uart_reg("UCON", EXYNOS_UART_UCON);
    log_uart_reg("UFCON", EXYNOS_UART_UFCON);
    log_uart_reg("UINTM", EXYNOS_UART_UINTM);
    console_flush();
}

static bool rx_ready(void)
{
    if (uart_read(EXYNOS_UART_UFCON) & EXYNOS_UART_UFCON_FIFOMODE) {
        uint32_t ufstat = uart_read(EXYNOS_UART_UFSTAT);
        return (ufstat & EXYNOS_UART_UFSTAT_RXCOUNT) || (ufstat & EXYNOS_UART_UFSTAT_RXFULL);
    }
    return uart_read(EXYNOS_UART_UTRSTAT) & EXYNOS_UART_UTRSTAT_RXDR;
}

static bool word_is(const char *s, size_t len, const char *word)
{
    size_t i = 0;

    for (; i < len && word[i]; i++) {
        if (s[i] != word[i]) {
            return false;
        }
    }
    return i == len && word[i] == '\0';
}

static size_t token_len(const char *s, size_t len)
{
    size_t n = 0;

    while (n < len && s[n] != ' ') {
        n++;
    }
    return n;
}

static void cmd_help(uint64_t id, uint64_t verb)
{
    console_puts("commands:\n");
    console_puts("  help         this list\n");
    console_puts("  ping         answers pong\n");
    console_puts("  trace-dump   print every trace record recorded so far\n");
    console_puts("  guest-start  start the guest from a fresh image (refused while it runs)\n");
    console_puts("  guest-stop   stop the running guest (refused if none is running)\n");
    console_puts("  status       show whether the guest is running, and why it stopped\n");
    console_puts("  guest-regs   print the guest vCPU's registers (stalls the vCPU's core)\n");
    console_puts("  gic-dump     print every core's GIC redistributor state (debug kernel)\n");
    console_puts("  pc-sample    sample every core's PC, EL and security state via CoreSight\n");
}

static void cmd_ping(uint64_t id, uint64_t verb)
{
    console_puts("pong\n");
}

static uint64_t read_cntpct(void)
{
    uint64_t val;
    asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(val));
    return val;
}

static uint64_t read_cntfrq(void)
{
    uint64_t val;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

static const char *vmm_phase_name(uint64_t phase)
{
    switch (phase) {
    case VMM_PHASE_IDLE:
        return "idle in its event loop";
    case VMM_PHASE_FAULT:
        return "inside fault()";
    case VMM_PHASE_NOTIFIED:
        return "inside notified()";
    default:
        return "in an unknown phase";
    }
}

static void report_vmm_breadcrumb(const struct cmd_ring *ring)
{
    const struct vmm_breadcrumb *crumb = &ring->breadcrumb;
    uint64_t seq = __atomic_load_n(&crumb->seq, __ATOMIC_ACQUIRE);
    uint64_t since = __atomic_load_n(&crumb->since, __ATOMIC_RELAXED);
    uint64_t ms = (read_cntpct() - since) / (read_cntfrq() / 1000);

    console_puts("UARTRX|WARN: VMM breadcrumb: ");
    console_puts(vmm_phase_name(__atomic_load_n(&crumb->phase, __ATOMIC_RELAXED)));
    console_puts(" for ");
    console_putdec(ms);
    console_puts(" ms, label/ch ");
    console_putdec(__atomic_load_n(&crumb->label, __ATOMIC_RELAXED));
    console_puts(" mr0 ");
    console_puthex64(__atomic_load_n(&crumb->mr0, __ATOMIC_RELAXED));
    console_puts(" mr1 ");
    console_puthex64(__atomic_load_n(&crumb->mr1, __ATOMIC_RELAXED));
    console_puts(", seq ");
    console_putdec(seq);
    console_puts(", console waits ");
    console_putdec(__atomic_load_n(&crumb->console_waits, __ATOMIC_RELAXED));
    console_puts("\n");
}

static bool wait_for_vmm(struct cmd_ring *ring, uint64_t id)
{
    uint64_t deadline = read_cntpct() + read_cntfrq() / 1000 * VMM_REPLY_TIMEOUT_MS;

    while (__atomic_load_n(&ring->completed_id, __ATOMIC_ACQUIRE) < id) {
        if (read_cntpct() > deadline) {
            return false;
        }
    }
    return true;
}

static void cmd_to_vmm(uint64_t id, uint64_t verb)
{
    struct cmd_ring *ring = (struct cmd_ring *)cmd_ring_vaddr;
    uint64_t head = __atomic_load_n(&ring->head, __ATOMIC_RELAXED);

    if (head - __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE) >= CMD_RING_CAPACITY) {
        console_puts("UARTRX|ERROR: the VMM has too many commands queued; command dropped\n");
        return;
    }
    ring->entries[head % CMD_RING_CAPACITY] = (struct cmd_entry) { .id = id, .verb = verb };
    __atomic_store_n(&ring->head, head + 1, __ATOMIC_RELEASE);
    microkit_notify(VMM_CH);
    if (!wait_for_vmm(ring, id)) {
        console_puts("UARTRX|WARN: the VMM did not finish command ");
        console_putdec(id);
        console_puts(" within 2 s\n");
        report_vmm_breadcrumb(ring);
    }
}

static uint32_t cs_read(uintptr_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void cs_write(uintptr_t base, uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(base + offset) = val;
}

static uint64_t cs_read64(uintptr_t base, uint32_t offset)
{
    return *(volatile uint64_t *)(base + offset);
}

static void pc_sample_line(uint32_t core, uint64_t pcsr)
{
    console_puts("  core ");
    console_putdec(core);
    console_puts(" PMUPCSR ");
    console_puthex64(pcsr);
    console_puts(" ns ");
    console_putdec(CS_PCSR_NS(pcsr));
    console_puts(" el ");
    console_putdec(CS_PCSR_EL(pcsr));
    console_puts("\n");
}

static void pc_sample_core(uint32_t core)
{
    uintptr_t dbg = CS_DBG_VADDR(core);
    uintptr_t pmu = CS_PMU_VADDR(core);
    uint32_t prsr = cs_read(dbg, CS_DBGPRSR);

    console_puts("core ");
    console_putdec(core);
    console_puts(": DBGPRSR ");
    console_puthex32(prsr);
    console_puts(" MIDR ");
    console_puthex32(cs_read(dbg, CS_MIDR));
    console_puts("\n");
    if (!(prsr & CS_PRSR_POWER_UP) || (prsr & CS_PRSR_RESET_STATE)) {
        console_puts("  powered down or in reset; not sampled\n");
        return;
    }
    cs_write(dbg, CS_DBGLAR, CS_OSLOCK_MAGIC);
    cs_write(dbg, CS_DBGOSLAR, 0);
    cs_write(pmu, CS_DBGLAR, CS_OSLOCK_MAGIC);
    for (uint32_t i = 0; i < CS_PCSR_SAMPLES; i++) {
        (void)cs_read64(pmu, CS_PMUPCSR);
        pc_sample_line(core, cs_read64(pmu, CS_PMUPCSR));
    }
    cs_write(pmu, CS_DBGLAR, CS_LOCK);
}

/* TODO(will): remove pc-sample with gic-dump once the wedged-core problem is understood */
static void cmd_pc_sample(uint64_t id, uint64_t verb)
{
    console_puts("sampling every core's PC through CoreSight (exynos-coresight sequence)\n");
    for (uint32_t core = 0; core < CS_NUM_CORES; core++) {
        pc_sample_core(core);
    }
}

static void cmd_gic_dump(uint64_t id, uint64_t verb)
{
    console_puts("dumping GIC state; the kernel prints it directly\n");
    console_flush();
    seL4_DebugGICDump();
}

static void cmd_trace_dump(uint64_t id, uint64_t verb)
{
    struct trace_control *control = (struct trace_control *)trace_control_vaddr;

    console_puts("dumping trace\n");
    __atomic_fetch_add(&control->dump_requests, 1, __ATOMIC_RELEASE);
}

struct command {
    const char *name;
    uint64_t verb;
    void (*run)(uint64_t id, uint64_t verb);
};

static const struct command commands[] = {
    { "help", TRACE_CMD_VERB_HELP, cmd_help },
    { "ping", TRACE_CMD_VERB_PING, cmd_ping },
    { "trace-dump", TRACE_CMD_VERB_TRACE_DUMP, cmd_trace_dump },
    { "guest-start", TRACE_CMD_VERB_GUEST_START, cmd_to_vmm },
    { "guest-stop", TRACE_CMD_VERB_GUEST_STOP, cmd_to_vmm },
    { "status", TRACE_CMD_VERB_STATUS, cmd_to_vmm },
    { "guest-regs", TRACE_CMD_VERB_GUEST_REGS, cmd_to_vmm },
    { "gic-dump", TRACE_CMD_VERB_GIC_DUMP, cmd_gic_dump },
    { "pc-sample", TRACE_CMD_VERB_PC_SAMPLE, cmd_pc_sample },
};

static const struct command *lookup(const char *verb, size_t len)
{
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (word_is(verb, len, commands[i].name)) {
            return &commands[i];
        }
    }
    return NULL;
}

static void record_command(uint64_t verb, uint64_t id, uint8_t flags)
{
    struct trace_record rec = {
        .addr = verb,
        .value = id,
        .kind = TRACE_KIND_CMD,
        .flags = flags,
    };

    trace_emit(&rec);
}

static void report_line_errors(void)
{
    if (line_errors != 0) {
        console_puts("UARTRX|WARN: receive errors on this line, UERSTAT ");
        console_puthex32(line_errors);
        console_puts("\n");
    }
}

static void run_line(const char *s, size_t len)
{
    while (len > 0 && s[0] == ' ') {
        s++;
        len--;
    }
    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    if (len == 0) {
        return;
    }

    uint64_t id = ++commands_received;
    const struct command *cmd = lookup(s, token_len(s, len));

    if (cmd == NULL) {
        record_command(TRACE_CMD_VERB_NONE, id, TRACE_CMD_REJECTED_VERB);
        console_puts("unknown command; type help\n");
        return;
    }
    record_command(cmd->verb, id, TRACE_CMD_ACCEPTED);
    cmd->run(id, cmd->verb);
}

static void end_line(void)
{
    console_puts("\n");
    report_line_errors();
    run_line(line, line_len);
    line_len = 0;
    line_errors = 0;
    prompt();
}

static void erase_char(void)
{
    if (line_len > 0) {
        line_len--;
        console_puts("\b \b");
        console_flush();
    }
}

static void add_char(char c)
{
    if (line_len == LINE_MAX) {
        return;
    }
    line[line_len++] = c;
    console_putc(c);
    console_flush();
}

static void rx_byte(uint8_t c, uint32_t uerstat)
{
    bool after_cr = last_was_cr;

    last_was_cr = (c == '\r');
    line_errors |= uerstat;
    if (c == '\n' && after_cr) {
        return;
    }
    if (c == '\r' || c == '\n') {
        end_line();
    } else if (c == ASCII_BS || c == ASCII_DEL) {
        erase_char();
    } else if (c >= 0x20 && c < 0x7f) {
        add_char((char)c);
    }
}

static void rx_poll_forever(void)
{
    for (;;) {
        while (rx_ready()) {
            uint32_t uerstat = uart_read(EXYNOS_UART_UERSTAT);
            rx_byte((uint8_t)(uart_read(EXYNOS_UART_URXH) & 0xff), uerstat);
        }
    }
}

void init(void)
{
    trace_producer_init(trace_ring_vaddr, TRACER_CH, TRACE_PRODUCER_UARTRX);
    log_uart_config();
    prompt();
    rx_poll_forever();
}

void notified(microkit_channel ch)
{
}
