#include <tests/doctest.h>

#include <cmath>

#include <lfortran/codegen/evaluator.h>
#include <lfortran/exception.h>
#include <lfortran/ast.h>
#include <lfortran/asr.h>
#include <lfortran/parser/parser.h>
#include <lfortran/semantics/ast_to_asr.h>
#include <lfortran/codegen/asr_to_llvm.h>
#include <lfortran/pickle.h>

using LFortran::FortranEvaluator;


TEST_CASE("llvm 1") {
    std::cout << "LLVM Version:" << std::endl;
    LFortran::LLVMEvaluator::print_version_message();

    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
define i64 @f1()
{
    ret i64 4
}
    )""");
    CHECK(e.intfn("f1") == 4);
    e.add_module("");
    CHECK(e.intfn("f1") == 4);

    e.add_module(R"""(
define i64 @f1()
{
    ret i64 5
}
    )""");
    CHECK(e.intfn("f1") == 5);
    e.add_module("");
    CHECK(e.intfn("f1") == 5);
}

TEST_CASE("llvm 1 fail") {
    LFortran::LLVMEvaluator e;
    CHECK_THROWS_AS(e.add_module(R"""(
define i64 @f1()
{
    ; FAIL: "=x" is incorrect syntax
    %1 =x alloca i64
}
        )"""), LFortran::CodeGenError);
    CHECK_THROWS_WITH(e.add_module(R"""(
define i64 @f1()
{
    ; FAIL: "=x" is incorrect syntax
    %1 =x alloca i64
}
        )"""), "Invalid LLVM IR");
}


TEST_CASE("llvm 2") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
@count = global i64 0

define i64 @f1()
{
    store i64 4, i64* @count
    %1 = load i64, i64* @count
    ret i64 %1
}
    )""");
    CHECK(e.intfn("f1") == 4);

    e.add_module(R"""(
@count = external global i64

define i64 @f2()
{
    %1 = load i64, i64* @count
    ret i64 %1
}
    )""");
    CHECK(e.intfn("f2") == 4);

    CHECK_THROWS_AS(e.add_module(R"""(
define i64 @f3()
{
    ; FAIL: @count is not defined
    %1 = load i64, i64* @count
    ret i64 %1
}
        )"""), LFortran::CodeGenError);
}

TEST_CASE("llvm 3") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
@count = global i64 5
    )""");

    e.add_module(R"""(
@count = external global i64

define i64 @f1()
{
    %1 = load i64, i64* @count
    ret i64 %1
}

define void @inc()
{
    %1 = load i64, i64* @count
    %2 = add i64 %1, 1
    store i64 %2, i64* @count
    ret void
}
    )""");
    CHECK(e.intfn("f1") == 5);
    e.voidfn("inc");
    CHECK(e.intfn("f1") == 6);
    e.voidfn("inc");
    CHECK(e.intfn("f1") == 7);

    e.add_module(R"""(
@count = external global i64

define void @inc2()
{
    %1 = load i64, i64* @count
    %2 = add i64 %1, 2
    store i64 %2, i64* @count
    ret void
}
    )""");
    CHECK(e.intfn("f1") == 7);
    e.voidfn("inc2");
    CHECK(e.intfn("f1") == 9);
    e.voidfn("inc");
    CHECK(e.intfn("f1") == 10);
    e.voidfn("inc2");
    CHECK(e.intfn("f1") == 12);

    // Test that we can have another independent LLVMEvaluator and use both at
    // the same time:
    LFortran::LLVMEvaluator e2;
    e2.add_module(R"""(
@count = global i64 5

define i64 @f1()
{
    %1 = load i64, i64* @count
    ret i64 %1
}

define void @inc()
{
    %1 = load i64, i64* @count
    %2 = add i64 %1, 1
    store i64 %2, i64* @count
    ret void
}
    )""");

    CHECK(e2.intfn("f1") == 5);
    e2.voidfn("inc");
    CHECK(e2.intfn("f1") == 6);
    e2.voidfn("inc");
    CHECK(e2.intfn("f1") == 7);

    CHECK(e.intfn("f1") == 12);
    e2.voidfn("inc");
    CHECK(e2.intfn("f1") == 8);
    CHECK(e.intfn("f1") == 12);
    e.voidfn("inc");
    CHECK(e2.intfn("f1") == 8);
    CHECK(e.intfn("f1") == 13);

}

TEST_CASE("llvm 4") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
@count = global i64 5

define i64 @f1()
{
    %1 = load i64, i64* @count
    ret i64 %1
}

define void @inc()
{
    %1 = load i64, i64* @count
    %2 = add i64 %1, 1
    store i64 %2, i64* @count
    ret void
}
)""");
    CHECK(e.intfn("f1") == 5);
    e.voidfn("inc");
    CHECK(e.intfn("f1") == 6);
    e.voidfn("inc");
    CHECK(e.intfn("f1") == 7);

    e.add_module(R"""(
declare void @inc()

define void @inc2()
{
    call void @inc()
    call void @inc()
    ret void
}
)""");
    CHECK(e.intfn("f1") == 7);
    e.voidfn("inc2");
    CHECK(e.intfn("f1") == 9);
    e.voidfn("inc");
    CHECK(e.intfn("f1") == 10);
    e.voidfn("inc2");
    CHECK(e.intfn("f1") == 12);

    CHECK_THROWS_AS(e.add_module(R"""(
define void @inc2()
{
    ; FAIL: @inc is not defined
    call void @inc()
    call void @inc()
    ret void
}
        )"""), LFortran::CodeGenError);
}

