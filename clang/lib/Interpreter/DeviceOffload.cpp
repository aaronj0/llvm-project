//===---------- DeviceOffload.cpp - Device Offloading------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements offloading to CUDA devices.
//
//===----------------------------------------------------------------------===//

#include "DeviceOffload.h"

#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/CodeGen/ModuleLinker.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Interpreter/PartialTranslationUnit.h"

#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Transforms/IPO/Internalize.h"

namespace clang {

IncrementalCUDADeviceParser::IncrementalCUDADeviceParser(
    CompilerInstance &DeviceInstance, CompilerInstance &HostInstance,
    IncrementalAction *DeviceAct,
    llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> FS,
    llvm::Error &Err, std::list<PartialTranslationUnit> &PTUs)
    : IncrementalParser(DeviceInstance, DeviceAct, Err, PTUs), VFS(FS),
      DeviceCI(DeviceInstance), HostCodeGenOpts(HostInstance.getCodeGenOpts()),
      TargetOpts(DeviceInstance.getTargetOpts()) {
  if (Err)
    return;
  StringRef Arch = TargetOpts.CPU;
  if (!Arch.starts_with("sm_") || Arch.substr(3).getAsInteger(10, SMVersion)) {
    Err = llvm::joinErrors(std::move(Err), llvm::make_error<llvm::StringError>(
                                               "Invalid CUDA architecture",
                                               llvm::inconvertibleErrorCode()));
    return;
  }
}

llvm::Expected<llvm::StringRef> IncrementalCUDADeviceParser::GeneratePTX() {
  auto &PTU = PTUs.back();

  // The driver's device job links the builtin bitcode named by
  // -mlink-builtin-bitcode (libdevice) and runs the backend on the result.
  // The incremental flow reached neither. BackendConsumer::LinkInModules
  // consumes the link modules on the first translation unit, so every later
  // PTU called undefined __nv_* externs, which the driver's PTX JIT rejects.
  // Mirror the driver for every PTU.
  llvm::SmallVector<LinkModule, 4> LinkModules;
  if (loadLinkModules(DeviceCI, PTU.TheModule->getContext(), LinkModules))
    return llvm::make_error<llvm::StringError>(
        "Failed to load the bitcode to link into the device module.",
        llvm::inconvertibleErrorCode());
  for (LinkModule &LM : LinkModules) {
    if (LM.PropagateAttrs)
      for (llvm::Function &F : *LM.Module) {
        // Skip intrinsics. Keep consistent with how intrinsics are created
        // in LLVM IR.
        if (F.isIntrinsic())
          continue;
        CodeGen::mergeDefaultFunctionDefinitionAttributes(
            F, DeviceCI.getCodeGenOpts(), DeviceCI.getLangOpts(),
            DeviceCI.getTargetOpts(), LM.Internalize);
      }
    bool LinkFailed;
    if (LM.Internalize)
      LinkFailed = llvm::Linker::linkModules(
          *PTU.TheModule, std::move(LM.Module), LM.LinkFlags,
          [](llvm::Module &M, const llvm::StringSet<> &GVS) {
            llvm::internalizeModule(M, [&GVS](const llvm::GlobalValue &GV) {
              return !GV.hasName() || (GVS.count(GV.getName()) == 0);
            });
          });
    else
      LinkFailed = llvm::Linker::linkModules(
          *PTU.TheModule, std::move(LM.Module), LM.LinkFlags);
    if (LinkFailed)
      return llvm::make_error<llvm::StringError>(
          "Failed to link the bitcode into the device module.",
          llvm::inconvertibleErrorCode());
  }

  // Emit through the backend path the driver uses, so the device PTU sees the
  // same pipeline, tuning and verification as a regular device compilation.
  PTXCode.clear();
  DiagnosticsEngine &Diags = DeviceCI.getDiagnostics();
  unsigned NumErrorsBefore = Diags.getNumErrors();
  emitBackendOutput(DeviceCI, DeviceCI.getCodeGenOpts(), PTU.TheModule.get(),
                    Backend_EmitAssembly, DeviceCI.getVirtualFileSystemPtr(),
                    std::make_unique<llvm::raw_svector_ostream>(PTXCode));
  if (Diags.getNumErrors() > NumErrorsBefore)
    return llvm::make_error<llvm::StringError>("Failed to emit PTX code.",
                                               llvm::inconvertibleErrorCode());

  PTXCode += '\0';
  while (PTXCode.size() % 8)
    PTXCode += '\0';
  return PTXCode.str();
}

llvm::Error IncrementalCUDADeviceParser::GenerateFatbinary() {
  enum FatBinFlags {
    AddressSize64 = 0x01,
    HasDebugInfo = 0x02,
    ProducerCuda = 0x04,
    HostLinux = 0x10,
    HostMac = 0x20,
    HostWindows = 0x40
  };

  struct FatBinInnerHeader {
    uint16_t Kind;             // 0x00
    uint16_t unknown02;        // 0x02
    uint32_t HeaderSize;       // 0x04
    uint32_t DataSize;         // 0x08
    uint32_t unknown0c;        // 0x0c
    uint32_t CompressedSize;   // 0x10
    uint32_t SubHeaderSize;    // 0x14
    uint16_t VersionMinor;     // 0x18
    uint16_t VersionMajor;     // 0x1a
    uint32_t CudaArch;         // 0x1c
    uint32_t unknown20;        // 0x20
    uint32_t unknown24;        // 0x24
    uint32_t Flags;            // 0x28
    uint32_t unknown2c;        // 0x2c
    uint32_t unknown30;        // 0x30
    uint32_t unknown34;        // 0x34
    uint32_t UncompressedSize; // 0x38
    uint32_t unknown3c;        // 0x3c
    uint32_t unknown40;        // 0x40
    uint32_t unknown44;        // 0x44
    FatBinInnerHeader(uint32_t DataSize, uint32_t CudaArch, uint32_t Flags)
        : Kind(1 /*PTX*/), unknown02(0x0101), HeaderSize(sizeof(*this)),
          DataSize(DataSize), unknown0c(0), CompressedSize(0),
          SubHeaderSize(HeaderSize - 8), VersionMinor(2), VersionMajor(4),
          CudaArch(CudaArch), unknown20(0), unknown24(0), Flags(Flags),
          unknown2c(0), unknown30(0), unknown34(0), UncompressedSize(0),
          unknown3c(0), unknown40(0), unknown44(0) {}
  };

  struct FatBinHeader {
    uint32_t Magic;      // 0x00
    uint16_t Version;    // 0x04
    uint16_t HeaderSize; // 0x06
    uint32_t DataSize;   // 0x08
    uint32_t unknown0c;  // 0x0c
  public:
    FatBinHeader(uint32_t DataSize)
        : Magic(0xba55ed50), Version(1), HeaderSize(sizeof(*this)),
          DataSize(DataSize), unknown0c(0) {}
  };

  FatBinHeader OuterHeader(sizeof(FatBinInnerHeader) + PTXCode.size());
  FatbinContent.append((char *)&OuterHeader,
                       ((char *)&OuterHeader) + OuterHeader.HeaderSize);

  FatBinInnerHeader InnerHeader(PTXCode.size(), SMVersion,
                                FatBinFlags::AddressSize64 |
                                    FatBinFlags::HostLinux);
  FatbinContent.append((char *)&InnerHeader,
                       ((char *)&InnerHeader) + InnerHeader.HeaderSize);

  FatbinContent.append(PTXCode.begin(), PTXCode.end());

  const PartialTranslationUnit &PTU = PTUs.back();

  std::string FatbinFileName = "/" + PTU.TheModule->getName().str() + ".fatbin";

  VFS->addFile(FatbinFileName, 0,
               llvm::MemoryBuffer::getMemBuffer(
                   llvm::StringRef(FatbinContent.data(), FatbinContent.size()),
                   "", false));

  HostCodeGenOpts.OffloadBinaryToEmbedFile = std::move(FatbinFileName);

  FatbinContent.clear();

  return llvm::Error::success();
}

IncrementalCUDADeviceParser::~IncrementalCUDADeviceParser() {}

} // namespace clang
