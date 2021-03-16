#include <lfortran/asr.h>
#include <lfortran/containers.h>
#include <lfortran/exception.h>
#include <lfortran/asr_utils.h>
#include <lfortran/pass/do_loops.h>


namespace LFortran {

using ASR::down_cast;
using ASR::is_a;

/*

This ASR pass replaces select-case construct with if-then-else-if statements. The function
`pass_replace_select_case` transforms the ASR tree in-place.

Converts:

    select case (a)
    
       case (b:c) 
          ...
 
       case (d:e)
          ...
      case (f)
          ...
       
       case default
          ...
          
    end select

to:

    if ( b <= a && a <= c ) then 
        ...
    else if ( d <= a && a <= e ) then       
        ...  
    else if (f) then
        ...
    else       
        ...
    end if

*/

void case_to_if(Allocator& al, const ASR::Select_t& x, ASR::expr_t* a_test, Vec<ASR::stmt_t*>& body) {
    int idx = (int) x.n_body - 1;
    ASR::case_stmt_t* case_body = x.m_body[idx];
    ASR::stmt_t* last_if_else = nullptr;
    switch(case_body->type) {
        case ASR::case_stmtType::CaseStmt : {
            ASR::CaseStmt_t* Case_Stmt = (ASR::CaseStmt_t*)(&(case_body->base));
            ASR::expr_t* test_expr = EXPR(ASR::make_Compare_t(al, x.base.base.loc, a_test, ASR::cmpopType::Eq, Case_Stmt->m_test[0], expr_type(a_test)));
            last_if_else = STMT(ASR::make_If_t(al, x.base.base.loc, test_expr, Case_Stmt->m_body, Case_Stmt->n_body, x.m_default, x.n_default));
        } 
        default : {
            break;
        }
    }

    for( idx = (int) x.n_body - 2; idx >= 0; idx-- ) {
        ASR::case_stmt_t* case_body = x.m_body[idx];
        ASR::CaseStmt_t* Case_Stmt = (ASR::CaseStmt_t*)(&(case_body->base));
        ASR::expr_t* test_expr = EXPR(ASR::make_Compare_t(al, x.base.base.loc, a_test, ASR::cmpopType::Eq, Case_Stmt->m_test[0], expr_type(a_test)));
        Vec<ASR::stmt_t*> if_body_vec;
        if_body_vec.reserve(al, 1);
        if_body_vec.push_back(al, last_if_else);
        last_if_else = STMT(ASR::make_If_t(al, x.base.base.loc, test_expr, Case_Stmt->m_body, Case_Stmt->n_body, if_body_vec.p, if_body_vec.size()));
    }
    body.reserve(al, 1);
    body.push_back(al, last_if_else);
}

Vec<ASR::stmt_t*> replace_selectcase(Allocator &al, const ASR::Select_t &select_case) {
    ASR::expr_t *a = select_case.m_test;
    Vec<ASR::stmt_t*> body;
    case_to_if(al, select_case, a, body);
    /*
    std::cout << "Input:" << std::endl;
    std::cout << pickle((ASR::asr_t&)loop);
    std::cout << "Output:" << std::endl;
    std::cout << pickle((ASR::asr_t&)*stmt1);
    std::cout << pickle((ASR::asr_t&)*stmt2);
    std::cout << "--------------" << std::endl;
    */
    return body;
}

class SelectCaseVisitor : public ASR::BaseWalkVisitor<SelectCaseVisitor>
{
private:
    Allocator &al;
    Vec<ASR::stmt_t*> select_case_result;
public:
    SelectCaseVisitor(Allocator &al) : al{al} {
        select_case_result.n = 0;

    }

    void visit_TranslationUnit(const ASR::TranslationUnit_t &x) {
        for (auto &a : x.m_global_scope->scope) {
            this->visit_symbol(*a.second);
        }
    }

    void transform_stmts(ASR::stmt_t **&m_body, size_t &n_body) {
        Vec<ASR::stmt_t*> body;
        body.reserve(al, n_body);
        for (size_t i=0; i<n_body; i++) {
            // Not necessary after we check it after each visit_stmt in every
            // visitor method:
            select_case_result.n = 0;
            visit_stmt(*m_body[i]);
            if (select_case_result.size() > 0) {
                for (size_t j=0; j<select_case_result.size(); j++) {
                    body.push_back(al, select_case_result[j]);
                }
                select_case_result.n = 0;
            } else {
                body.push_back(al, m_body[i]);
            }
        }
        m_body = body.p;
        n_body = body.size();
    }

    // TODO: Only Program and While is processed, we need to process all calls
    // to visit_stmt().

    void visit_Program(const ASR::Program_t &x) {
        // FIXME: this is a hack, we need to pass in a non-const `x`,
        // which requires to generate a TransformVisitor.
        ASR::Program_t &xx = const_cast<ASR::Program_t&>(x);
        transform_stmts(xx.m_body, xx.n_body);

        // Transform nested functions and subroutines
        for (auto &item : x.m_symtab->scope) {
            if (is_a<ASR::Subroutine_t>(*item.second)) {
                ASR::Subroutine_t *s = down_cast<ASR::Subroutine_t>(item.second);
                visit_Subroutine(*s);
            }
            if (is_a<ASR::Function_t>(*item.second)) {
                ASR::Function_t *s = down_cast<ASR::Function_t>(item.second);
                visit_Function(*s);
            }
        }
    }

    void visit_Subroutine(const ASR::Subroutine_t &x) {
        // FIXME: this is a hack, we need to pass in a non-const `x`,
        // which requires to generate a TransformVisitor.
        ASR::Subroutine_t &xx = const_cast<ASR::Subroutine_t&>(x);
        transform_stmts(xx.m_body, xx.n_body);
    }

    void visit_Function(const ASR::Function_t &x) {
        // FIXME: this is a hack, we need to pass in a non-const `x`,
        // which requires to generate a TransformVisitor.
        ASR::Function_t &xx = const_cast<ASR::Function_t&>(x);
        transform_stmts(xx.m_body, xx.n_body);
    }

    void visit_WhileLoop(const ASR::WhileLoop_t &x) {
        // FIXME: this is a hack, we need to pass in a non-const `x`,
        // which requires to generate a TransformVisitor.
        ASR::WhileLoop_t &xx = const_cast<ASR::WhileLoop_t&>(x);
        transform_stmts(xx.m_body, xx.n_body);
    }

    void visit_Select(const ASR::Select_t &x) {
        select_case_result = replace_selectcase(al, x);
    }
};

void pass_replace_select_case(Allocator &al, ASR::TranslationUnit_t &unit) {
    SelectCaseVisitor v(al);
    // Each call transforms only one layer of nested loops, so we call it twice
    // to transform doubly nested loops:
    v.visit_TranslationUnit(unit);
    v.visit_TranslationUnit(unit);
}


} // namespace LFortran