TEST_CASE("llvm array 1") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
; Sum the three elements in %a
define i64 @sum3(i64* %a)
{
    %a1addr = getelementptr i64, i64* %a, i64 0
    %a1 = load i64, i64* %a1addr

    %a2addr = getelementptr i64, i64* %a, i64 1
    %a2 = load i64, i64* %a2addr

    %a3addr = getelementptr i64, i64* %a, i64 2
    %a3 = load i64, i64* %a3addr

    %tmp = add i64 %a2, %a1
    %r = add i64 %a3, %tmp

    ret i64 %r
}


define i64 @f()
{
    %a = alloca [3 x i64]

    %a1 = getelementptr [3 x i64], [3 x i64]* %a, i64 0, i64 0
    store i64 1, i64* %a1

    %a2 = getelementptr [3 x i64], [3 x i64]* %a, i64 0, i64 1
    store i64 2, i64* %a2

    %a3 = getelementptr [3 x i64], [3 x i64]* %a, i64 0, i64 2
    store i64 3, i64* %a3

    %r = call i64 @sum3(i64* %a1)
    ret i64 %r
}
    )""");
    CHECK(e.intfn("f") == 6);
}

TEST_CASE("llvm array 2") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
%array = type {i64, [3 x i64]}

; Sum the three elements in %a
define i64 @sum3(%array* %a)
{
    %a1addr = getelementptr %array, %array* %a, i64 0, i32 1, i64 0
    %a1 = load i64, i64* %a1addr

    %a2addr = getelementptr %array, %array* %a, i64 0, i32 1, i64 1
    %a2 = load i64, i64* %a2addr

    %a3addr = getelementptr %array, %array* %a, i64 0, i32 1, i64 2
    %a3 = load i64, i64* %a3addr

    %tmp = add i64 %a2, %a1
    %r = add i64 %a3, %tmp

    ret i64 %r
}


define i64 @f()
{
    %a = alloca %array

    %idx0 = getelementptr %array, %array* %a, i64 0, i32 0
    store i64 3, i64* %idx0

    %idx1 = getelementptr %array, %array* %a, i64 0, i32 1, i64 0
    store i64 1, i64* %idx1
    %idx2 = getelementptr %array, %array* %a, i64 0, i32 1, i64 1
    store i64 2, i64* %idx2
    %idx3 = getelementptr %array, %array* %a, i64 0, i32 1, i64 2
    store i64 3, i64* %idx3

    %r = call i64 @sum3(%array* %a)
    ret i64 %r
}
    )""");
    CHECK(e.intfn("f") == 6);
}

int f(int a, int b) {
    return a+b;
}

TEST_CASE("llvm callback 0") {
    LFortran::LLVMEvaluator e;
    std::string addr = std::to_string((int64_t)f);
    e.add_module(R"""(
define i64 @addrcaller(i64 %a, i64 %b)
{
    %f = inttoptr i64 )""" + addr + R"""( to i64 (i64, i64)*
    %r = call i64 %f(i64 %a, i64 %b)
    ret i64 %r
}

define i64 @f1()
{
    %r = call i64 @addrcaller(i64 2, i64 3)
    ret i64 %r
}
    )""");
    CHECK(e.intfn("f1") == 5);
}


