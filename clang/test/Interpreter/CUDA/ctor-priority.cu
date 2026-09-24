// Tests a kernel defined and launched in the same incremental input. The
// module constructor that registers the fatbinary must run before the
// top-level statement that launches the kernel. Otherwise the launch targets
// an unregistered stub and fails with cudaErrorInvalidResourceHandle (400).
// RUN: cat %s | clang-repl --cuda | FileCheck %s

extern "C" int printf(const char*, ...);

int* devptr = nullptr;
printf("cudaMalloc: %d\n", cudaMalloc((void **) &devptr, sizeof(int)));
// CHECK: cudaMalloc: 0

__global__ void set_value(int* p) { *p = 7; } set_value<<<1,1>>>(devptr); printf("launch: %d\n", cudaGetLastError());
// CHECK-NEXT: launch: 0

int host_value = 0;
printf("cudaMemcpy: %d\n", cudaMemcpy(&host_value, devptr, sizeof(int), cudaMemcpyDeviceToHost));
// CHECK-NEXT: cudaMemcpy: 0

printf("Value: %d\n", host_value);
// CHECK-NEXT: Value: 7

%quit
