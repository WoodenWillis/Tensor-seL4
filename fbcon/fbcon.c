/* SPDX-License-Identifier: BSD-2-Clause */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <sddf/util/cache.h>

#include <hw/zumapro_dpu.h>
#include <trace/console_ring.h>

#include "fbcon_font.h"

#define TRACER_CH 1

#define SCALE 1u
#define CELL_W (FONT_W * SCALE)
#define CELL_H (FONT_H * SCALE)
#define FG 0xffffffffu
#define BG 0xff000000u
#define TAB_WIDTH 4u
#define RUN_STATUS_SPINS 2000000u

#define MAX_COLS 160u
#define MAX_ROWS 360u

uintptr_t decon_vaddr;
uintptr_t rdma_vaddr;
uintptr_t fb_vaddr;
uintptr_t fbcon_ring_vaddr;

static uint32_t stride;
static uint32_t width;
static uint32_t height;
static uint32_t cols;
static uint32_t rows;
static uint32_t cur_col;
static uint32_t cur_row;
static bool ready;
static bool scrolled;
static char grid[MAX_ROWS][MAX_COLS];
static bool dirty[MAX_ROWS];

static uint32_t reg_read(uintptr_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void reg_write(uintptr_t base, uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(base + offset) = val;
}

static void log_hex(const char *msg, uint64_t val)
{
    static const char hex[] = "0123456789abcdef";
    char buf[19] = "0x";

    for (int i = 0; i < 16; i++) {
        buf[2 + i] = hex[(val >> (60 - 4 * i)) & 0xf];
    }
    buf[18] = '\0';
    microkit_dbg_puts(msg);
    microkit_dbg_puts(buf);
    microkit_dbg_puts("\n");
}

static bool read_geometry(void)
{
    uint32_t img = reg_read(rdma_vaddr, RDMA_IMG_SIZE);
    uint32_t base = reg_read(rdma_vaddr, RDMA_BASEADDR_P0);

    width = RDMA_IMG_SIZE_W(img);
    height = RDMA_IMG_SIZE_H(img);
    stride = reg_read(rdma_vaddr, RDMA_SRC_STRIDE_0) & RDMA_SRC_STRIDE_MASK;
    log_hex("FBCON|INFO: RDMA0 BASEADDR_P0 ", base);
    log_hex("FBCON|INFO: RDMA0 IMG_SIZE ", img);
    log_hex("FBCON|INFO: RDMA0 SRC_STRIDE_0 ", stride);
    if (base != ABL_FB_PHYS) {
        log_hex("FBCON|ERROR: scanout is not the mapped framebuffer at ", ABL_FB_PHYS);
        return false;
    }
    if (width == 0 || height == 0 || stride < width * 4 || stride % 4 != 0) {
        microkit_dbg_puts("FBCON|ERROR: RDMA0 geometry is not a 32-bit framebuffer\n");
        return false;
    }
    if ((uint64_t)stride * height > ABL_FB_SIZE) {
        log_hex("FBCON|ERROR: framebuffer is larger than the mapped ", ABL_FB_SIZE);
        return false;
    }
    cols = width / CELL_W;
    rows = height / CELL_H;
    if (cols > MAX_COLS || rows > MAX_ROWS) {
        microkit_dbg_puts("FBCON|ERROR: text grid exceeds MAX_COLS x MAX_ROWS\n");
        return false;
    }
    return true;
}

static volatile uint32_t *pixel_row(uint32_t y)
{
    return (volatile uint32_t *)(fb_vaddr + (uintptr_t)y * stride);
}

static const uint32_t *glyph_of(char ch)
{
    unsigned char c = (unsigned char)ch;

    return font_rows[(c >= FONT_FIRST && c <= FONT_LAST) ? c - FONT_FIRST : 0];
}

static void draw_glyph(uint32_t col, uint32_t row, char ch)
{
    const uint32_t *glyph = glyph_of(ch);

    for (uint32_t gy = 0; gy < FONT_H; gy++) {
        for (uint32_t sy = 0; sy < SCALE; sy++) {
            volatile uint32_t *px = pixel_row(row * CELL_H + gy * SCALE + sy) + col * CELL_W;
            for (uint32_t gx = 0; gx < FONT_W; gx++) {
                uint32_t color = (glyph[gy] >> (FONT_W - 1 - gx)) & 1u ? FG : BG;
                for (uint32_t sx = 0; sx < SCALE; sx++) {
                    *px++ = color;
                }
            }
        }
    }
}

static void clean_rows(uint32_t first_y, uint32_t count)
{
    uintptr_t start = (uintptr_t)pixel_row(first_y);
    cache_clean(start, start + (uintptr_t)count * stride);
}

static void present(void)
{
    uint32_t trig = reg_read(decon_vaddr, DECON_TRIG_CON);

    asm volatile("dsb sy" ::: "memory");
    reg_write(decon_vaddr, DECON_TRIG_CON,
              (trig & ~(TRIG_CON_HW_TRIG_EN | TRIG_CON_HW_TRIG_MASK)) | TRIG_CON_SW_TRIG_EN | TRIG_CON_SW_TRIG_DET_EN);
    reg_write(decon_vaddr, DECON_SHD_REG_UP_REQ, SHD_REG_UP_REQ_GLOBAL);
}

static void start_decon(void)
{
    uint32_t con = reg_read(decon_vaddr, DECON_GLOBAL_CON);

    reg_write(decon_vaddr, DECON_GLOBAL_CON, con | GLOBAL_CON_DECON_EN | GLOBAL_CON_DECON_EN_F);
    reg_write(decon_vaddr, DECON_SHD_REG_UP_REQ, SHD_REG_UP_REQ_GLOBAL);
    for (uint32_t spins = 0; spins < RUN_STATUS_SPINS; spins++) {
        if (reg_read(decon_vaddr, DECON_GLOBAL_CON) & GLOBAL_CON_RUN_STATUS) {
            return;
        }
    }
    log_hex("FBCON|WARN: DECON0 RUN_STATUS not set; GLOBAL_CON ", reg_read(decon_vaddr, DECON_GLOBAL_CON));
}

static void clear_screen(void)
{
    for (uint32_t y = 0; y < height; y++) {
        volatile uint32_t *px = pixel_row(y);
        for (uint32_t x = 0; x < width; x++) {
            px[x] = BG;
        }
    }
    clean_rows(0, height);
    for (uint32_t r = 0; r < rows; r++) {
        for (uint32_t c = 0; c < cols; c++) {
            grid[r][c] = ' ';
        }
    }
}

static void scroll_grid(void)
{
    for (uint32_t r = 1; r < rows; r++) {
        for (uint32_t c = 0; c < cols; c++) {
            grid[r - 1][c] = grid[r][c];
        }
    }
    for (uint32_t c = 0; c < cols; c++) {
        grid[rows - 1][c] = ' ';
    }
    scrolled = true;
}

static void newline(void)
{
    cur_col = 0;
    if (cur_row + 1 < rows) {
        cur_row++;
    } else {
        scroll_grid();
    }
}

static void put_printable(char c)
{
    if (cur_col >= cols) {
        newline();
    }
    grid[cur_row][cur_col++] = c;
    dirty[cur_row] = true;
}

static void put(char c)
{
    switch (c) {
    case '\n':
        newline();
        return;
    case '\r':
        cur_col = 0;
        return;
    case '\t':
        for (uint32_t i = 0; i < TAB_WIDTH; i++) {
            put_printable(' ');
        }
        return;
    default:
        if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f) {
            put_printable(c);
        }
        return;
    }
}

