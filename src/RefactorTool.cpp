#include "RefactorTool.h"

#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"

#include <cctype>
#include <fstream>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

namespace {

llvm::cl::OptionCategory toolCategory("refactor-tool options");
llvm::cl::opt<std::string> logFile("log-file", llvm::cl::desc("Path to refactoring log file"),
                                   llvm::cl::init("refactor.log"), llvm::cl::cat(toolCategory));

bool isEditableMainFileLocation(SourceLocation location, const SourceManager &sm) {
    if (location.isInvalid() || location.isMacroID()) {
        return false;
    }
    return sm.isWrittenInMainFile(sm.getSpellingLoc(location));
}

unsigned locationKey(SourceLocation location, const SourceManager &sm) {
    return sm.getFileOffset(sm.getSpellingLoc(location));
}

void logChange(llvm::StringRef change, llvm::StringRef entity, SourceLocation location, const SourceManager &sm) {
    if (logFile.empty()) {
        return;
    }

    const PresumedLoc presumed = sm.getPresumedLoc(sm.getSpellingLoc(location));
    if (presumed.isInvalid()) {
        return;
    }

    std::ofstream output(logFile.getValue(), std::ios::app);
    if (!output) {
        llvm::errs() << "Cannot open refactoring log file: " << logFile.getValue() << "\n";
        return;
    }

    output << change.str() << " | " << presumed.getFilename() << ':' << presumed.getLine() << ':'
           << presumed.getColumn();
    if (!entity.empty()) {
        output << " | " << entity.str();
    }
    output << '\n';
}

std::string insertionBeforeToken(SourceLocation tokenLocation, const SourceManager &sm, llvm::StringRef text) {
    const SourceLocation spelling = sm.getSpellingLoc(tokenLocation);
    const unsigned offset = sm.getFileOffset(spelling);
    if (offset == 0) {
        return text.str() + " ";
    }

    bool invalid = false;
    const char *tokenData = sm.getCharacterData(spelling, &invalid);
    if (invalid || tokenData == nullptr) {
        return " " + text.str() + " ";
    }

    const unsigned char previous = static_cast<unsigned char>(tokenData[-1]);
    if (std::isspace(previous) != 0) {
        return text.str() + " ";
    }
    return " " + text.str() + " ";
}

SourceLocation findOverrideInsertionPoint(const CXXMethodDecl *method, SourceManager &sm,
                                          const LangOptions &langOptions) {
    if (method == nullptr || method->getTypeSourceInfo() == nullptr) {
        return {};
    }

    const TypeLoc typeLoc = method->getTypeSourceInfo()->getTypeLoc();
    const FunctionProtoTypeLoc functionTypeLoc = typeLoc.getAs<FunctionProtoTypeLoc>();
    if (functionTypeLoc.isNull() || functionTypeLoc.getRParenLoc().isInvalid()) {
        return {};
    }

    SourceLocation current = Lexer::getLocForEndOfToken(functionTypeLoc.getRParenLoc(), 0, sm, langOptions);
    if (current.isInvalid()) {
        return {};
    }

    unsigned parenDepth = 0;
    unsigned squareDepth = 0;

    for (unsigned tokenCount = 0; tokenCount < 512; ++tokenCount) {
        Token token;
        if (Lexer::getRawToken(current, token, sm, langOptions,
                               /*IgnoreWhiteSpace=*/true)) {
            return {};
        }

        if (token.is(tok::l_paren)) {
            ++parenDepth;
        } else if (token.is(tok::r_paren)) {
            if (parenDepth > 0) {
                --parenDepth;
            }
        } else if (token.is(tok::l_square)) {
            ++squareDepth;
        } else if (token.is(tok::r_square)) {
            if (squareDepth > 0) {
                --squareDepth;
            }
        } else if (parenDepth == 0 && squareDepth == 0 &&
                   (token.is(tok::l_brace) || token.is(tok::semi) || token.is(tok::equal))) {
            return token.getLocation();
        }

        const SourceLocation next = Lexer::getLocForEndOfToken(token.getLocation(), 0, sm, langOptions);
        if (next.isInvalid() || next == current) {
            return {};
        }
        current = next;
    }

    return {};
}

}  // namespace

