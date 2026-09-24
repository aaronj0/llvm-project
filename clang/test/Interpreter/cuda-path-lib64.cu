// --cuda-path on the Linux toolkit layout, with the runtime in lib64/. A
// host-only input never calls into the runtime, so a stub library suffices.
// REQUIRES: host-supports-jit, nvptx-registered-target
// UNSUPPORTED: system-windows
// RUN: rm -rf %t && mkdir -p %t/lib64
// RUN: %clang -fPIC -shared -x c /dev/null -o %t/lib64/libcudart.so
// RUN: cat %s | clang-repl --cuda --cuda-path=%t -Xcc -nocudainc \
// RUN:   -Xcc -nocudalib | FileCheck %s
extern "C" int printf(const char*, ...);
int i = 42;
printf("i: %d\n", i);
// CHECK: i: 42
%quit
