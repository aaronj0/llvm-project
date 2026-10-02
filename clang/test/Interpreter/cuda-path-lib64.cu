// --cuda-path on the Linux toolkit layouts: the runtime in lib64/ (NVIDIA's
// installers) or in lib/ (conda-forge's packages). A stub standing in for
// libcudart.so exports a marker, and a host-only input never calls into the
// runtime, so neither a toolkit nor a GPU is needed.
// REQUIRES: host-supports-jit, nvptx-registered-target, system-linux
// RUN: rm -rf %t && mkdir -p %t/lib64
// RUN: echo 'int cudart_stub(void) { return 64; }' \
// RUN:   | %clang -fPIC -shared -x c - -o %t/lib64/libcudart.so
// RUN: cat %s | clang-repl --cuda --cuda-path=%t -Xcc -nocudainc \
// RUN:   -Xcc -nocudalib | FileCheck %s
// RUN: rm -rf %t && mkdir -p %t/lib
// RUN: echo 'int cudart_stub(void) { return 32; }' \
// RUN:   | %clang -fPIC -shared -x c - -o %t/lib/libcudart.so
// RUN: cat %s | clang-repl --cuda --cuda-path=%t -Xcc -nocudainc \
// RUN:   -Xcc -nocudalib | FileCheck --check-prefix=LIB %s
extern "C" int printf(const char*, ...);
extern "C" int cudart_stub();
printf("stub: %d\n", cudart_stub());
// CHECK: stub: 64
// LIB: stub: 32
%quit