// Метод run вызывается для каждого совпадения с матчем.
// Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
void RefactorHandler::run(const MatchFinder::MatchResult &result) {
    if (result.Context == nullptr || result.SourceManager == nullptr) {
        return;
    }

    auto &diag = result.Context->getDiagnostics();
    auto &sm = *result.SourceManager;  // Получаем SourceManager для проверки isInMainFile

    if (const auto *dtor = result.Nodes.getNodeAs<CXXDestructorDecl>("nonVirtualDtor")) {
        handle_nv_dtor(dtor, diag, sm);
    }

    if (const auto *method = result.Nodes.getNodeAs<CXXMethodDecl>("missingOverride")) {
        handle_miss_override(method, diag, sm);
    }

    if (const auto *loopVar = result.Nodes.getNodeAs<VarDecl>("loopVar")) {
        handle_crange_for(loopVar, diag, sm);
    }
}

void RefactorHandler::handle_nv_dtor(const CXXDestructorDecl *dtor, DiagnosticsEngine &diag, SourceManager &sm) {
    // Реализуйте Ваш код ниже
    if (dtor == nullptr || dtor->isVirtual() || dtor->isImplicit()) {
        return;
    }

    const SourceLocation location = dtor->getLocation();
    if (!isEditableMainFileLocation(location, sm)) {
        return;
    }

    if (!virtualDtorLocations_.insert(locationKey(location, sm)).second) {
        return;
    }

    if (rewrite_.InsertTextBefore(location, "virtual ")) {
        return;
    }

    const unsigned diagID =
        diag.getCustomDiagID(DiagnosticsEngine::Remark, "added 'virtual' to destructor of base class '%0'");
    diag.Report(location, diagID) << dtor->getParent()->getName();
    logChange("add virtual destructor", dtor->getParent()->getName(), location, sm);
}

void RefactorHandler::handle_miss_override(const CXXMethodDecl *method, DiagnosticsEngine &diag, SourceManager &sm) {
    if (method == nullptr || method->isImplicit() || isa<CXXDestructorDecl>(method) ||
        method->size_overridden_methods() == 0 || method->hasAttr<OverrideAttr>()) {
        return;
    }

    const SourceLocation methodLocation = method->getLocation();
    if (!isEditableMainFileLocation(methodLocation, sm)) {
        return;
    }

    if (!overrideLocations_.insert(locationKey(methodLocation, sm)).second) {
        return;
    }

    const SourceLocation insertionPoint = findOverrideInsertionPoint(method, sm, rewrite_.getLangOpts());
    if (!isEditableMainFileLocation(insertionPoint, sm)) {
        return;
    }

    const std::string text = insertionBeforeToken(insertionPoint, sm, "override");
    if (rewrite_.InsertTextBefore(insertionPoint, text)) {
        return;
    }

    const unsigned diagnosticId = diag.getCustomDiagID(DiagnosticsEngine::Remark, "added 'override' to method '%0'");
    diag.Report(methodLocation, diagnosticId) << method->getNameAsString();
    logChange("add override", method->getQualifiedNameAsString(), methodLocation, sm);
}

// todo: необходимо реализовать обработку случая отсутствие & в range-for
void RefactorHandler::handle_crange_for(const VarDecl *loopVar, DiagnosticsEngine &diag, SourceManager &sm) {
    if (loopVar == nullptr || loopVar->getTypeSourceInfo() == nullptr) {
        return;
    }

    const QualType type = loopVar->getType();
    if (!type.isConstQualified() || type->isReferenceType() || type->isFundamentalType()) {
        return;
    }

    const TypeLoc typeLoc = loopVar->getTypeSourceInfo()->getTypeLoc();
    SourceLocation typeEnd = typeLoc.getEndLoc();
    if (!isEditableMainFileLocation(typeEnd, sm)) {
        return;
    }

    typeEnd = Lexer::getLocForEndOfToken(typeEnd, 0, sm, rewrite_.getLangOpts());
    if (!isEditableMainFileLocation(typeEnd, sm)) {
        return;
    }

    if (!rangeForLocations_.insert(locationKey(typeEnd, sm)).second) {
        return;
    }

    if (rewrite_.InsertTextBefore(typeEnd, "&")) {
        return;
    }

    const unsigned diagID =
        diag.getCustomDiagID(DiagnosticsEngine::Remark, "changed range-for variable '%0' to const reference");
    diag.Report(loopVar->getLocation(), diagID) << loopVar->getName();
    logChange("add const range-for reference", loopVar->getName(), loopVar->getLocation(), sm);
}

