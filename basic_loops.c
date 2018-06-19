#include <stdio.h>

// static void print_hello(int n);
void print_hello(int n) {
  for(int i=0;i<n;i++) {
    puts("Hello World!");
  }
}

int main(void) {
  int i = 5;
  while(i > 0) {
    printf("While loop - step %d\n", i);
    i--;
  }

  for(int j=1; j<=5; j++) {
    printf("For loop - step %d\n", j);
  }

  print_hello(1);
  return 1;
}

