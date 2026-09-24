// Tests that kernels can call CUDA device math (libdevice bitcode must be
// linked into every incremental PTU, not just the first one).
// RUN: cat %s | clang-repl --cuda | FileCheck %s

extern "C" int printf(const char*, ...);

__global__ void math_func(float* value) { *value = sinf(0.0f) + expf(0.0f); }

float fvar;
float* fdevptr = nullptr;
printf("cudaMalloc: %d\n", cudaMalloc((void **) &fdevptr, sizeof(float)));
// CHECK: cudaMalloc: 0

math_func<<<1,1>>>(fdevptr);
printf("CUDA Error: %d\n", cudaGetLastError());
// CHECK-NEXT: CUDA Error: 0

printf("cudaMemcpy: %d\n", cudaMemcpy(&fvar, fdevptr, sizeof(float), cudaMemcpyDeviceToHost));
// CHECK-NEXT: cudaMemcpy: 0

printf("Value: %.1f\n", fvar);
// CHECK-NEXT: Value: 1.0

%quit
