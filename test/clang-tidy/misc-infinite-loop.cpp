// RUN: %check_clang_tidy %s misc-infinite-loop %t

void simple_loop1() {
  int i = 0;
  int j = 0;
  while (i < 10) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: error: The condition variable (i) is not updated in the loop body [misc-infinite-loop
    j++;
  }
}


void simple_loop2() {
  int i = 0;
  int j = 0;
  int Limit = 5;
  while (i < Limit) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: error: None of the condition variables (i, Limit) are updated in the loop body [misc-infinite-loop
    j++;
  }
}

void loop2() {
  int i = 0, Limit = 100;
  int *p = &i;
  while (i < Limit) { // Not Error, since p is alias of i;
    *++p;
  }
}

void loop3() {
  int i = 0, Limit = 100;
  int *p = &i;
  while (i < Limit) { // Error, since the pointer p is increased
    *(p++);
  }
}

struct Dude {
  int a, b;
  operator bool();
};

void loop4() {
  int i = 0, Limit = 100;
  while (i < Limit) { // Not error since Limit is updated
    Limit--;
  }
}

int glob;
void loop6(int &x) {
  int i = 0, Limit = 100;
  while (x < Limit) { // Not error since x can be an alias of glob.
    glob++;
  }
}

void loop7() {
  int i = 0, Limit = 100, j = 0;
  for (int i = 0; i < Limit; j++) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: error: None of the condition variables (i, Limit) are updated in the loop body [misc-infinite-loop
    j++;
  }
}

void loop8() {
  int i = 0, Limit = 100;
  while (glob < Limit) { // Since 'glob' is declared out of the function we do not warn.
    i++;
  }
}