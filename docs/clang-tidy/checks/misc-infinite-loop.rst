.. title:: clang-tidy - misc-infinite-loop

misc-infinite-loop
==================

Finds loops where none of the condition variables are updated in the body. This
performs a very conservative check in order to avoid false positives and work
only on integer types at the moment.

Examples:

.. code-block:: c++

  void simple_infinite_loop() {
    int i = 0;
    int j = 0;
    int Limit = 10;
    while (i < Limit) { // Error, since none of the variables are updated.
      j++;
    }
  }

  void escape_before() {
    int i = 0;
    int Limit = 100;
    int *p = &i;
    while (i < Limit) { // Not an error, since p is alias of i.
      *++p;
    }
  }
