//==-- llvm/CodeGen/RegisterBank.h - Register Bank ---------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API of register banks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERSTRIPE_H
#define LLVM_CODEGEN_REGISTERSTRIPE_H

#include <cstdint>

namespace llvm {
// Forward declarations.
class RegisterStripeInfo;
class raw_ostream;
class TargetRegisterInfo;


class RegisterStripe {
private:
  unsigned ID;
  unsigned NumRegClasses;
  const char *Name;
  const uint32_t *CoveredClasses;

  friend RegisterStripeInfo;

public:
  constexpr RegisterStripe(unsigned ID, const char *Name,
                         const uint32_t *CoveredClasses, unsigned NumRegClasses)
      : ID(ID), NumRegClasses(NumRegClasses), Name(Name),
        CoveredClasses(CoveredClasses) {}

  unsigned getID() const { return ID; }

  const char *getName() const { return Name; }


  /// Check whether \p OtherRS is the same as this.
  bool operator==(const RegisterStripe &OtherRS) const;
  bool operator!=(const RegisterStripe &OtherRS) const {
    return !this->operator==(OtherRS);
  }

};

} // End namespace llvm.

#endif
