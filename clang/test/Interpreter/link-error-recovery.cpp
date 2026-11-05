// REQUIRES: host-supports-jit
// RUN: cat %s | clang-repl 2>&1 | FileCheck %s

// A link error in one input must not fail the inputs after it. The failed
// module's initializer used to stay registered with the JIT, and running the
// next input's initializers tripped over it.

extern "C" int printf(const char *, ...);

extern "C" int undefined_function();
int result = undefined_function();
// CHECK: Symbols not found: [ {{_?}}undefined_function ]
// CHECK: error: Failed to materialize symbols
// CHECK-NOT: error:

int x = 42;
printf("x = %d\n", x);
// CHECK: x = 42

%quit
