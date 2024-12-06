
#include "llvm/CodeGen/RegisterStripeInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "registerstripeinfo"

using namespace llvm;

const RegisterStripe *RegisterStripeInfo::getRegStripe(Register Reg, const MachineRegisterInfo &MRI,
                             const TargetRegisterInfo &TRI) const {
  return nullptr;
}


