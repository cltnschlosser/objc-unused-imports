#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <unordered_set>
#include <unordered_map>
#include <cstring>

using namespace llvm;
using namespace clang;
using namespace clang::tooling;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory toolCategory("objc-unused-imports options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");
static cl::opt<bool> DebugPrint("debug-print");

enum class SymbolType: std::size_t {
  ClassDeclaration = 0,
  Class = 1,
  TypedefDeclaration = 2,
  Type = 3,
  StructDeclaration = 4,
  Struct = 5,
  VariableDeclaration = 6,
  Variable = 7,
  FunctionDeclaration = 8,
  Function = 9,
  EnumDeclaration = 10,
  Enum = 11,
  ProtocolDeclaration = 12,
  Protocol = 13,
  MethodDeclaration = 14,
  Method = 15,
  EnumConstantDeclaration = 16,
  EnumConstant = 17,
  PropertyDeclaration = 18,
  Property = 19,
  MacroDefinition = 20,
  Macro = 21,
  ProtocolConformanceDeclaration = 22,
  ProtocolConformance = 23,
  CategoryDeclaration = 24,
  Category = 25
};

static std::string symbolTypeToString(const SymbolType& type) {
  switch (type) {
    case SymbolType::ClassDeclaration:
      return "ClassDeclaration";
    case SymbolType::Class:
      return "Class";
    case SymbolType::TypedefDeclaration:
      return "TypedefDeclaration";
    case SymbolType::Type:
      return "Type";
    case SymbolType::StructDeclaration:
      return "StructDeclaration";
    case SymbolType::Struct:
      return "Struct";
    case SymbolType::VariableDeclaration:
      return "VariableDeclaration";
    case SymbolType::Variable:
      return "Variable";
    case SymbolType::FunctionDeclaration:
      return "FunctionDeclaration";
    case SymbolType::Function:
      return "Function";
    case SymbolType::EnumDeclaration:
      return "EnumDeclaration";
    case SymbolType::Enum:
      return "Enum";
    case SymbolType::EnumConstantDeclaration:
      return "EnumConstantDeclaration";
    case SymbolType::EnumConstant:
      return "EnumConstant";
    case SymbolType::ProtocolDeclaration:
      return "ProtocolDeclaration";
    case SymbolType::Protocol:
      return "Protocol";
    case SymbolType::MethodDeclaration:
      return "MethodDeclaration";
    case SymbolType::Method:
      return "Method";
    case SymbolType::PropertyDeclaration:
      return "PropertyDeclaration";
    case SymbolType::Property:
      return "Property";
    case SymbolType::MacroDefinition:
      return "MacroDefinition";
    case SymbolType::Macro:
      return "Macro";
    case SymbolType::ProtocolConformanceDeclaration:
      return "ProtocolConformanceDeclaration";
    case SymbolType::ProtocolConformance:
      return "ProtocolConformance";
    case SymbolType::CategoryDeclaration:
      return "CategoryDeclaration";
    case SymbolType::Category:
      return "Category";
  }
}

namespace std {
  template <>
  struct hash<SymbolType>
  {
    std::size_t operator()(const SymbolType& type) const
    {
      return static_cast<std::size_t>(type);
    }
  };
}

struct Symbol {
  SymbolType type;
  std::string value;
  std::unordered_set<std::string> *classNames;

  Symbol(SymbolType type, std::string value) {
    this->type = type;
    this->value = value;
    this->classNames = nullptr;
  }

  bool operator==(const Symbol &other) const {
   return type == other.type && value == other.value;
  }
};

namespace std {
  template <>
  struct hash<Symbol>
  {
    std::size_t operator()(const Symbol& symbol) const
    {
      using std::size_t;
      using std::hash;
      using std::string;

      return (hash<string>()(symbol.value) << 8) | hash<SymbolType>()(symbol.type);
    }
  };
}

static std::unordered_map<std::string, std::unordered_set<Symbol>> symbolsForFile;
static std::unordered_map<std::string, unsigned int> lineNumbers;
static std::unordered_set<std::string> modulesImported;
static std::unordered_map<std::string, std::string> superClass;
static std::unordered_map<std::string, std::unordered_set<std::string>> macroDefinitions;
static std::unordered_map<std::string, std::unordered_set<std::string>> macroUsages;

