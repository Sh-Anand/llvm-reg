//===- llvm/CodeGen/RegisterBankInfo.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API for the register bank info.
/// This API is responsible for handling the register banks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERSTRIPEINFO_H
#define LLVM_CODEGEN_REGISTERSTRIPEINFO_H

#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/RegisterStripe.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <memory>

namespace llvm {

class MachineRegisterInfo;
class raw_ostream;
class TargetRegisterInfo;

class RegisterStripeInfo {

protected:
  /// Hold the set of supported register stripes.
  const RegisterStripe **RegStripes;

  /// Total number of register stripes.
  unsigned NumRegStripes;
    
  RegisterStripeInfo(const RegisterStripe **RegStripes, unsigned NumRegStripes);

  RegisterStripeInfo() {
    llvm_unreachable("This constructor should not be executed");
  }

  const RegisterStripe &getRegStripe(unsigned ID) {
    assert(ID < getNumRegStripes() && "Accessing an unknown register stripe");
    return *RegStripes[ID];
  }

public:
  virtual ~RegisterStripeInfo() = default;

  /// Get the register stripe identified by \p ID.
  const RegisterStripe &getRegStripe(unsigned ID) const {
    return const_cast<RegisterStripeInfo *>(this)->getRegStripe(ID);
  }

  /// Get the register stripe of \p Reg.
  /// If Reg has not been assigned a register, a register class,
  /// or a register stripe, then this returns nullptr.
  ///
  /// \pre Reg != 0 (NoRegister)
  const RegisterStripe *getRegStripe(Register Reg, const MachineRegisterInfo &MRI,
                                 const TargetRegisterInfo &TRI) const;

  /// Get the total number of register stripes.
  unsigned getNumRegStripes() const { return NumRegStripes; }
};
} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERSTRIPEINFO_H
