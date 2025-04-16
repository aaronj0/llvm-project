//===--- InterpreterUtils.h - Incremental Utils --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements some common utils used in the incremental library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INTERPRETER_UTILS_H
#define LLVM_CLANG_INTERPRETER_UTILS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/TypeVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Lex/PreprocessorOptions.h"

#include "clang/Sema/Lookup.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Errc.h"
#include "llvm/TargetParser/Host.h"

namespace clang {
IntegerLiteral *IntegerLiteralExpr(ASTContext &C, uint64_t Val);

Expr *CStyleCastPtrExpr(Sema &S, QualType Ty, Expr *E);

Expr *CStyleCastPtrExpr(Sema &S, QualType Ty, uintptr_t Ptr);

Sema::DeclGroupPtrTy CreateDGPtrFrom(Sema &S, Decl *D);

NamespaceDecl *LookupNamespace(Sema &S, llvm::StringRef Name,
                               const DeclContext *Within = nullptr);

NamedDecl *LookupNamed(Sema &S, llvm::StringRef Name,
                       const DeclContext *Within);

std::string GetFullTypeName(ASTContext &Ctx, QualType QT);
} // namespace clang

/// Custom error class for interpreter execution diagnostics.
class InterpExecutionError : public llvm::ErrorInfo<InterpExecutionError> {
public:
    // Unique identifier for the error class.
    static char ID;

    // Diagnostic flags.
    bool HasPTUConsistency;   // Non-fatal warning flag.
    bool HasJitSessionError;  // Fatal error flag.

    // Accumulated diagnostic messages.
    std::string PTUConsistencyMsg;
    std::string JitSessionErrorMsg;

    /// Default constructor.
    InterpExecutionError();

    /// Method to accumulate a PTU consistency (warning) diagnostic.
    void addPTUConsistency(llvm::StringRef ModuleID);

    /// Method to accumulate a JIT session error diagnostic.
    void addJitSessionError(llvm::StringRef ErrMsg);

    /// Log the error diagnostics.
    void log(llvm::raw_ostream &OS) const override;

    /// Convert error info to a standard error code.
    std::error_code convertToErrorCode() const override;
};

#endif