void insertSymbol(std::unordered_set<Symbol>& set, Symbol symbol, std::string className) {
  if (className == "") {
    set.insert(symbol);
  } else {
    auto iter = set.find(symbol);
    if (iter == set.end()) {
      auto classNames = new std::unordered_set<std::string>();
      classNames->insert(className);
      symbol.classNames = classNames;
      set.insert(symbol);
    } else {
      iter->classNames->insert(className);
    }
  }
}

void insertSymbolForFile(std::string fileName, Symbol& symbol, std::string className) {
  auto iter = symbolsForFile.find(fileName);
  if (iter == symbolsForFile.end())
  {
    auto symbolSet = std::unordered_set<Symbol>();
    // Insert symbol before inserting to symbolsForFile, because a copy is made.
    insertSymbol(symbolSet, symbol, className);
    symbolsForFile.insert(std::pair<std::string, std::unordered_set<Symbol>>(fileName, symbolSet));
  } else {
    insertSymbol(iter->second, symbol, className);
  }
}

bool addSymbolIfModule(const SourceManager& sourceManager, FullSourceLoc& fullLocation, Symbol& symbol, std::string className = "") {
  std::pair<SourceLocation, StringRef> moduleInfo = sourceManager.getModuleImportLoc(fullLocation);
  if (moduleInfo.first.isValid()) {
    insertSymbolForFile(moduleInfo.second.str(), symbol, className);
    return true;
  }
  return false;
}