// todo: ниже необходимо реализовать матчеры для поиска узлов AST
// note: синтаксис написания матчеров точно такой же как и для использования clang-query
/*
    Пример того, как может выглядеть реализация:
    auto AllClassesMatcher()
    {
        return cxxRecordDecl().bind("classDecl");
    }
*/
auto NvDtorMatcher() {
    // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска невиртуальных деструкторов
    return cxxRecordDecl(
               isDefinition(),
               hasAnyBase(hasType(cxxRecordDecl(
                   isDefinition(),
                   has(cxxDestructorDecl(unless(isVirtual()), unless(isImplicit())).bind("nonVirtualDtor"))))))
        .bind("derivedClass");
}

auto NoOverrideMatcher() {
    // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска методов без override
    return cxxMethodDecl(isOverride(), unless(isImplicit()), unless(hasAttr(attr::Override))).bind("missingOverride");
}

auto NoRefConstVarInRangeLoopMatcher() {
    // todo: замените код ниже, на свою реализацию, необходимо реализовать матчеры для поиска range-for без &
    return cxxForRangeStmt(hasLoopVariable(varDecl(hasType(qualType(isConstQualified()))).bind("loopVar")));
}

// Конструктор принимает Rewriter для изменения кода.
ComplexConsumer::ComplexConsumer(Rewriter &rewrite) : handler_(rewrite) {
    // Создаем MatchFinder и добавляем матчеры.
    finder_.addMatcher(NvDtorMatcher(), &handler_);
    finder_.addMatcher(NoOverrideMatcher(), &handler_);
    finder_.addMatcher(NoRefConstVarInRangeLoopMatcher(), &handler_);
}

// Метод HandleTranslationUnit вызывается для каждого файла.
void ComplexConsumer::HandleTranslationUnit(ASTContext &context) { finder_.matchAST(context); }

std::unique_ptr<ASTConsumer> CodeRefactorAction::CreateASTConsumer(CompilerInstance &compiler, StringRef /*file*/) {
    rewriter_.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    return std::make_unique<ComplexConsumer>(rewriter_);
}

bool CodeRefactorAction::BeginSourceFileAction(CompilerInstance &compiler) {
    // Инициализируем Rewriter для рефакторинга.
    rewriter_.setSourceMgr(compiler.getSourceManager(), compiler.getLangOpts());
    return true;  // Возвращаем true, чтобы продолжить обработку файла.
}

void CodeRefactorAction::EndSourceFileAction() {
    // Применяем изменения в файле.
    if (rewriter_.overwriteChangedFiles()) {
        llvm::errs() << "Error applying changes to files.\n";
    }
}

int main(int argc, const char **argv) {
    // Парсер опций: Обрабатывает флаги командной строки, компиляционные базы данных.
    auto expectedParser = CommonOptionsParser::create(argc, argv, toolCategory);
    if (!expectedParser) {
        llvm::errs() << expectedParser.takeError();
        return 1;
    }

    CommonOptionsParser &optionsParser = expectedParser.get();
    if (!logFile.empty()) {
        std::ofstream(logFile.getValue(), std::ios::trunc);
    }
    // Создаем ClangTool
    ClangTool tool(optionsParser.getCompilations(), optionsParser.getSourcePathList());
    // Запускаем RefactorAction.
    return tool.run(newFrontendActionFactory<CodeRefactorAction>().get());
}