TEST_CASE("ASR -> LLVM 1") {
    std::string source = R"(function f()
integer :: f
f = 5
end function)";

    // Src -> AST
    Allocator al(4*1024);
    LFortran::AST::TranslationUnit_t* tu = LFortran::parse2(al, source);
    LFortran::AST::ast_t* ast = tu->m_items[0];
    CHECK(LFortran::pickle(*ast) == "(Function f [] () () () [] [(Declaration [(f \"integer\" [] [] [] ())])] [(= f 5)] [])");

    // AST -> ASR
    LFortran::ASR::TranslationUnit_t* asr = LFortran::ast_to_asr(al, *tu);
    CHECK(LFortran::pickle(*asr) == "(TranslationUnit (SymbolTable 1 {f: (Function (SymbolTable 2 {f: (Variable 2 f ReturnVar (Integer 4 []))}) f [] [(= (Var 2 f) (ConstantInteger 5 (Integer 4 [])))] () (Var 2 f) ())}) [])");

    // ASR -> LLVM
    LFortran::LLVMEvaluator e;
    std::unique_ptr<LFortran::LLVMModule> m = LFortran::asr_to_llvm(*asr,
            e.get_context(), al);
    std::cout << "Module:" << std::endl;
    std::cout << m->str() << std::endl;

    // LLVM -> Machine code -> Execution
    e.add_module(std::move(m));
    CHECK(e.intfn("f") == 5);
}

TEST_CASE("ASR -> LLVM 2") {
    std::string source = R"(function f()
integer :: f
f = 4
end function)";

    // Src -> AST
    Allocator al(4*1024);
    LFortran::AST::TranslationUnit_t* tu = LFortran::parse2(al, source);
    LFortran::AST::ast_t* ast = tu->m_items[0];
    CHECK(LFortran::pickle(*ast) == "(Function f [] () () () [] [(Declaration [(f \"integer\" [] [] [] ())])] [(= f 4)] [])");

    // AST -> ASR
    LFortran::ASR::TranslationUnit_t* asr = LFortran::ast_to_asr(al, *tu);
    CHECK(LFortran::pickle(*asr) == "(TranslationUnit (SymbolTable 3 {f: (Function (SymbolTable 4 {f: (Variable 4 f ReturnVar (Integer 4 []))}) f [] [(= (Var 4 f) (ConstantInteger 4 (Integer 4 [])))] () (Var 4 f) ())}) [])");

    // ASR -> LLVM
    LFortran::LLVMEvaluator e;
    std::unique_ptr<LFortran::LLVMModule> m = LFortran::asr_to_llvm(*asr,
            e.get_context(), al);
    std::cout << "Module:" << std::endl;
    std::cout << m->str() << std::endl;

    // LLVM -> Machine code -> Execution
    e.add_module(std::move(m));
    CHECK(e.intfn("f") == 4);
}

TEST_CASE("FortranEvaluator 1") {
    FortranEvaluator e;
    FortranEvaluator::Result<FortranEvaluator::EvalResult>
    r = e.evaluate("integer :: i");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::none);
    r = e.evaluate("i = 5");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::statement);
    r = e.evaluate("i");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::integer);
    CHECK(r.result.i == 5);
}

TEST_CASE("FortranEvaluator 2") {
    FortranEvaluator e;
    FortranEvaluator::Result<FortranEvaluator::EvalResult>
    r = e.evaluate(R"(real :: r
r = 3
r
)");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::real);
    CHECK(r.result.f == 3);
}

TEST_CASE("FortranEvaluator 3") {
    FortranEvaluator e;
    e.evaluate("integer :: i, j");
    e.evaluate(R"(j = 0
do i = 1, 5
    j = j + i
end do
)");
    FortranEvaluator::Result<FortranEvaluator::EvalResult>
    r = e.evaluate("j");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::integer);
    CHECK(r.result.i == 15);
}

TEST_CASE("FortranEvaluator 4") {
    FortranEvaluator e;
    e.evaluate(R"(
integer function fn(i, j)
integer, intent(in) :: i, j
fn = i + j
end function
)");
    FortranEvaluator::Result<FortranEvaluator::EvalResult>
    r = e.evaluate("fn(2, 3)");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::integer);
    CHECK(r.result.i == 5);

    e.evaluate(R"(
integer function fn(i, j)
integer, intent(in) :: i, j
fn = i - j
end function
)");
    r = e.evaluate("fn(2, 3)");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::integer);
    CHECK(r.result.i == -1);
}

