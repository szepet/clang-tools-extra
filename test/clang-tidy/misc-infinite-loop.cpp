// RUN: %check_clang_tidy %s misc-infinite-loop %t

void simple_infinite_loop1() {
  int i = 0;
  int j = 0;
  while (i < 10) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: The condition variable (i) is not updated in the loop body [misc-infinite-loop]
    j++;
  }

  do {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: The condition variable (i) is not updated in the loop body
    j++;
  } while (i < 10);

  for (i = 0; i < 10; ++j) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: The condition variable (i) is not updated in the loop body
  }
}

void simple_infinite_loop2() {
  int i = 0;
  int j = 0;
  int Limit = 10;
  while (i < Limit) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: None of the condition variables (i, Limit) are updated in the loop body [misc-infinite-loop]
    j++;
  }

  do {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: None of the condition variables (i, Limit) are updated in the loop body
    j++;
  } while (i < Limit);

  for (i = 0; i < Limit; ++j) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: None of the condition variables (i, Limit) are updated in the loop body
  }
}

void simple_not_infinite() {
  int i = 0;
  int Limit = 100;
  while (i < Limit) { // Not an error since 'Limit' is updated
    Limit--;
  }
  do {
    Limit--;
  } while (i < Limit);

  for (i = 0; i < Limit;Limit--) {
  }
}

void escape_before1() {
  int i = 0;
  int Limit = 100;
  int *p = &i;
  while (i < Limit) { // Not an error, since p is alias of i.
    *++p;
  }

  do {
    *++p;
  } while (i < Limit);

  for (i = 0; i < Limit; *++p) {
  }
}

void escape_before2() {
  int i = 0;
  int Limit = 100;
  int *p = &i;
  while (i < Limit) { // We do not warn since the var 'i' is escaped but it is
                      // an actual error, since the pointer 'p' is increased.
    *(p++);
  }
}

void escape_after() {
  int i = 0;
  int j = 0;
  int Limit = 10;

  while (i < Limit) {
    // CHECK-MESSAGES: :[[@LINE-1]]:3: warning: None of the condition variables (i, Limit) are updated in the loop body
  }
  int *p = &i;
}

int glob;
void glob_var(int &x) {
  int i = 0, Limit = 100;
  while (x < Limit) { // Not an error since 'x' can be an alias of glob.
    glob++;
  }
}

void glob_var2() {
  int i = 0, Limit = 100;
  while (glob < Limit) { // Since 'glob' is declared out of the function we do not warn.
    i++;
  }
}
