// RUN: echo "GPU binary would be here" > %t
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s \
// RUN:     -fcuda-include-gpubinary %t -o - \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -fincremental-extensions \
// RUN:     -fcuda-include-gpubinary %t -o - \
// RUN:   | FileCheck --check-prefix=INCR %s

#include "Inputs/cuda.h"

__global__ void kernel() {}

struct Launcher {
  Launcher() { kernel<<<1, 1>>>(); }
} launcher;

// The constructor that registers the device binary precedes the static
// initializer that launches the kernel.
// CHECK: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__cuda_module_ctor, ptr null }, { i32, ptr, ptr } { i32 65535, ptr @_GLOBAL__sub_I_{{.*}}, ptr null }]

// Under -fincremental-extensions the initializer is a constructor of its own,
// emitted before the module is finalized; the registration still precedes it.
// INCR: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @__cuda_module_ctor, ptr null }, { i32, ptr, ptr } { i32 65535, ptr @__cxx_global_var_init, ptr null }]
