//===--- IncrementalExecutor.cpp - Incremental Execution --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the class which performs incremental code execution.
//
//===----------------------------------------------------------------------===//

#include "IncrementalExecutor.h"

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Interpreter/PartialTranslationUnit.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"

#include <iostream>
// Force linking some of the runtimes that helps attaching to a debugger.
LLVM_ATTRIBUTE_USED void linkComponents() {
  llvm::errs() << (void *)&llvm_orc_registerJITLoaderGDBWrapper
               << (void *)&llvm_orc_registerJITLoaderGDBAllocAction;
}

namespace clang {
IncrementalExecutor::IncrementalExecutor(llvm::orc::ThreadSafeContext &TSC)
    : TSCtx(TSC) {}

llvm::Expected<std::unique_ptr<llvm::orc::LLJITBuilder>>
IncrementalExecutor::createDefaultJITBuilder(
    llvm::orc::JITTargetMachineBuilder JTMB) {
  auto JITBuilder = std::make_unique<llvm::orc::LLJITBuilder>();
  JITBuilder->setJITTargetMachineBuilder(std::move(JTMB));
  JITBuilder->setPrePlatformSetup([](llvm::orc::LLJIT &J) {
    // Try to enable debugging of JIT'd code (only works with JITLink for
    // ELF and MachO).
    consumeError(llvm::orc::enableDebuggerSupport(J));
    return llvm::Error::success();
  });
  return std::move(JITBuilder);
}

IncrementalExecutor::IncrementalExecutor(llvm::orc::ThreadSafeContext &TSC,
                                         llvm::orc::LLJITBuilder &JITBuilder,
                                         llvm::Error &Err)
    : TSCtx(TSC) {
  using namespace llvm::orc;
  llvm::ErrorAsOutParameter EAO(&Err);

  if (auto JitOrErr = JITBuilder.create())
    Jit = std::move(*JitOrErr);
  else {
    Err = JitOrErr.takeError();
    return;
  }
}

IncrementalExecutor::~IncrementalExecutor() {}

llvm::Error IncrementalExecutor::addModule(PartialTranslationUnit &PTU) {
  llvm::orc::ResourceTrackerSP RT =
      Jit->getMainJITDylib().createResourceTracker();
  ResourceTrackers[&PTU] = RT;

  return Jit->addIRModule(RT, {std::move(PTU.TheModule), TSCtx});
}

llvm::Error IncrementalExecutor::removeModule(PartialTranslationUnit &PTU) {

  llvm::orc::ResourceTrackerSP RT = std::move(ResourceTrackers[&PTU]);
  if (!RT)
    return llvm::Error::success();

  ResourceTrackers.erase(&PTU);
  if (llvm::Error Err = RT->remove())
    return Err;
  return llvm::Error::success();
}

// Clean up the JIT instance.
llvm::Error IncrementalExecutor::cleanUp() {
  // This calls the global dtors of registered modules.
  return Jit->deinitialize(Jit->getMainJITDylib());
}

llvm::Error IncrementalExecutor::runCtors() const {
  return Jit->initialize(Jit->getMainJITDylib());
}

llvm::Expected<llvm::orc::ExecutorAddr>
IncrementalExecutor::getSymbolAddress(llvm::StringRef Name,
                                      SymbolNameKind NameKind) const {
  using namespace llvm::orc;
  auto SO = makeJITDylibSearchOrder({&Jit->getMainJITDylib(),
                                     Jit->getPlatformJITDylib().get(),
                                     Jit->getProcessSymbolsJITDylib().get()});

  ExecutionSession &ES = Jit->getExecutionSession();

  auto SymOrErr =
      ES.lookup(SO, (NameKind == LinkerName) ? ES.intern(Name)
                                             : Jit->mangleAndIntern(Name));
  if (auto Err = SymOrErr.takeError())
    return std::move(Err);
  return SymOrErr->getAddress();
}


// MCJIT implementation
MCJITIncrementalExecutor::MCJITIncrementalExecutor(
                                         llvm::orc::ThreadSafeContext &TSC,
                                         llvm::TargetMachine *TM,
                                         llvm::Error &Err,
                                         llvm::EngineBuilder* JITBuilder)
    : IncrementalExecutor(TSC) {
  
  llvm::ErrorAsOutParameter EAO(&Err);
  
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto Context = TSC.getContext();
  auto M = std::make_unique<llvm::Module>("__mcjit_module", *Context);
  
  llvm::EngineBuilder EB(std::move(M));
  std::string err;
  EB.setErrorStr(&err);
  EB.setEngineKind(llvm::EngineKind::JIT);
  
  if(JITBuilder)
    std::cout<<"Using custom JITBuilder\n";

  llvm::ExecutionEngine *engine = JITBuilder ?
      JITBuilder->create(TM) :
      EB.create(TM);
  EE.reset(engine);
  
  if (!EE) {
    Err = llvm::make_error<llvm::StringError>(
        "Failed to create ExecutionEngine: " + err, 
        llvm::inconvertibleErrorCode());
    return;
  }
}

llvm::Error MCJITIncrementalExecutor::addModule(PartialTranslationUnit &PTU) {
  std::cout << "[MCJITIncrementalExecutor::addModule] Adding module: " << PTU.TheModule.get() << "module name" << " moduleName: " << PTU.TheModule->getName().data()  << "\n";  
  std::cout << "\nIR of module being added:\n";
  PTU.TheModule->print(llvm::outs(), nullptr);
  EE->addModule(std::move(PTU.TheModule));
  return llvm::Error::success();
}
  // llvm::Error removeModule(PartialTranslationUnit &PTU) override;
  llvm::Error MCJITIncrementalExecutor::runCtors() const {
  EE->finalizeObject();
  EE->runStaticConstructorsDestructors(false);
  return llvm::Error::success();
  }
  // llvm::Error cleanUp() override;
  // llvm::Expected<llvm::orc::ExecutorAddr>
  // getSymbolAddress(llvm::StringRef Name,
  //                  SymbolNameKind NameKind) const override;