static void draw_row(uint32_t r)
{
    for (uint32_t c = 0; c < cols; c++) {
        draw_glyph(c, r, grid[r][c]);
    }
    clean_rows(r * CELL_H, CELL_H);
}

static void render(void)
{
    for (uint32_t r = 0; r < rows; r++) {
        if (scrolled || dirty[r]) {
            draw_row(r);
        }
        dirty[r] = false;
    }
    scrolled = false;
    present();
}

static bool drain_ring(void)
{
    struct console_ring *ring = (struct console_ring *)fbcon_ring_vaddr;
    uint64_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
    bool any = false;

    while (tail != __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE)) {
        if (ready) {
            put(ring->buf[tail % CONSOLE_RING_CAPACITY]);
        }
        tail++;
        any = true;
    }
    __atomic_store_n(&ring->tail, tail, __ATOMIC_RELEASE);
    return any;
}

void init(void)
{
    if (!read_geometry()) {
        microkit_dbg_puts("FBCON|ERROR: not drawing; the console stays on the UART only\n");
        return;
    }
    clear_screen();
    start_decon();
    present();
    ready = true;
    microkit_dbg_puts("FBCON|INFO: mirroring the console to the panel\n");
}

void notified(microkit_channel ch)
{
    if (ch != TRACER_CH) {
        microkit_dbg_puts("FBCON|ERROR: notification on unexpected channel\n");
        return;
    }
    if (drain_ring() && ready) {
        render();
    }
}