TEST_CASE("FortranEvaluator 5") {
    FortranEvaluator e;
    e.evaluate(R"(
integer subroutine fn(i, j, r)
integer, intent(in) :: i, j
integer, intent(out) :: r
r = i + j
end subroutine
)");
    e.evaluate("integer :: r");
    e.evaluate("call fn(2, 3, r)");
    FortranEvaluator::Result<FortranEvaluator::EvalResult>
    r = e.evaluate("r");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::integer);
    CHECK(r.result.i == 5);

    e.evaluate(R"(
integer subroutine fn(i, j, r)
integer, intent(in) :: i, j
integer, intent(out) :: r
r = i - j
end subroutine
)");
    e.evaluate("call fn(2, 3, r)");
    r = e.evaluate("r");
    CHECK(r.ok);
    CHECK(r.result.type == FortranEvaluator::EvalResult::integer);
    CHECK(r.result.i == -1);
}

TEST_CASE("FortranEvaluator 6") {
    FortranEvaluator e;
    FortranEvaluator::Result<FortranEvaluator::EvalResult>
    r = e.evaluate("$");
    CHECK(!r.ok);
    CHECK(r.error.type == FortranEvaluator::Error::Tokenizer);

    r = e.evaluate("1x");
    CHECK(!r.ok);
    CHECK(r.error.type == FortranEvaluator::Error::Parser);

    r = e.evaluate("x = 'x'");
    CHECK(!r.ok);
    CHECK(r.error.type == FortranEvaluator::Error::Semantic);
}

// Tests passing the complex struct by reference
TEST_CASE("llvm complex type") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
%complex = type { float, float }

define float @sum2(%complex* %a)
{
    %a1addr = getelementptr %complex, %complex* %a, i32 0, i32 0
    %a1 = load float, float* %a1addr

    %a2addr = getelementptr %complex, %complex* %a, i32 0, i32 1
    %a2 = load float, float* %a2addr

    %r = fadd float %a2, %a1

    ret float %r
}

define float @f()
{
    %a = alloca %complex

    %a1 = getelementptr %complex, %complex* %a, i32 0, i32 0
    store float 3.5, float* %a1

    %a2 = getelementptr %complex, %complex* %a, i32 0, i32 1
    store float 4.5, float* %a2

    %r = call float @sum2(%complex* %a)
    ret float %r
}
    )""");
    CHECK(std::abs(e.floatfn("f") - 8) < 1e-6);
}

// Tests passing the complex struct by value
TEST_CASE("llvm complex type value") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(
%complex = type { float, float }

define float @sum2(%complex %a_value)
{
    %a = alloca %complex
    store %complex %a_value, %complex* %a

    %a1addr = getelementptr %complex, %complex* %a, i32 0, i32 0
    %a1 = load float, float* %a1addr

    %a2addr = getelementptr %complex, %complex* %a, i32 0, i32 1
    %a2 = load float, float* %a2addr

    %r = fadd float %a2, %a1
    ret float %r
}

define float @f()
{
    %a = alloca %complex

    %a1 = getelementptr %complex, %complex* %a, i32 0, i32 0
    store float 3.5, float* %a1

    %a2 = getelementptr %complex, %complex* %a, i32 0, i32 1
    store float 4.5, float* %a2

    %ap = load %complex, %complex* %a
    %r = call float @sum2(%complex %ap)
    ret float %r
}
    )""");
    CHECK(std::abs(e.floatfn("f") - 8) < 1e-6);
}

// Tests passing boolean by reference
TEST_CASE("llvm boolean type") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(

define i1 @and_func(i1* %p, i1* %q)
{
    %pval = load i1, i1* %p
    %qval = load i1, i1* %q

    %r = and i1 %pval, %qval
    ret i1 %r
}

define i1 @b()
{
    %p = alloca i1
    %q = alloca i1

    store i1 1, i1* %p
    store i1 0, i1* %q

    %r = call i1 @and_func(i1* %p, i1* %q)

    ret i1 %r
}
    )""");
    CHECK(e.boolfn("b") == false);
}

// Tests passing boolean by value
TEST_CASE("llvm boolean type") {
    LFortran::LLVMEvaluator e;
    e.add_module(R"""(

define i1 @and_func(i1 %p, i1 %q)
{
    %r = and i1 %p, %q
    ret i1 %r
}

define i1 @b()
{
    %p = alloca i1
    %q = alloca i1

    store i1 1, i1* %p
    store i1 0, i1* %q

    %pval = load i1, i1* %p
    %qval = load i1, i1* %q

    %r = call i1 @and_func(i1 %pval, i1 %qval)

    ret i1 %r
}
    )""");
    CHECK(e.boolfn("b") == false);
}