bool addSymbolIfIncludedByMain(const SourceManager& sourceManager, ASTContext *context, FullSourceLoc& fullLocation, Symbol& symbol, std::string className = "") {
  SourceLocation includeLocation = sourceManager.getIncludeLoc(fullLocation.getFileID());
  if (includeLocation.isValid()) {
    FullSourceLoc fullIncludeLocation = context->getFullLoc(includeLocation);
    if (fullIncludeLocation.isValid()) {
      FileID includedFileID = fullIncludeLocation.getFileID();
      if (includedFileID.isValid()) {
        FileID mainFileID = sourceManager.getMainFileID();
        if (mainFileID.isValid() && includedFileID == mainFileID) {
          const FileEntry *fileEntry = fullLocation.getFileEntry();
          if (fileEntry && fileEntry->isValid()) {
            StringRef filenameRef = fileEntry->getName();
            if (!filenameRef.empty()) {
              std::string filename = filenameRef.str();
              if (lineNumbers.find(filename) == lineNumbers.end()) {
                lineNumbers.insert(std::pair<std::string, unsigned int>(filename, fullIncludeLocation.getLineNumber()));
              }
              insertSymbolForFile(filename, symbol, className);
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

void addSymbolIfMain(const SourceManager& sourceManager, FullSourceLoc& fullLocation, Symbol& symbol, std::string className = "") {
  FileID declFileID = fullLocation.getFileID();
  FileID mainFileID = sourceManager.getMainFileID();
  if (declFileID.isValid() && mainFileID.isValid() && declFileID == mainFileID) {
    const FileEntry *fileEntry = fullLocation.getFileEntry();
    if (fileEntry && fileEntry->isValid()) {
      StringRef filename = fileEntry->getName();
      if (!filename.empty()) {
        insertSymbolForFile(filename.str(), symbol, className);
      }
    }
  }
}

class PPCallbacksTracker : public clang::PPCallbacks {
public:
  PPCallbacksTracker(clang::Preprocessor &PP, ASTContext *context) : preprocessor(PP), context(context) {}

  void MacroDefined(const clang::Token &macroNameToken,
                    const clang::MacroDirective *macroDirective) {
    if(macroDirective->isFromPCH()) {
      return;
    }

    FullSourceLoc fullLocation = context->getFullLoc(macroDirective->getLocation());
    if (!fullLocation.isValid()) {
      return;
    }

    Symbol symbol = Symbol(SymbolType::MacroDefinition, preprocessor.getSpelling(macroNameToken));

    const SourceManager& sourceManager = preprocessor.getSourceManager();
    if (addSymbolIfIncludedByMain(sourceManager, context, fullLocation, symbol)) {
      return;
    }
  }
  void MacroExpands(const clang::Token &macroNameToken,
                    const clang::MacroDefinition &macroDefinition,
                    clang::SourceRange range,
                    const clang::MacroArgs *args) {
    FullSourceLoc fullLocation = context->getFullLoc(range.getBegin());
    if (!fullLocation.isValid()) {
      return;
    }

    std::string name = preprocessor.getSpelling(macroNameToken);
    Symbol symbol = Symbol(SymbolType::Macro, name);

    const SourceManager& sourceManager = preprocessor.getSourceManager();
    addSymbolIfMain(sourceManager, fullLocation, symbol);

    // Modules are precompiled, so we need to check for macro definitions at time of use
    for (clang::ModuleMacro* moduleMacro : macroDefinition.getModuleMacros()) {
      if (moduleMacro) {
        if (clang::Module *module = moduleMacro->getOwningModule()) {
          Symbol moduleSymbol = Symbol(SymbolType::MacroDefinition, name);
          insertSymbolForFile(module->getTopLevelModule()->Name, moduleSymbol, "");
        }
      }
    }
  }
private:
  clang::Preprocessor &preprocessor;
  ASTContext *context;
};

class ObjcClassVisitor: public RecursiveASTVisitor<ObjcClassVisitor> {
public:
  explicit ObjcClassVisitor(ASTContext *context)
    : context(context) {}

  bool VisitImportDecl(ImportDecl *declaration) {
    FullSourceLoc fullLocation = context->getFullLoc(declaration->getLocStart());
    const SourceManager& sourceManager = context->getSourceManager();
    if (!fullLocation.isValid()) {
      return true;
    }
    FileID declFileID = fullLocation.getFileID();
    FileID mainFileID = sourceManager.getMainFileID();
    if (declFileID.isValid() && mainFileID.isValid() && declFileID == mainFileID) {
      std::string name = declaration->getImportedModule()->getFullModuleName();
      modulesImported.insert(name);
      lineNumbers[name] = fullLocation.getLineNumber();
    }
    return true;
  }

  bool VisitObjCInterfaceDecl(ObjCInterfaceDecl *declaration) {
    // Skip forward declarations
    if (!declaration->isThisDeclarationADefinition()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::ClassDeclaration, declaration->getNameAsString());

    ObjCInterfaceDecl *superDeclaration = declaration->getSuperClass();
    if (superDeclaration) {
      superClass.insert(std::pair<std::string, std::string>(declaration->getNameAsString(), superDeclaration->getNameAsString()));
    }

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }
    return true;
  }

  bool VisitObjCImplementationDecl(ObjCImplementationDecl *declaration) {
    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::Class, declaration->getNameAsString());

    addSymbolIfMain(fullLocation, symbol);

    return true;
  }

  bool VisitTypedefDecl(TypedefDecl *declaration) {
    // Only save final declarations (not forward declarations)
    if (declaration->getMostRecentDecl() != declaration) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::TypedefDeclaration, declaration->getNameAsString());

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }
    return true;
  }

  bool VisitRecordDecl(RecordDecl *declaration) {
    // Skip forward declarations
    if (!declaration->isThisDeclarationADefinition()) {
      return true;
    }

    // Ignore anonymous structs
    if (declaration->isAnonymousStructOrUnion()) {
      return true;
    }

    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::StructDeclaration, name.str());

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *declaration) {
    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    if (!declaration->hasGlobalStorage()) {
      QualType type = declaration->getType();
      if (type.isNull()) {
        return true;
      }
      Symbol localVarSymbol = { SymbolType::Type, qualTypeSimple(type) };
      addSymbolIfMain(fullLocation, localVarSymbol);
      return true;
    }

    Symbol symbol = Symbol(SymbolType::VariableDeclaration, name.str());

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }

    // Not technically needed, but seems like a good idea to keep it
    Symbol variableDefinition = Symbol(SymbolType::Variable, name.str());
    addSymbolIfMain(fullLocation, variableDefinition);
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *declaration) {
    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::FunctionDeclaration, name.str());

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }

    // Not technically needed, but seems like a good idea to keep it
    Symbol functionDefinition = Symbol(SymbolType::Function, name.str());
    addSymbolIfMain(fullLocation, functionDefinition);
    return true;
  }

  bool VisitEnumDecl(EnumDecl *declaration) {
    // Skip forward declarations
    if (!declaration->isThisDeclarationADefinition()) {
      return true;
    }

    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::EnumDeclaration, name.str());

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }
    return true;
  }

  bool VisitEnumConstantDecl(EnumConstantDecl *declaration) {
    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::EnumConstantDeclaration, name.str());

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }
    return true;
  }

  bool VisitObjCProtocolDecl(ObjCProtocolDecl *declaration) {
    // Skip forward declarations
    if (!declaration->isThisDeclarationADefinition()) {
      return true;
    }

    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::ProtocolDeclaration, name.str());

    if (addSymbolIfModule(fullLocation, symbol)) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol)) {
      return true;
    }
    return true;
  }

  bool VisitObjCCategoryDecl(ObjCCategoryDecl *declaration) {
    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol categorySymbol = Symbol(SymbolType::CategoryDeclaration, declaration->getNameAsString());
    addSymbolIfIncludedByMain(fullLocation, categorySymbol);

    for (auto protocol : declaration->getReferencedProtocols()) {
      StringRef name = protocol->getName();
      if (name.empty()) {
        continue;
      }
      Symbol symbol = Symbol(SymbolType::Protocol, name.str());
      addSymbolIfMain(fullLocation, symbol);

      auto *classDecl = declaration->getClassInterface();
      if (!classDecl) {
        continue;
      }
      StringRef className = classDecl->getName();
      if (className.empty()) {
        continue;
      }
      Symbol conformSymbol = Symbol(SymbolType::ProtocolConformanceDeclaration, name.str());
      if (addSymbolIfModule(fullLocation, conformSymbol, className.str())) {
        continue;
      }
      if (addSymbolIfIncludedByMain(fullLocation, conformSymbol, className.str())) {
        continue;
      }
    }
    return true;
  }

  bool VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *declaration) {
    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    Symbol categorySymbol = Symbol(SymbolType::Category, declaration->getNameAsString());
    addSymbolIfMain(fullLocation, categorySymbol);

    if (ObjCInterfaceDecl *classDeclaration = declaration->getClassInterface()) {
      Symbol classSymbol = Symbol(SymbolType::Class, classDeclaration->getNameAsString());
      addSymbolIfMain(fullLocation, classSymbol);
    }

    return true;
  }

  bool VisitObjCMethodDecl(ObjCMethodDecl *declaration) {
    Selector selector = declaration->getSelector();
    if (selector.isNull()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    StringRef nameRef;
    if (auto *parent = declaration->getParent()) {
      if (auto *classDecl = dyn_cast<ObjCInterfaceDecl>(parent)) {
        nameRef = classDecl->getName();
      } else if (auto *protocolDecl = dyn_cast<ObjCProtocolDecl>(parent)) {
        nameRef = protocolDecl->getName();
      } else if (auto *categoryDecl = dyn_cast<ObjCCategoryDecl>(parent)) {
        if (auto *classDecl = categoryDecl->getClassInterface()) {
          nameRef = classDecl->getName();
        } else {
          llvm::outs() << "error: MethodDecl has parent ObjCCategoryDecl with no class\n";
          return true;
        }
      } else if (auto *implDecl = dyn_cast<ObjCImplDecl>(parent)) {
        auto *classDecl = implDecl->getClassInterface();
        if (!classDecl) {
          return true;
        }
        StringRef className = classDecl->getName();
        if (className.empty()) {
          return true;
        }

        Symbol definitionSymbol = Symbol(SymbolType::Method, selector.getAsString());
        addSymbolIfMain(fullLocation, definitionSymbol, className.str());

        QualType returnType = declaration->getReturnType();
        if (!returnType.isNull()) {
          Symbol returnSymbol = Symbol(SymbolType::Type, qualTypeSimple(returnType));
          addSymbolIfMain(fullLocation, returnSymbol);
        }

        // This is terrible, but it's only here because we do "casts" to add protocol conformance (this really can't be safe).
        for (ParmVarDecl *param : declaration->parameters()) {
          QualType qualType = param->getOriginalType();
          if (qualType.isNull()) {
            continue;
          }
          const clang::Type *typePtr = qualType.getTypePtr();
          if (!typePtr) {
            continue;
          }
          auto string = qualTypeSimple(qualType);
          size_t index = 0;
          while (true) {
            auto end_index = string.find(',', index);
            std::string partial_string;
            if (end_index == std::string::npos) {
              partial_string = string.substr(index);
            } else {
              auto length = end_index - index;
              partial_string = string.substr(index, length);
            }
            Symbol protocolSymbol = Symbol(SymbolType::Type, partial_string);
            addSymbolIfMain(fullLocation, protocolSymbol);
            if (end_index == std::string::npos) {
              break;
            }
            index = end_index + 1;
          }
        }

        return true;
      } else {
        const char *kindName = parent->getDeclKindName();
        if (kindName) {
          llvm::outs() << "error: MethodDecl with unsupported parent: " << kindName << "\n";
        }
        return true;
      }
    } else {
      llvm::outs() << "error: MethodDecl has null parent\n";
      return true;
    }
    if (nameRef.empty()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::MethodDeclaration, selector.getAsString());

    if (addSymbolIfModule(fullLocation, symbol, nameRef.str())) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol, nameRef.str())) {
      return true;
    }

    return true;
  }

  bool VisitObjCMessageExpr(ObjCMessageExpr *expression) {
    Selector selector = expression->getSelector();
    if (selector.isNull()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(expression->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    QualType receiverType = expression->getReceiverType();
    if (receiverType.isNull()) {
      return true;
    }

    // Handle return type
    if (const ObjCMethodDecl *methodDecl = expression->getMethodDecl()) {
      QualType returnType = methodDecl->getReturnType();
      if (auto *returnTypePtr = returnType.getTypePtrOrNull()) {
        if (returnTypePtr->isObjCObjectPointerType()
         && !returnTypePtr->isObjCIdType()
         && !returnTypePtr->isObjCClassOrClassKindOfType()) {
          Symbol typeSymbol = Symbol(SymbolType::Type, qualTypeSimple(returnType));
          addSymbolIfMain(fullLocation, typeSymbol);
        }
      }
    }

    // HACK: use base type if not id, otherwise you subtype (assume protocol)
    if (auto *receiverTypePtr = receiverType.getTypePtr()) {
      if (auto *receiverClass = receiverTypePtr->getAsObjCInterfaceType()) {
        if (!receiverClass->isObjCId()) {
          QualType baseType = receiverClass->getBaseType();
          if (!baseType.isNull()) {
            Symbol symbol = Symbol(SymbolType::Method, selector.getAsString());
            addSymbolIfMain(fullLocation, symbol, qualTypeSimple(baseType));
            Symbol typeSymbol = Symbol(SymbolType::Type, qualTypeSimple(baseType));
            addSymbolIfMain(fullLocation, typeSymbol);
          }
        }
      } else if (auto *receiverClassPtr = receiverTypePtr->getAsObjCInterfacePointerType()) {
        if (!receiverClassPtr->isObjCIdType()) {
          QualType baseType = receiverClassPtr->getObjectType()->getBaseType();
          if (!baseType.isNull()) {
            Symbol symbol = Symbol(SymbolType::Method, selector.getAsString());
            addSymbolIfMain(fullLocation, symbol, qualTypeSimple(baseType));
            Symbol typeSymbol = Symbol(SymbolType::Type, qualTypeSimple(baseType));
            addSymbolIfMain(fullLocation, typeSymbol);
          }
        }
      } else if (receiverTypePtr->isObjCClassType()) {
        if (auto *receiverExpr = expression->getInstanceReceiver()) {
          if (PseudoObjectExpr *pseudoExpr = dyn_cast<PseudoObjectExpr>(receiverExpr)) {
            if (auto *propertyRefExpr = dyn_cast<ObjCPropertyRefExpr>(pseudoExpr->getSyntacticForm())) {
              QualType realType = propertyRefExpr->getReceiverType(*context);
              if (!realType.isNull()) {
                Symbol symbol = Symbol(SymbolType::Method, selector.getAsString());
                addSymbolIfMain(fullLocation, symbol, qualTypeSimple(realType));
              }
            }
          }
          // Might need to add other types here in the future
        }
      }
    }

    std::string receiverTypeName = qualTypeSimple(receiverType);

    Symbol symbol = Symbol(SymbolType::Method, selector.getAsString());
    addSymbolIfMain(fullLocation, symbol, receiverTypeName);
    if (expression->isClassMessage()) {
      Symbol typeSymbol = Symbol(SymbolType::Type, receiverTypeName);
      addSymbolIfMain(fullLocation, typeSymbol);
    }

    // Check parameters to see if protocol conformance is needed
    auto arguments = expression->arg_begin();
    const ObjCMethodDecl *declaration = expression->getMethodDecl();
    for (ParmVarDecl *param : declaration->parameters()) {
      auto *arg = *arguments;
      arguments++;
      if (arguments == expression->arg_end()) {
        return true;
      }
      QualType argType = arg->IgnoreImpCasts()->getType();
      if (argType.isNull()) {
        continue;
      }

      QualType qualType = param->getOriginalType();
      if (qualType.isNull()) {
        continue;
      }
      const clang::Type *typePtr = qualType.getTypePtr();
      if (!typePtr) {
        continue;
      }
      if (typePtr->isObjCQualifiedIdType() || typePtr->isObjCQualifiedClassType()) {
        Symbol protocolSymbol = Symbol(SymbolType::ProtocolConformance, qualTypeSimple(qualType));
        addSymbolIfMain(fullLocation, protocolSymbol, qualTypeSimple(argType));
      }
    }

    return true;
  }

  bool VisitObjCPropertyDecl(ObjCPropertyDecl *declaration) {
    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    ObjCInterfaceDecl *classDecl = nullptr;
    if (ObjCInterfaceDecl *interfaceDecl = dyn_cast<ObjCInterfaceDecl>(declaration->getDeclContext())) {
      classDecl = interfaceDecl;
    } else if (ObjCCategoryDecl *categoryDecl = dyn_cast<ObjCCategoryDecl>(declaration->getDeclContext())) {
      classDecl = categoryDecl->getClassInterface();
    }
    if (!classDecl) {
      return true;
    }
    StringRef className = classDecl->getName();
    if (className.empty()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::PropertyDeclaration, name.str());

    if (addSymbolIfModule(fullLocation, symbol, className.str())) {
      return true;
    }
    if (addSymbolIfIncludedByMain(fullLocation, symbol, className.str())) {
      return true;
    }

    QualType type = declaration->getType();
    if (type.isNull()) {
      return true;
    }
    Symbol typeSymbol = Symbol(SymbolType::Type, qualTypeSimple(type));
    addSymbolIfMain(fullLocation, typeSymbol);
    return true;
  }

  bool VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *expression) {
    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(expression->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    if (expression->isImplicitProperty()) {
      return true;
    }
    auto *declaration = expression->getExplicitProperty();
    if (!declaration) {
      return true;
    }

    QualType receiver = expression->getReceiverType(*context);
    if (receiver.isNull()) {
      return true;
    }

    StringRef name = declaration->getName();
    if (name.empty()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::Property, name.str());
    addSymbolIfMain(fullLocation, symbol, qualTypeSimple(receiver));

    return true;
  }

  bool VisitParmVarDecl(ParmVarDecl *declaration) {
    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(declaration->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    auto type = declaration->getOriginalType();
    if (type.isNull()) {
      return true;
    }

    Symbol symbol = Symbol(SymbolType::Type, qualTypeSimple(type));
    addSymbolIfMain(fullLocation, symbol);

    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *expression) {
    FullSourceLoc fullLocation = context->getFullLoc(context->getSourceManager().getFileLoc(expression->getLocStart()));
    if (!fullLocation.isValid()) {
      return true;
    }

    auto *declaration = expression->getFoundDecl();

    StringRef nameRef = declaration->getName();
    if (nameRef.empty()) {
      return true;
    }

    SymbolType type;
    std::string name = nameRef.str();
    if (strcmp(declaration->getDeclKindName(), "Var") == 0) {
      VarDecl *varDecl = (VarDecl *) declaration;
      if (!varDecl->hasGlobalStorage() || varDecl->isStaticLocal()) {
        return true;
      }
      type = SymbolType::Variable;
    } else if (strcmp(declaration->getDeclKindName(), "Function") == 0) {
      type = SymbolType::Function;
    } else if (strcmp(declaration->getDeclKindName(), "EnumConstant") == 0) {
      type = SymbolType::EnumConstant;
    } else if (strcmp(declaration->getDeclKindName(), "ParmVar") == 0) {
      // Skip ParmVar usage
      return true;
    } else if (strcmp(declaration->getDeclKindName(), "ImplicitParam") == 0) {
      // Skip ParmVar usage
      return true;
    } else {
      llvm::outs() << "error: Unknown DeclKind: " << declaration->getDeclKindName() << " - " << name << "\n";
      const FileEntry *file = fullLocation.getFileEntry();
      if (file) {
        llvm::outs() << "InFile: " << file->getName() << "\n";
      }
      return true;
    }

     Symbol symbol = Symbol(type, name);
     addSymbolIfMain(fullLocation, symbol);

    return true;
  }

private:
  ASTContext *context;

  bool addSymbolIfModule(FullSourceLoc& fullLocation, Symbol& symbol, std::string className = "") {
    const SourceManager& sourceManager = context->getSourceManager();
    return ::addSymbolIfModule(sourceManager, fullLocation, symbol, className);
  }

  bool addSymbolIfIncludedByMain(FullSourceLoc& fullLocation, Symbol& symbol, std::string className = "") {
    const SourceManager& sourceManager = context->getSourceManager();
    return ::addSymbolIfIncludedByMain(sourceManager, context, fullLocation, symbol, className);
  }

  void addSymbolIfMain(FullSourceLoc& fullLocation, Symbol& symbol, std::string className = "") {
    const SourceManager& sourceManager = context->getSourceManager();
    return ::addSymbolIfMain(sourceManager, fullLocation, symbol, className);
  }

  std::string qualTypeSimple(QualType type) {
    std::string fullString = type.stripObjCKindOfType(*context).getUnqualifiedType().getAsString();
    std::string string = getUpToFirstSpace(fullString);
    auto start_index = string.find_first_of('<');
    if (start_index == std::string::npos) {
      return string;
    }
    auto end_index = string.find_first_of('>');
    if (end_index == std::string::npos) {
      return string.substr(start_index + 1);
    }
    if (start_index > end_index) {
      return string;
    }
    auto length = end_index - start_index;
    return string.substr(start_index + 1, length - 1);
  }

  std::string getUpToFirstSpace(std::string string) {
    auto index = string.find_first_of(' ');
    if (index == std::string::npos) {
      return string;
    } else {
      return string.substr(0, index);
    }
  }
};

class ObjcClassConsumer : public clang::ASTConsumer {
public:
  explicit ObjcClassConsumer(ASTContext *context, Preprocessor &PP)
    : visitor(context) {
      PP.addPPCallbacks(llvm::make_unique<PPCallbacksTracker>(PP, context));
    }

  virtual void HandleTranslationUnit(clang::ASTContext &context) {
    visitor.TraverseDecl(context.getTranslationUnitDecl());
  }
private:
  ObjcClassVisitor visitor;
};

class ObjcClassAction : public clang::ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
    clang::CompilerInstance &compiler, llvm::StringRef inFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new ObjcClassConsumer(&compiler.getASTContext(), compiler.getPreprocessor()));
  }
};

