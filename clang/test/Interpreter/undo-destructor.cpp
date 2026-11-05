// REQUIRES: host-supports-jit
// RUN: cat %s | clang-repl 2>&1 | FileCheck %s

// Undoing an input with a destructor function must not leave the destructor
// registered with the JIT: at exit it would be called after its code has been
// freed, and whatever was loaded at that address since would run instead.

extern "C" int printf(const char *, ...);

__attribute__((destructor)) void d() { printf("dtor ran\n"); }
%undo

printf("after undo\n");
// CHECK: after undo
// CHECK-NOT: after undo
// CHECK-NOT: dtor ran
// CHECK-NOT: error:

%quit
