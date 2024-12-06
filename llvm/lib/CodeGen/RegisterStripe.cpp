#include "llvm/CodeGen/RegisterStripe.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "registerstripe"

using namespace llvm;


bool RegisterStripe::operator==(const RegisterStripe &OtherRS) const {
  // There must be only one instance of a given register stripe alive
  // for the whole compilation.
  // The RegisterBankInfo is supposed to enforce that.
  assert((OtherRS.getID() != getID() || &OtherRS == this) &&
         "ID does not uniquely identify a RegisterStripe");
  return &OtherRS == this;
}

