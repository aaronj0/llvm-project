// clang-repl loads the CUDA runtime from the toolkit the driver uses, out of
// its lib64/. The toolkit tree below holds a libcudart.so that cannot be
// loaded, so the error names the path clang-repl chose.
// REQUIRES: host-supports-jit, nvptx-registered-target, system-linux
// RUN: not clang-repl --cuda --cuda-path=%S/Inputs/CUDA/usr/local/cuda -Xcc -v \
// RUN:   -Xcc -nocudainc -Xcc -nocudalib < /dev/null 2>&1 | FileCheck %s
// RUN: not clang-repl --cuda -Xcc --sysroot=%S/Inputs/CUDA -Xcc -v \
// RUN:   -Xcc --cuda-path-ignore-env -Xcc -nocudainc -Xcc -nocudalib \
// RUN:   < /dev/null 2>&1 | FileCheck %s
// CHECK: Found CUDA installation: {{.*}}Inputs/CUDA/usr/local/cuda
// CHECK: clang-repl: {{.*}}Inputs/CUDA/usr/local/cuda/lib64/libcudart.so:
