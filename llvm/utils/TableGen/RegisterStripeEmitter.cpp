#include "Common/CodeGenRegisters.h"
#include "Common/CodeGenTarget.h"
#include "Common/InfoByHwMode.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TGTimer.h"
#include "llvm/TableGen/TableGenBackend.h"
    
#define DEBUG_TYPE "register-stripe-emitter"

using namespace llvm;

namespace {
class RegisterStripe {

  /// A vector of register classes that are included in the register Stripe.
  typedef std::vector<const CodeGenRegisterClass *> RegisterClassesTy;

private:
  const Record &TheDef;

  /// The register classes that are covered by the register Stripe.
  RegisterClassesTy RCs;

public:
  RegisterStripe(const Record &TheDef, unsigned NumModeIds)
      : TheDef(TheDef) {}

  /// Get the human-readable name for the Stripe.
  StringRef getName() const { return TheDef.getValueAsString("Name"); }
  /// Get the name of the enumerator in the ID enumeration.
  std::string getEnumeratorName() const {
    return (TheDef.getName() + "ID").str();
  }

  /// Get the name of the array holding the register class coverage data;
  std::string getCoverageArrayName() const {
    return (TheDef.getName() + "CoverageData").str();
  }

  /// Get the name of the global instance variable.
  StringRef getInstanceVarName() const { return TheDef.getName(); }

  const Record &getDef() const { return TheDef; }

  std::vector<const CodeGenRegisterClass *>
  getExplicitlySpecifiedRegisterClasses(
      const CodeGenRegStripe &RegisterClassHierarchy) const {
    std::vector<const CodeGenRegisterClass *> RCs;
    for (const auto *RCDef : getDef().getValueAsListOfDefs("RegisterClasses"))
      RCs.push_back(RegisterClassHierarchy.getRegClass(RCDef));
    return RCs;
  }

  /// Add a register class to the Stripe without duplicates.
  void addRegisterClass(const CodeGenRegisterClass *RC) {
    if (llvm::is_contained(RCs, RC))
      return;

    RCs.emplace_back(RC);
  }

  iterator_range<typename RegisterClassesTy::const_iterator>
  register_classes() const {
    return llvm::make_range(RCs.begin(), RCs.end());
  }
};

class RegisterStripeEmitter {
private:
  const CodeGenTarget Target;
  const RecordKeeper &Records;

  void emitHeader(raw_ostream &OS, const StringRef TargetName,
                  ArrayRef<RegisterStripe> Stripes);
  void emitBaseClassDefinition(raw_ostream &OS, const StringRef TargetName,
                               ArrayRef<RegisterStripe> Stripes);
  void emitBaseClassImplementation(raw_ostream &OS, const StringRef TargetName,
                                   ArrayRef<RegisterStripe> Stripes);

public:
  RegisterStripeEmitter(const RecordKeeper &R) : Target(R), Records(R) {}

  void run(raw_ostream &OS);
};

} // end anonymous namespace

/// Emit code to declare the ID enumeration and external global instance
/// variables.
void RegisterStripeEmitter::emitHeader(raw_ostream &OS,
                                     const StringRef TargetName,
                                     ArrayRef<RegisterStripe> Stripes) {
  // <Target>RegisterStripeInfo.h
  OS << "namespace llvm {\n"
     << "namespace " << TargetName << " {\n"
     << "enum : unsigned {\n";

  OS << "  InvalidRegStripeID = ~0u,\n";
  unsigned ID = 0;
  for (const auto &Stripe : Stripes)
    OS << "  " << Stripe.getEnumeratorName() << " = " << ID++ << ",\n";
  OS << "  NumRegisterStripes,\n"
     << "};\n"
     << "} // end namespace " << TargetName << "\n"
     << "} // end namespace llvm\n";
}

/// Emit declarations of the <Target>GenRegisterStripeInfo class.
void RegisterStripeEmitter::emitBaseClassDefinition(
    raw_ostream &OS, const StringRef TargetName, ArrayRef<RegisterStripe> Stripes) {
  OS << "private:\n"
     << "  static const RegisterStripe *RegStripes[];\n"
     << "  static const unsigned Sizes[];\n\n"
     << "public:\n"
     << "  const RegisterStripe &getRegStripeFromRegClass(const "
        "TargetRegisterClass &RC, LLT Ty) const override;\n"
     << "protected:\n"
     << "  " << TargetName << "GenRegisterStripeInfo(unsigned HwMode = 0);\n"
     << "\n";
}

/// Visit each register class belonging to the given register Stripe.
///
/// A class belongs to the Stripe iff any of these apply:
/// * It is explicitly specified
/// * It is a subclass of a class that is a member.
/// * It is a class containing subregisters of the registers of a class that
///   is a member. This is known as a subreg-class.
///
/// This function must be called for each explicitly specified register class.
///
/// \param RC The register class to search.
/// \param Kind A debug string containing the path the visitor took to reach RC.
/// \param VisitFn The action to take for each class visited. It may be called
///                multiple times for a given class if there are multiple paths
///                to the class.
static void visitRegisterStripeClasses(
    const CodeGenRegStripe &RegisterClassHierarchy,
    const CodeGenRegisterClass *RC, const Twine &Kind,
    std::function<void(const CodeGenRegisterClass *, StringRef)> VisitFn,
    SmallPtrSetImpl<const CodeGenRegisterClass *> &VisitedRCs) {

  // Make sure we only visit each class once to avoid infinite loops.
  if (!VisitedRCs.insert(RC).second)
    return;

  // Visit each explicitly named class.
  VisitFn(RC, Kind.str());

  for (const auto &PossibleSubclass : RegisterClassHierarchy.getRegClasses()) {
    std::string TmpKind =
        (Kind + " (" + PossibleSubclass.getName() + ")").str();

    // Visit each subclass of an explicitly named class.
    if (RC != &PossibleSubclass && RC->hasSubClass(&PossibleSubclass))
      visitRegisterStripeClasses(RegisterClassHierarchy, &PossibleSubclass,
                               TmpKind + " " + RC->getName() + " subclass",
                               VisitFn, VisitedRCs);
  }
}

