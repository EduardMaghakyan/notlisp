#include <stdio.h>
static int add_together(int x, int y);

int main(void) {
  printf("10+18=%d\n", add_together(10, 18));
}

int add_together(int x, int y) {
  int result = x + y;
  return result;
}

