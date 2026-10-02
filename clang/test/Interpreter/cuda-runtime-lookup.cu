// The runtime is loaded from the toolkit the driver uses, from lib64/ or lib/
// depending on the layout: explicit through --cuda-path, or detected through
// ptxas on PATH. A stub standing in for libcudart.so exports a marker that
// shows which library was loaded, and a host-only input never calls into the
// runtime, so neither a toolkit nor a GPU is needed.
// REQUIRES: host-supports-jit, nvptx-registered-target, system-linux
// RUN: rm -rf %t && mkdir -p %t/explicit/lib64
// RUN: echo 'int cudart_stub(void) { return 64; }' \
// RUN:   | %clang -fPIC -shared -x c - -o %t/explicit/lib64/libcudart.so
// RUN: cat %s | clang-repl --cuda --cuda-path=%t/explicit -Xcc -nocudainc \
// RUN:   -Xcc -nocudalib | FileCheck %s
//
// RUN: mkdir -p %t/detected/bin %t/detected/include %t/detected/nvvm/libdevice \
// RUN:   %t/detected/lib
// RUN: echo 'int cudart_stub(void) { return 32; }' \
// RUN:   | %clang -fPIC -shared -x c - -o %t/detected/lib/libcudart.so
// RUN: echo 'int main(void) { return 0; }' | %clang -x c - -o %t/detected/bin/ptxas
// RUN: echo '#define CUDA_VERSION 11080' > %t/detected/include/cuda.h
// RUN: cat %s | env PATH=%t/detected/bin clang-repl --cuda -Xcc -nocudainc \
// RUN:   -Xcc -nocudalib | FileCheck --check-prefix=LIB %s
extern "C" int printf(const char*, ...);
extern "C" int cudart_stub();
printf("stub: %d\n", cudart_stub());
// CHECK: stub: 64
// LIB: stub: 32
%quit