void RegisterStripeEmitter::emitBaseClassImplementation(
    raw_ostream &OS, StringRef TargetName, ArrayRef<RegisterStripe> Stripes) {
  const CodeGenRegStripe &RegisterClassHierarchy = Target.getRegStripe();
  const CodeGenHwModes &CGH = Target.getHwModes();

  OS << "namespace llvm {\n"
     << "namespace " << TargetName << " {\n";
  for (const auto &Stripe : Stripes) {
    std::vector<std::vector<const CodeGenRegisterClass *>> RCsGroupedByWord(
        (RegisterClassHierarchy.getRegClasses().size() + 31) / 32);

    for (const auto &RC : Stripe.register_classes())
      RCsGroupedByWord[RC->EnumValue / 32].push_back(RC);

    OS << "const uint32_t " << Stripe.getCoverageArrayName() << "[] = {\n";
    unsigned LowestIdxInWord = 0;
    for (const auto &RCs : RCsGroupedByWord) {
      OS << "    // " << LowestIdxInWord << "-" << (LowestIdxInWord + 31)
         << "\n";
      for (const auto &RC : RCs) {
        OS << "    (1u << (" << RC->getQualifiedIdName() << " - "
           << LowestIdxInWord << ")) |\n";
      }
      OS << "    0,\n";
      LowestIdxInWord += 32;
    }
    OS << "};\n";
  }
  OS << "\n";

  for (const auto &Stripe : Stripes) {
    std::string QualifiedStripeID =
        (TargetName + "::" + Stripe.getEnumeratorName()).str();
    OS << "constexpr RegisterStripe " << Stripe.getInstanceVarName() << "(/* ID */ "
       << QualifiedStripeID << ", /* Name */ \"" << Stripe.getName() << "\", "
       << "/* CoveredRegClasses */ " << Stripe.getCoverageArrayName()
       << ", /* NumRegClasses */ "
       << RegisterClassHierarchy.getRegClasses().size() << ");\n";
  }
  OS << "} // end namespace " << TargetName << "\n"
     << "\n";

  OS << "const RegisterStripe *" << TargetName
     << "GenRegisterStripeInfo::RegStripes[] = {\n";
  for (const auto &Stripe : Stripes)
    OS << "    &" << TargetName << "::" << Stripe.getInstanceVarName() << ",\n";
  OS << "};\n\n";

  unsigned NumModeIds = CGH.getNumModeIds();
  OS << "const unsigned " << TargetName << "GenRegisterStripeInfo::Sizes[] = {\n";
  for (unsigned M = 0; M < NumModeIds; ++M) {
    OS << "    // Mode = " << M << " (";
    if (M == DefaultMode)
      OS << "Default";
    else
      OS << CGH.getMode(M).Name;
    OS << ")\n";
    // for (const auto &Stripe : Stripes) {
    //   const CodeGenRegisterClass &RC = *Stripe.getRCWithLargestRegSize(M);
    //   unsigned Size = RC.RSI.get(M).SpillSize;
    //   OS << "    " << Size << ",\n";
    // }
  }
  OS << "};\n\n";

  OS << TargetName << "GenRegisterStripeInfo::" << TargetName
     << "GenRegisterStripeInfo(unsigned HwMode)\n"
     << "    : RegisterStripeInfo(RegStripes, " << TargetName
     << "::NumRegisterStripes, Sizes, HwMode) {\n"
     << "  // Assert that RegStripe indices match their ID's\n"
     << "#ifndef NDEBUG\n"
     << "  for (auto RB : enumerate(RegStripes))\n"
     << "    assert(RB.index() == RB.value()->getID() && \"Index != ID\");\n"
     << "#endif // NDEBUG\n"
     << "}\n";

  uint32_t NumRegStripes = Stripes.size();
  uint32_t BitSize = NextPowerOf2(Log2_32(NumRegStripes));
  uint32_t ElemsPerWord = 32 / BitSize;
  uint32_t BitMask = (1 << BitSize) - 1;
  bool HasAmbigousOrMissingEntry = false;
  struct Entry {
    std::string RCIdName;
    std::string RBIdName;
  };
  SmallVector<Entry, 0> Entries;
  for (const auto &Stripe : Stripes) {
    for (const auto *RC : Stripe.register_classes()) {
      if (RC->EnumValue >= Entries.size())
        Entries.resize(RC->EnumValue + 1);
      Entry &E = Entries[RC->EnumValue];
      E.RCIdName = RC->getIdName();
      if (!E.RBIdName.empty()) {
        HasAmbigousOrMissingEntry = true;
        E.RBIdName = "InvalidRegStripeID";
      } else {
        E.RBIdName = (TargetName + "::" + Stripe.getEnumeratorName()).str();
      }
    }
  }
  for (auto &E : Entries) {
    if (E.RBIdName.empty()) {
      HasAmbigousOrMissingEntry = true;
      E.RBIdName = "InvalidRegStripeID";
    }
  }
  OS << "const RegisterStripe &\n"
     << TargetName
     << "GenRegisterStripeInfo::getRegStripeFromRegClass"
        "(const TargetRegisterClass &RC, LLT) const {\n";
  if (HasAmbigousOrMissingEntry) {
    OS << "  constexpr uint32_t InvalidRegStripeID = uint32_t("
       << TargetName + "::InvalidRegStripeID) & " << BitMask << ";\n";
  }
  unsigned TableSize =
      Entries.size() / ElemsPerWord + ((Entries.size() % ElemsPerWord) > 0);
  OS << "  static const uint32_t RegClass2RegStripe[" << TableSize << "] = {\n";
  uint32_t Shift = 32 - BitSize;
  bool First = true;
  std::string TrailingComment;
  for (auto &E : Entries) {
    Shift += BitSize;
    if (Shift == 32) {
      Shift = 0;
      if (First)
        First = false;
      else
        OS << ',' << TrailingComment << '\n';
    } else {
      OS << " |" << TrailingComment << '\n';
    }
    OS << "    ("
       << (E.RBIdName.empty()
               ? "InvalidRegStripeID"
               : Twine("uint32_t(").concat(E.RBIdName).concat(")").str())
       << " << " << Shift << ')';
    if (!E.RCIdName.empty())
      TrailingComment = " // " + E.RCIdName;
    else
      TrailingComment = "";
  }
  OS << TrailingComment
     << "\n  };\n"
        "  const unsigned RegClassID = RC.getID();\n"
        "  if (LLVM_LIKELY(RegClassID < "
     << Entries.size()
     << ")) {\n"
        "    unsigned RegStripeID = (RegClass2RegStripe[RegClassID / "
     << ElemsPerWord << "] >> ((RegClassID % " << ElemsPerWord << ") * "
     << BitSize << ")) & " << BitMask << ";\n";
  if (HasAmbigousOrMissingEntry) {
    OS << "    if (RegStripeID != InvalidRegStripeID)\n"
          "      return getRegStripe(RegStripeID);\n";
  } else
    OS << "    return getRegStripe(RegStripeID);\n";
  OS << "  }\n"
        "  llvm_unreachable(llvm::Twine(\"Target needs to handle register "
        "class ID "
        "0x\").concat(llvm::Twine::utohexstr(RegClassID)).str().c_str());\n"
        "}\n";

  OS << "} // end namespace llvm\n";
}