  llvm::Expected<llvm::orc::ExecutorAddr>
MCJITIncrementalExecutor::getSymbolAddress(llvm::StringRef Name,
                                          SymbolNameKind NameKind) const {
  
  std::string SymbolName = Name.str();
                                          
  if (NameKind == IRName) {
    // MCJIT uses the linker names directly.
    // For simplicity, we assume that all names are not mangled.
    // In a complete implementation, we would need to mangle the name
    // according to the target platform's ABI.
  }



  // auto *MCJITEngine = static_cast<llvm::MCJIT*>(EE.get());
  
  
  // Try to find the symbol in external libraries/process first
  uint64_t Addr = llvm::RTDyldMemoryManager::getSymbolAddressInProcess(SymbolName);
  if (Addr)
    return llvm::orc::ExecutorAddr(Addr);

  // Cast away const since ExecutionEngine methods aren't const
  auto *NonConstEE = const_cast<llvm::ExecutionEngine*>(EE.get());

  // Try as a function in JIT'd modules
  Addr = NonConstEE->getFunctionAddress(SymbolName);
  if (Addr)
    return llvm::orc::ExecutorAddr(Addr);

  // Try as a global variable in JIT'd modules  
  Addr = NonConstEE->getGlobalValueAddress(SymbolName);
  if (Addr)
    return llvm::orc::ExecutorAddr(Addr);

  return llvm::make_error<llvm::StringError>(
      "Symbol not found: " + SymbolName,
      llvm::inconvertibleErrorCode());


  // dlsym via llvm::sys::DynamicLibrary since we don't have the MemoryManager
}

llvm::Error MCJITIncrementalExecutor::cleanUp() {
  return llvm::Error::success();
}

  MCJITIncrementalExecutor::~MCJITIncrementalExecutor() = default;
} // namespace clang
