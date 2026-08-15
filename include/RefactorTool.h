#pragma once

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <memory>
#include <unordered_set>

class RefactorHandler : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
    explicit RefactorHandler(clang::Rewriter &rewrite) : rewrite_(rewrite) {}
    // Метод run вызывается для каждого совпадения с матчем.
    // Мы проверяем тип совпадения по bind-именам и применяем рефакторинг.
    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;

private:
    // 1. Невиртуальные деструкторы
    void handle_nv_dtor(const clang::CXXDestructorDecl *dtor, clang::DiagnosticsEngine &diag, clang::SourceManager &sm);
    // 2. Методы без override
    void handle_miss_override(const clang::CXXMethodDecl *method, clang::DiagnosticsEngine &diag,
                              clang::SourceManager &sm);
    // 3. range-for без &
    void handle_crange_for(const clang::VarDecl *loopVar, clang::DiagnosticsEngine &diag, clang::SourceManager &sm);

    clang::Rewriter &rewrite_;
    std::unordered_set<unsigned> virtualDtorLocations_;
    std::unordered_set<unsigned> overrideLocations_;
    std::unordered_set<unsigned> rangeForLocations_;
};

class ComplexConsumer : public clang::ASTConsumer {
public:
    // Конструктор принимает Rewriter для изменения кода.
    explicit ComplexConsumer(clang::Rewriter &rewrite);
    // Метод HandleTranslationUnit вызывается для каждого файла.
    void HandleTranslationUnit(clang::ASTContext &context) override;

private:
    RefactorHandler handler_;                  // Обработчик матчеров.
    clang::ast_matchers::MatchFinder finder_;  // MatchFinder для поиска узлов AST.
};

class CodeRefactorAction : public clang::ASTFrontendAction {
public:
    // Returns our ASTConsumer per translation unit.
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler,
                                                          clang::StringRef file) override;
    bool BeginSourceFileAction(clang::CompilerInstance &compiler) override;
    void EndSourceFileAction() override;

private:
    clang::Rewriter rewriter_;
};