void RegisterStripeEmitter::run(raw_ostream &OS) {
  StringRef TargetName = Target.getName();
  const CodeGenRegStripe &RegisterClassHierarchy = Target.getRegStripe();
  const CodeGenHwModes &CGH = Target.getHwModes();

  TGTimer &Timer = Records.getTimer();
  Timer.startTimer("Analyze records");
  std::vector<RegisterStripe> Stripes;
  for (const auto &V : Records.getAllDerivedDefinitions("RegisterStripe")) {
    SmallPtrSet<const CodeGenRegisterClass *, 8> VisitedRCs;
    RegisterStripe Stripe(*V, CGH.getNumModeIds());

    for (const CodeGenRegisterClass *RC :
         Stripe.getExplicitlySpecifiedRegisterClasses(RegisterClassHierarchy)) {
      visitRegisterStripeClasses(
          RegisterClassHierarchy, RC, "explicit",
          [&Stripe](const CodeGenRegisterClass *RC, StringRef Kind) {
            LLVM_DEBUG(dbgs()
                       << "Added " << RC->getName() << "(" << Kind << ")\n");
            Stripe.addRegisterClass(RC);
          },
          VisitedRCs);
    }

    Stripes.push_back(Stripe);
  }

  // Warn about ambiguous MIR caused by register Stripe/class name clashes.
  Timer.startTimer("Warn ambiguous");
  for (const auto &Class : RegisterClassHierarchy.getRegClasses()) {
    for (const auto &Stripe : Stripes) {
      if (Stripe.getName().lower() == StringRef(Class.getName()).lower()) {
        PrintWarning(Stripe.getDef().getLoc(), "Register Stripe names should be "
                                             "distinct from register classes "
                                             "to avoid ambiguous MIR");
        PrintNote(Stripe.getDef().getLoc(), "RegisterStripe was declared here");
        PrintNote(Class.getDef()->getLoc(), "RegisterClass was declared here");
      }
    }
  }

  Timer.startTimer("Emit output");
  emitSourceFileHeader("Register Stripe Source Fragments", OS);
  OS << "#ifdef GET_REGStripe_DECLARATIONS\n"
     << "#undef GET_REGStripe_DECLARATIONS\n";
  emitHeader(OS, TargetName, Stripes);
  OS << "#endif // GET_REGStripe_DECLARATIONS\n\n"
     << "#ifdef GET_TARGET_REGStripe_CLASS\n"
     << "#undef GET_TARGET_REGStripe_CLASS\n";
  emitBaseClassDefinition(OS, TargetName, Stripes);
  OS << "#endif // GET_TARGET_REGStripe_CLASS\n\n"
     << "#ifdef GET_TARGET_REGStripe_IMPL\n"
     << "#undef GET_TARGET_REGStripe_IMPL\n";
  emitBaseClassImplementation(OS, TargetName, Stripes);
  OS << "#endif // GET_TARGET_REGStripe_IMPL\n";
}

static TableGen::Emitter::OptClass<RegisterStripeEmitter>
    X("gen-register-stripe", "Generate register stripe descriptions");
