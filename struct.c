#include <stdio.h>
#include <math.h>

int main(void) {
  typedef struct {
    float x;
    float y;
  } point;

  point p;
  p.x = 0.5;
  p.y = 0.4;
  float length = sqrt(p.x * p.x + p.y * p.y);
  printf("Length of like is %f\n", length);
  return 0;
}
