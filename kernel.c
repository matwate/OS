#include <stddef.h>
#define VGA_ADDR ((unsigned char *)0xA0000)
#define VGA_WIDTH 320
#define VGA_HEIGHT 200

#define CELL_SIZE 5
#define GRID_N 28

void putpixel(int x, int y, unsigned char color) {
  if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT)
    return;
  VGA_ADDR[y * VGA_WIDTH + x] = color;
}

void clear_screen(unsigned char color) {
  unsigned char *vga = VGA_ADDR;
  int total = VGA_WIDTH * VGA_HEIGHT;
  for (int i = 0; i < total; i++)
    vga[i] = color;
}

void fill_rect(int x, int y, int w, int h, unsigned char color) {
  for (int row = y; row < y + h; row++) {
    for (int col = x; col < x + w; col++) {
      putpixel(col, row, color);
    }
  }
}

void draw_rect(int x, int y, int w, int h, unsigned char color) {
  for (int i = x; i < x + w; i++) {
    putpixel(i, y, color);
    putpixel(i, y + h - 1, color);
  }
  for (int i = y; i < y + h; i++) {
    putpixel(x, i, color);
    putpixel(x + w - 1, i, color);
  }
}

void draw_mnist_grid(int starting_point_x, int starting_point_y) {

  for (int i = 0; i < GRID_N; i++) {
    int every_5 = 0; 

    draw_rect(starting_point_x, i * CELL_SIZE + every_5 + starting_point_y, CELL_SIZE * GRID_N, 1, 15);
    for (int k = 0; k < CELL_SIZE; k++) {

      for (int j = 0; j <= GRID_N *CELL_SIZE; j += CELL_SIZE) {
        putpixel(starting_point_x + j , i * CELL_SIZE + k + starting_point_y, 15);
      }
      every_5 = k;
    }
  }
  draw_rect(starting_point_x, starting_point_y + GRID_N *CELL_SIZE, GRID_N *CELL_SIZE, 1 , 15);
}

void __attribute__((section(".text.start"))) kernel_main(void) {
  clear_screen(0);

  draw_mnist_grid(10, 20);

  while (1) {
  }
}
