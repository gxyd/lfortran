#include <iostream>
#include <memory>

#include <lfortran/asr.h>
#include <lfortran/containers.h>
#include <lfortran/codegen/asr_to_cpp.h>
#include <lfortran/exception.h>
#include <lfortran/asr_utils.h>
#include <lfortran/string_utils.h>


namespace LFortran {

using ASR::is_a;
using ASR::down_cast;
using ASR::down_cast2;

std::string convert_dims(size_t n_dims, ASR::dimension_t *m_dims);

std::string format_type(const std::string &dims, const std::string &type,
        const std::string &name, bool use_ref, bool dummy);

class ASRToPyVisitor : public ASR::BaseVisitor<ASRToPyVisitor>
{
public:
    std::string src;

    std::string convert_variable_decl(const ASR::Variable_t &v)
    {
        std::string sub;
        bool use_ref = (v.m_intent == LFortran::ASRUtils::intent_out || v.m_intent == LFortran::ASRUtils::intent_inout);
        bool dummy = LFortran::ASRUtils::is_arg_dummy(v.m_intent);
        if (is_a<ASR::Integer_t>(*v.m_type)) {
            ASR::Integer_t *t = down_cast<ASR::Integer_t>(v.m_type);
            std::string dims = convert_dims(t->n_dims, t->m_dims);
            sub = format_type(dims, "int", v.m_name, use_ref, dummy);
        } else if (is_a<ASR::Real_t>(*v.m_type)) {
            ASR::Real_t *t = down_cast<ASR::Real_t>(v.m_type);
            std::string dims = convert_dims(t->n_dims, t->m_dims);
            sub = format_type(dims, "float", v.m_name, use_ref, dummy);
        } else if (is_a<ASR::Logical_t>(*v.m_type)) {
            ASR::Logical_t *t = down_cast<ASR::Logical_t>(v.m_type);
            std::string dims = convert_dims(t->n_dims, t->m_dims);
            sub = format_type(dims, "bool", v.m_name, use_ref, dummy);
        } else if (is_a<ASR::Character_t>(*v.m_type)) {
            ASR::Character_t *t = down_cast<ASR::Character_t>(v.m_type);
            std::string dims = convert_dims(t->n_dims, t->m_dims);
            sub = format_type(dims, "std::string", v.m_name, use_ref, dummy);
            if (v.m_symbolic_value) {
                this->visit_expr(*v.m_symbolic_value);
                std::string init = src;
                sub += "=" + init;
            }
        } else {
            throw CodeGenError("Type not supported");
        }
        return sub;
    }

    void visit_TranslationUnit(const ASR::TranslationUnit_t &x) {
        // All loose statements must be converted to a function, so the items
        // must be empty:
        LFORTRAN_ASSERT(x.n_items == 0);
        std::string unit_src = "";

        std::string headers =
R"(#include <stdio>

)";
        unit_src += headers;

        // Process procedures first:
        for (auto &item : x.m_global_scope->scope) {
            if (is_a<ASR::Function_t>(*item.second)
                || is_a<ASR::Subroutine_t>(*item.second)) {
                visit_symbol(*item.second);
                unit_src += src;
            }
        }

        // Then do all the modules in the right order
        std::vector<std::string> build_order
            = LFortran::ASRUtils::determine_module_dependencies(x);
        for (auto &item : build_order) {
            LFORTRAN_ASSERT(x.m_global_scope->scope.find(item)
                != x.m_global_scope->scope.end());
            if (!startswith(item, "lfortran_intrinsic")) {
                ASR::symbol_t *mod = x.m_global_scope->scope[item];
                visit_symbol(*mod);
                unit_src += src;
            }
        }

        // Then the main program:
        for (auto &item : x.m_global_scope->scope) {
            if (is_a<ASR::Program_t>(*item.second)) {
                visit_symbol(*item.second);
                unit_src += src;
            }
        }

        src = unit_src;
    }

    void visit_Module(const ASR::Module_t &x) {
        // Generate code for nested subroutines and functions first:
        std::string contains;
        for (auto &item : x.m_symtab->scope) {
            if (is_a<ASR::Subroutine_t>(*item.second)) {
                ASR::Subroutine_t *s = ASR::down_cast<ASR::Subroutine_t>(item.second);
                visit_Subroutine(*s);
                contains += src;
            }
            if (is_a<ASR::Function_t>(*item.second)) {
                ASR::Function_t *s = ASR::down_cast<ASR::Function_t>(item.second);
                visit_Function(*s);
                contains += src;
            }
        }
        src = contains;
    }

    void visit_Subroutine(const ASR::Subroutine_t &x) {
        std::string sub = "void " + std::string(x.m_name) + "(";
        for (size_t i=0; i<x.n_args; i++) {
            ASR::Variable_t *arg = LFortran::ASRUtils::EXPR2VAR(x.m_args[i]);
            LFORTRAN_ASSERT(LFortran::ASRUtils::is_arg_dummy(arg->m_intent));
            sub += convert_variable_decl(*arg);
            if (i < x.n_args-1) sub += ", ";
        }
        sub += ")\n";

        src = sub;
    }



};

std::string asr_to_py(ASR::TranslationUnit_t &asr)
{
    ASRToPyVisitor v;
    v.visit_asr((ASR::asr_t &)asr);
    return v.src;
}

} // namespace LFortran
