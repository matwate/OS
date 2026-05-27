#include <stdint.h>
#include <stdbool.h>
#include "mnist.h"

#define VGA_ADDR ((unsigned char *)0xA0000)
#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define CELL_SIZE 5
#define GRID_N 28
#define Y_PADDING 30
#define X_PADDING 20

#define BLACK 0
#define BLUE 1
#define WHITE 15
#define GRAY 8
#define DKGRAY 7
#define RED 12
#define GREEN 10

int mouse_x = 160, mouse_y = 100;
int mouse_buttons = 0, prev_mouse_buttons = 0;
int grid[GRID_N][GRID_N];

/* Weights buffer — loaded from disk at startup */
uint8_t weights_buf[MNIST_WEIGHTS_BUFSIZE] __attribute__((section(".bss")));

/* Off-screen framebuffer to eliminate flicker */
uint8_t framebuffer[VGA_WIDTH * VGA_HEIGHT] __attribute__((section(".bss")));

/* Forward declarations */
void clear_screen(unsigned char c);
void putpixel(int x, int y, unsigned char c);
void fill_rect(int x, int y, int w, int h, unsigned char c);
void draw_rect(int x, int y, int w, int h, unsigned char c);
void flip(void);
bool mouse_in_grid_b(int mx, int my);
int left_just_pressed(void);
int left_held(void);
int ata_pio_read(uint32_t lba, uint16_t count, void *dst);
void mnist_pointers(void);
int mnist_classify(const uint8_t *image);

/* === Graphics === */
void clear_screen(unsigned char c) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) framebuffer[i] = c;
}
void putpixel(int x, int y, unsigned char c) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;
    framebuffer[y * VGA_WIDTH + x] = c;
}
void flip(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) VGA_ADDR[i] = framebuffer[i];
}
void fill_rect(int x, int y, int w, int h, unsigned char c) {
    for (int r = y; r < y + h; r++)
        for (int col = x; col < x + w; col++)
            putpixel(col, r, c);
}
void draw_rect(int x, int y, int w, int h, unsigned char c) {
    for (int i = x; i < x + w; i++) { putpixel(i, y, c); putpixel(i, y + h - 1, c); }
    for (int i = y; i < y + h; i++) { putpixel(x, i, c); putpixel(x + w - 1, i, c); }
}

/* === Grid === */
void clear_grid(void) {
    for (int i = 0; i < GRID_N; i++)
        for (int j = 0; j < GRID_N; j++) grid[i][j] = BLACK;
}
void grid_set(int x, int y) {
    if (x >= 0 && x < GRID_N && y >= 0 && y < GRID_N) grid[x][y] = WHITE;
}
bool grid_get(int x, int y) {
    if (x < 0 || x >= GRID_N || y < 0 || y >= GRID_N) return false;
    return grid[x][y] == WHITE;
}
void draw_grid_lines(void) {
    for (int i = 0; i <= GRID_N; i++) {
        fill_rect(X_PADDING, Y_PADDING + i * CELL_SIZE, GRID_N * CELL_SIZE, 1, BLUE);
        fill_rect(X_PADDING + i * CELL_SIZE, Y_PADDING, 1, GRID_N * CELL_SIZE, BLUE);
    }
}
void draw_grid(void) {
    draw_grid_lines();
    for (int i = 0; i < GRID_N; i++)
        for (int j = 0; j < GRID_N; j++)
            if (grid_get(i, j))
                fill_rect(i * CELL_SIZE + X_PADDING, j * CELL_SIZE + Y_PADDING,
                          CELL_SIZE, CELL_SIZE, WHITE);
}
int mouse_pos_to_grid(int mx, int my) {
    return (mx - X_PADDING) / CELL_SIZE + ((my - Y_PADDING) / CELL_SIZE) * GRID_N;
}
bool mouse_in_grid_b(int mx, int my) {
    return mx >= X_PADDING && mx < X_PADDING + GRID_N * CELL_SIZE &&
           my >= Y_PADDING && my < Y_PADDING + GRID_N * CELL_SIZE;
}

/* === Pixel conversion for fixed-point MNIST === */
void grid_to_pixels(uint8_t out[784]) {
    for (int row = 0; row < GRID_N; row++)
        for (int col = 0; col < GRID_N; col++)
            out[row * GRID_N + col] = grid_get(row, col) ? 1 : 0;
}

/* === Button === */
typedef struct { int x, y, w, h; unsigned char color, border; const char *label; } button;

int mouse_in_button(int mx, int my, const button *btn) {
    return mx >= btn->x && mx < btn->x + btn->w && my >= btn->y && my < btn->y + btn->h;
}

static const unsigned char digit_font[10][7] = {
    {0x70,0x88,0x88,0x88,0x88,0x88,0x70},
    {0x20,0x60,0x20,0x20,0x20,0x20,0x70},
    {0x70,0x08,0x08,0x70,0x80,0x80,0xF8},
    {0x70,0x08,0x08,0x70,0x08,0x08,0x70},
    {0x10,0x30,0x50,0x90,0xF8,0x10,0x10},
    {0xF8,0x80,0x80,0xF0,0x08,0x08,0x70},
    {0x70,0x80,0x80,0xF0,0x88,0x88,0x70},
    {0xF8,0x08,0x10,0x20,0x40,0x40,0x40},
    {0x70,0x88,0x88,0x70,0x88,0x88,0x70},
    {0x70,0x88,0x88,0x78,0x08,0x08,0x70},
};

