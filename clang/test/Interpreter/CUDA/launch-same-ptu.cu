// Tests a kernel defined and launched in the same input
// RUN: cat %s | clang-repl --cuda | FileCheck %s

extern "C" int printf(const char*, ...);

int var;
int* devptr = nullptr;
printf("cudaMalloc: %d\n", cudaMalloc((void **) &devptr, sizeof(int)));
// CHECK: cudaMalloc: 0

__global__ void test_kernel(int* value) { *value = 42; } test_kernel<<<1,1>>>(devptr); printf("CUDA Error: %d\n", cudaGetLastError());
// CHECK-NEXT: CUDA Error: 0

printf("cudaMemcpy: %d\n", cudaMemcpy(&var, devptr, sizeof(int), cudaMemcpyDeviceToHost));
// CHECK-NEXT: cudaMemcpy: 0

printf("Value: %d\n", var);
// CHECK-NEXT: Value: 42

// A global's dynamic initializer launching a kernel of the same input: under
// -fincremental-extensions it is emitted as its own constructor as soon as
// the declaration is seen, so the device binary must be registered ahead of it.
__device__ int reg_value = 0; __global__ void reg_kernel(int v) { reg_value = v; } int launch_and_read(int v) { reg_kernel<<<1, 1>>>(v); int err = (int)cudaGetLastError(); cudaDeviceSynchronize(); int out = 0; cudaMemcpyFromSymbol(&out, reg_value, sizeof(int)); return err * 1000 + out; } static const int from_static_init = launch_and_read(7);
printf("Static initializer: %d\n", from_static_init);
// CHECK-NEXT: Static initializer: 7

%quit
