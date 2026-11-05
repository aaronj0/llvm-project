//===- LLJITTest.cpp - Unit tests for LLJIT -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "OrcTestCommon.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::orc;

namespace {

class LLJITTest : public testing::Test {
protected:
  void SetUp() override {
    OrcNativeTarget::initialize();
    auto JOrErr = LLJITBuilder().create();
    if (!JOrErr) {
      consumeError(JOrErr.takeError());
      GTEST_SKIP();
    }
    J = std::move(*JOrErr);
  }

  ThreadSafeModule parseModule(StringRef Source, StringRef Name) {
    auto Ctx = std::make_unique<LLVMContext>();
    SMDiagnostic Err;
    auto M = parseIR(MemoryBufferRef(Source, Name), Err, *Ctx);
    if (!M) {
      Err.print("LLJITTest", errs());
      return ThreadSafeModule();
    }
    M->setTargetTriple(J->getTargetTriple());
    M->setDataLayout(J->getDataLayout());
    return ThreadSafeModule(std::move(M), std::move(Ctx));
  }

  Expected<int> readInt(StringRef Name) {
    auto Sym = J->lookup(Name);
    if (!Sym)
      return Sym.takeError();
    return *Sym->toPtr<int *>();
  }

  std::unique_ptr<LLJIT> J;
};

// A module whose initializer calls a symbol that is not defined anywhere, so
// that materializing the module fails.
const char *CtorCallsUndefined = R"(
  declare void @undefined_fn()
  define internal void @ctor() {
    call void @undefined_fn()
    ret void
  }
  @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
      [{ i32, ptr, ptr } { i32 65535, ptr @ctor, ptr null }]
)";

// A module whose initializer records that it ran.
const char *CtorSetsRan = R"(
  @ran = global i32 0
  define internal void @ctor() {
    store i32 1, ptr @ran
    ret void
  }
  @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
      [{ i32, ptr, ptr } { i32 65535, ptr @ctor, ptr null }]
)";

TEST_F(LLJITTest, InitializeAfterFailedInitializer) {
  auto &JD = J->getMainJITDylib();

  // The lookup for the initializer materializes the module, which fails.
  cantFail(J->addIRModule(parseModule(CtorCallsUndefined, "A")));
  EXPECT_THAT_ERROR(J->initialize(JD), Failed());

  // The failed module must not get in the way of the next one.
  cantFail(J->addIRModule(parseModule(CtorSetsRan, "B")));
  EXPECT_THAT_ERROR(J->initialize(JD), Succeeded());
  EXPECT_THAT_EXPECTED(readInt("ran"), HasValue(1));
}

TEST_F(LLJITTest, InitializeAfterFailedLookup) {
  auto &JD = J->getMainJITDylib();

  // Materialize the module through a lookup of one of its symbols, before
  // initialize has run for it.
  const char *FooAndCtorCallsUndefined = R"(
    declare void @undefined_fn()
    define i32 @foo() {
      ret i32 1
    }
    define internal void @ctor() {
      call void @undefined_fn()
      ret void
    }
    @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
        [{ i32, ptr, ptr } { i32 65535, ptr @ctor, ptr null }]
  )";
  cantFail(J->addIRModule(parseModule(FooAndCtorCallsUndefined, "A")));
  EXPECT_THAT_EXPECTED(J->lookup("foo"), Failed());

  cantFail(J->addIRModule(parseModule(CtorSetsRan, "B")));
  EXPECT_THAT_ERROR(J->initialize(JD), Succeeded());
  EXPECT_THAT_EXPECTED(readInt("ran"), HasValue(1));
}

TEST_F(LLJITTest, DeinitializeAfterFailedInitializer) {
  auto &JD = J->getMainJITDylib();

  // The module's destructor is registered together with its failing
  // constructor.
  const char *CtorCallsUndefinedWithDtor = R"(
    declare void @undefined_fn()
    define internal void @ctor() {
      call void @undefined_fn()
      ret void
    }
    define internal void @dtor() {
      ret void
    }
    @llvm.global_ctors = appending global [1 x { i32, ptr, ptr }]
        [{ i32, ptr, ptr } { i32 65535, ptr @ctor, ptr null }]
    @llvm.global_dtors = appending global [1 x { i32, ptr, ptr }]
        [{ i32, ptr, ptr } { i32 65535, ptr @dtor, ptr null }]
  )";
  cantFail(J->addIRModule(parseModule(CtorCallsUndefinedWithDtor, "A")));
  EXPECT_THAT_ERROR(J->initialize(JD), Failed());

  const char *DtorSetsRan = R"(
    @ran = global i32 0
    define internal void @dtor() {
      store i32 1, ptr @ran
      ret void
    }
    @llvm.global_dtors = appending global [1 x { i32, ptr, ptr }]
        [{ i32, ptr, ptr } { i32 65535, ptr @dtor, ptr null }]
  )";
  cantFail(J->addIRModule(parseModule(DtorSetsRan, "B")));
  EXPECT_THAT_ERROR(J->initialize(JD), Succeeded());
  EXPECT_THAT_ERROR(J->deinitialize(JD), Succeeded());
  EXPECT_THAT_EXPECTED(readInt("ran"), HasValue(1));
}

} // end anonymous namespace