bool hasEnding(std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

bool isSameOrSubClass(std::string referenceClass, std::string testClass) {
  auto className = testClass;
  while(true) {
    if (referenceClass == className) {
      return true;
    }
    auto iter = superClass.find(className);
    if (iter == superClass.end()) {
      return false;
    } else {
      className = iter->second;
    }
  }

  return false;
}

bool matchWithClass(const Symbol &symbol, SymbolType type, std::unordered_set<Symbol> symbols) {
  auto mainFileSymbol = symbols.find(Symbol(type, symbol.value));
  if (mainFileSymbol == symbols.end()) {
    return false;
  }

  if (!symbol.classNames || !mainFileSymbol->classNames) {
    return false;
  }

  for (auto &className : *(symbol.classNames)) {
    for (auto &mainFileClassName : *(mainFileSymbol->classNames)) {
      // Be conservative with methods called on id
      if (mainFileClassName == "id") {
        return true;
      }
      if (isSameOrSubClass(className, mainFileClassName)) {
        return true;
      }
    }
  }
  return false;
}

bool symbolUsed(const Symbol &symbol, std::unordered_set<Symbol> symbols) {
  switch (symbol.type) {
    case SymbolType::ClassDeclaration:
      return symbols.find(Symbol(SymbolType::Class, symbol.value)) != symbols.end() || symbols.find(Symbol(SymbolType::Type, symbol.value)) != symbols.end();
    case SymbolType::TypedefDeclaration:
      return symbols.find(Symbol(SymbolType::Type, symbol.value)) != symbols.end();
    case SymbolType::StructDeclaration:
      return symbols.find(Symbol(SymbolType::Struct, symbol.value)) != symbols.end();
    case SymbolType::VariableDeclaration:
      return symbols.find(Symbol(SymbolType::Variable, symbol.value)) != symbols.end();
    case SymbolType::FunctionDeclaration:
      return symbols.find(Symbol(SymbolType::Function, symbol.value)) != symbols.end();
    case SymbolType::EnumDeclaration:
      return symbols.find(Symbol(SymbolType::Enum, symbol.value)) != symbols.end();
    case SymbolType::ProtocolDeclaration:
      return symbols.find(Symbol(SymbolType::Protocol, symbol.value)) != symbols.end() || symbols.find(Symbol(SymbolType::Type, symbol.value)) != symbols.end();
    case SymbolType::EnumConstantDeclaration:
      return symbols.find(Symbol(SymbolType::EnumConstant, symbol.value)) != symbols.end();
    case SymbolType::MethodDeclaration:
      return matchWithClass(symbol, SymbolType::Method, symbols);
    case SymbolType::PropertyDeclaration:
      return matchWithClass(symbol, SymbolType::Property, symbols) || matchWithClass(symbol, SymbolType::Method, symbols);
    case SymbolType::MacroDefinition:
      return symbols.find(Symbol(SymbolType::Macro, symbol.value)) != symbols.end();
    case SymbolType::ProtocolConformanceDeclaration:
      return matchWithClass(symbol, SymbolType::ProtocolConformance, symbols);
    case SymbolType::CategoryDeclaration:
      return symbols.find(Symbol(SymbolType::Category, symbol.value)) != symbols.end();
    default:
      return false;
  }
}

bool anySymbolUsed(std::unordered_set<Symbol> symbols, std::unordered_set<Symbol> referenceSymbols) {
  for (auto &symbol : symbols) {
    if (symbolUsed(symbol, referenceSymbols)) {
      return true;
    }
  }
  return false;
}

int main(int argc, const char **argv) {
  CommonOptionsParser optionsParser(argc, argv, toolCategory);
  ClangTool tool(optionsParser.getCompilations(),
                 optionsParser.getSourcePathList());
  int result = tool.run(newFrontendActionFactory<ObjcClassAction>().get());

  std::string file = optionsParser.getSourcePathList().front();
  auto mainSymbolsIter = symbolsForFile.find(file);
  std::unordered_set<Symbol> mainSymbols;
  if (mainSymbolsIter != symbolsForFile.end()) {
    mainSymbols = mainSymbolsIter->second;
  } else {
    mainSymbols = std::unordered_set<Symbol>();
  }

  if (DebugPrint) {
    for (auto &pair : symbolsForFile) {
      llvm::outs() << "File: " << pair.first << "\n";
      for (auto &symbol : pair.second) {
        if (symbol.classNames) {
          for (auto name : *(symbol.classNames)) {
            llvm::outs() << symbolTypeToString(symbol.type) << ": " << name << " " << symbol.value << "\n";
          }
        } else {
          llvm::outs() << symbolTypeToString(symbol.type) << ": " << symbol.value << "\n";
        }
      }
      llvm::outs() << "\n";
    }

    llvm::outs() << "\n" << "Modules:\n";
    for (auto &module : modulesImported) {
      llvm::outs() << module << "\n";
    }
    llvm::outs() << "\n";

    llvm::outs() << "Unused Imports:\n";
  }
  for (auto &pair : symbolsForFile) {
    if (!hasEnding(pair.first, ".h") && modulesImported.find(pair.first) == modulesImported.end()) {
      continue;
    }

    if (!anySymbolUsed(pair.second, mainSymbols)) {
      llvm::outs() << file << ":" << lineNumbers[pair.first] << ": warning: Unused import " << pair.first << "\n";
    }
  }

  return result;
}