void draw_digit(int x, int y, int d, unsigned char c) {
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 7; row++) {
        unsigned char bits = digit_font[d][row];
        for (int col = 0; col < 5; col++)
            if (bits & (0x80 >> col)) putpixel(x + col, y + row, c);
    }
}

void draw_number(int x, int y, int n, unsigned char c) {
    if (n < 10) { draw_digit(x, y, n, c); return; }
    if (n < 100) { draw_digit(x, y, n / 10, c); draw_digit(x + 6, y, n % 10, c); return; }
    draw_digit(x,      y, n / 100, c);
    draw_digit(x + 6,  y, (n / 10) % 10, c);
    draw_digit(x + 12, y, n % 10, c);
}

void draw_button(const button *btn) {
    fill_rect(btn->x, btn->y, btn->w, btn->h, btn->color);
    draw_rect(btn->x, btn->y, btn->w, btn->h, btn->border);
    int lx = btn->x + 8;
    const char *p = btn->label;
    while (*p) {
        int d = -1;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p == '-') { /* skip */ }
        if (d >= 0) draw_digit(lx, btn->y + 6, d, btn->border);
        lx += 7;
        p++;
    }
}

button clear_btn = { 20, 5, 80, 20, RED, WHITE, "CLEAR" };
button classify_btn = { 110, 5, 80, 20, GREEN, WHITE, "CLASS" };

/* === Mouse === */
unsigned char inpb(unsigned short port) {
    unsigned char val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
void outpb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
int mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++) if ((inpb(0x64) & 1) == 1) return 1;
    return 0;
}
int mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++) if ((inpb(0x64) & 2) == 0) return 1;
    return 0;
}
void mouse_write(unsigned char val) {
    mouse_wait_write(); outpb(0x64, 0xD4);
    mouse_wait_write(); outpb(0x60, val);
}
unsigned char mouse_read(void) { mouse_wait_read(); return inpb(0x60); }
void init_mouse(void) {
    mouse_wait_write(); outpb(0x64, 0xA8);
    mouse_wait_write(); outpb(0x64, 0x20);
    unsigned char status = mouse_wait_read() ? inpb(0x60) : 0;
    status |= 2; status &= ~32;
    mouse_wait_write(); outpb(0x64, 0x60);
    mouse_wait_write(); outpb(0x60, status);
    mouse_write(0xFF); mouse_read(); mouse_read(); mouse_read();
    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();
    for (int i = 0; i < 10; i++) if ((inpb(0x64) & 1)) inpb(0x60);
}
void update_mouse(void) {
    if ((inpb(0x64) & 1) == 0) return;
    unsigned char b0 = inpb(0x60);
    if ((b0 & 0x08) == 0) { while ((inpb(0x64) & 1)) inpb(0x60); return; }
    if (!mouse_wait_read()) return; unsigned char b1 = inpb(0x60);
    if (!mouse_wait_read()) return; unsigned char b2 = inpb(0x60);
    prev_mouse_buttons = mouse_buttons;
    mouse_buttons = b0 & 3;
    mouse_x += (char)b1; mouse_y -= (char)b2;
    if (mouse_x < 0) mouse_x = 0; if (mouse_x >= 320) mouse_x = 319;
    if (mouse_y < 0) mouse_y = 0; if (mouse_y >= 200) mouse_y = 199;
}
int left_just_pressed(void) { return (mouse_buttons & 1) && !(prev_mouse_buttons & 1); }
int left_held(void) { return (mouse_buttons & 1); }
void draw_cursor(void) {
    putpixel(mouse_x, mouse_y, WHITE);
    putpixel(mouse_x-1, mouse_y, WHITE); putpixel(mouse_x+1, mouse_y, WHITE);
    putpixel(mouse_x, mouse_y-1, WHITE); putpixel(mouse_x, mouse_y+1, WHITE);
    putpixel(mouse_x-2, mouse_y, GRAY);  putpixel(mouse_x+2, mouse_y, GRAY);
    putpixel(mouse_x, mouse_y-2, GRAY);  putpixel(mouse_x, mouse_y+2, GRAY);
}

/* === Main === */
void __attribute__((section(".text.start"))) kernel_main(void) {
    clear_screen(BLACK);
    init_mouse();

    /* Load MNIST weights from disk.
     * Kernel occupies LBA 1..K (padded to sector boundary).
     * Weights start at LBA K+1.
     * The build script passes the exact offset at compile time. */
#ifdef WEIGHTS_LBA
    int result = ata_pio_read(WEIGHTS_LBA, MNIST_WEIGHTS_SECTORS, weights_buf);
    (void)result;
    mnist_pointers();
#else
    /* Fallback if build script didn't pass WEIGHTS_LBA */
    mnist_pointers();
#endif

    int last_digit = -1;

    while (1) {
        clear_screen(BLACK);
        draw_grid();
        draw_button(&clear_btn);
        draw_button(&classify_btn);
        update_mouse();

        if (mouse_in_grid_b(mouse_x, mouse_y) && left_held()) {
            int idx = mouse_pos_to_grid(mouse_x, mouse_y);
            grid_set(idx % GRID_N, idx / GRID_N);
        }
        if (left_just_pressed()) {
            if (mouse_in_button(mouse_x, mouse_y, &clear_btn))
                clear_grid();
            else if (mouse_in_button(mouse_x, mouse_y, &classify_btn)) {
                uint8_t pixels[784];
                grid_to_pixels(pixels);
                last_digit = mnist_classify(pixels);
            }
        }

        if (last_digit >= 0) {
            fill_rect(200, 5, 40, 20, DKGRAY);
            draw_number(205, 6, last_digit, WHITE);
        }

        draw_cursor();
        flip();
    }
}
