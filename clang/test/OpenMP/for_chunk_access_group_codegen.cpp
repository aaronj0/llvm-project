// RUN: %clang_cc1 -fopenmp -x c++ -triple x86_64-unknown-unknown -emit-llvm %s -o - | FileCheck %s

// A thread runs the iterations of a chunk in logical order, so a dependence
// between iterations of one chunk is valid and the chunk loop must not carry
// llvm.loop.parallel_accesses. Only order(concurrent) allows it.

// CHECK-LABEL: define {{.*}}@_Z7dynamicPKiPi(
// CHECK: call void @__kmpc_dispatch_init_4(
// CHECK-NOT: !llvm.access.group
// CHECK: ret void
void dynamic(const int *idx, int *h) {
#pragma omp for schedule(dynamic)
  for (int i = 0; i < 128; ++i)
    h[idx[i]]++;
}

// CHECK-LABEL: define {{.*}}@_Z6guidedPKiPi(
// CHECK: call void @__kmpc_dispatch_init_4(
// CHECK-NOT: !llvm.access.group
// CHECK: ret void
void guided(const int *idx, int *h) {
#pragma omp for schedule(guided)
  for (int i = 0; i < 128; ++i)
    h[idx[i]]++;
}

// CHECK-LABEL: define {{.*}}@_Z10distributePKiPi.omp_outlined(
// CHECK: call void @__kmpc_for_static_init_4(
// CHECK-NOT: !llvm.access.group
// CHECK: ret void
void distribute(const int *idx, int *h) {
#pragma omp teams distribute dist_schedule(static, 4)
  for (int i = 0; i < 128; ++i)
    h[idx[i]]++;
}

// CHECK-LABEL: define {{.*}}@_Z10concurrentPKiPi(
// CHECK: call void @__kmpc_dispatch_init_4(
// CHECK: store i32 {{.*}}, !llvm.access.group
// CHECK: ret void
void concurrent(const int *idx, int *h) {
#pragma omp for schedule(dynamic) order(concurrent)
  for (int i = 0; i < 128; ++i)
    h[i] = idx[i];
}
