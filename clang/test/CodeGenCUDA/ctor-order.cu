// RUN: echo "GPU binary would be here" > %t
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s \
// RUN:     -fcuda-include-gpubinary %t -o - \
// RUN:   | FileCheck %s

#include "Inputs/cuda.h"

__global__ void kernel() {}

struct Launcher {
  Launcher() { kernel<<<1, 1>>>(); }
} launcher;

// The constructor that registers the device binary precedes the static
// initializer that launches the kernel.
// CHECK: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__cuda_module_ctor, ptr null }, { i32, ptr, ptr } { i32 65535, ptr @_GLOBAL__sub_I_{{.*}}, ptr null }]
