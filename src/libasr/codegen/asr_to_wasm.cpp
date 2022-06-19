#include <iostream>
#include <memory>
#include <chrono>
#include <iomanip>
#include <fstream>

#include <libasr/asr.h>
#include <libasr/containers.h>
#include <libasr/codegen/asr_to_wasm.h>
#include <libasr/codegen/wasm_assembler.h>
#include <libasr/pass/do_loops.h>
#include <libasr/pass/global_stmts.h>
#include <libasr/exception.h>
#include <libasr/asr_utils.h>

namespace LFortran {

namespace {

    // Local exception that is only used in this file to exit the visitor
    // pattern and caught later (not propagated outside)
    class CodeGenError {
    public:
        diag::Diagnostic d;

    public:
        CodeGenError(const std::string &msg) : d{diag::Diagnostic(msg, diag::Level::Error, diag::Stage::CodeGen)} {}
    };

}  // namespace


struct import_func{
    std::string name;
    std::vector<uint8_t> param_types, result_types;
};


class ASRToWASMVisitor : public ASR::BaseVisitor<ASRToWASMVisitor> {
   public:
    Allocator &m_al;
    diag::Diagnostics &diag;

    ASR::Variable_t *return_var;
    bool is_return_visited;

    Vec<uint8_t> m_type_section;
    Vec<uint8_t> m_import_section;
    Vec<uint8_t> m_func_section;
    Vec<uint8_t> m_export_section;
    Vec<uint8_t> m_code_section;
    Vec<uint8_t> m_data_section;

    uint32_t cur_func_idx;
    uint32_t no_of_functions;
    uint32_t no_of_imports;
    uint32_t no_of_data_segements;  
    uint32_t last_str_len;
    uint32_t avail_mem_loc;
    
    std::map<std::string, int32_t> m_var_name_idx_map;
    std::map<std::string, int32_t> m_func_name_idx_map;

   public:
    ASRToWASMVisitor(Allocator &al, diag::Diagnostics &diagnostics): m_al(al), diag(diagnostics) {
        cur_func_idx = 0;
        avail_mem_loc = 0;
        no_of_functions = 0;
        no_of_imports = 0;
        no_of_data_segements = 0;
        m_type_section.reserve(m_al, 1024 * 128);
        m_import_section.reserve(m_al, 1024 * 128);
        m_func_section.reserve(m_al, 1024 * 128);
        m_export_section.reserve(m_al, 1024 * 128);
        m_code_section.reserve(m_al, 1024 * 128);
        m_data_section.reserve(m_al, 1024 * 128);
    }

    void visit_TranslationUnit(const ASR::TranslationUnit_t &x) {

        // the main program:
        for (auto &item : x.m_global_scope->get_scope()) {
            if (ASR::is_a<ASR::Program_t>(*item.second)) {
                visit_symbol(*item.second);
            }
        }

    }

    void emit_imports(){
        std::vector<import_func> import_funcs = {
            {"print_i32", {0x7F}, {}},
            {"print_i64", {0x7E}, {}},
            {"print_f32", {0x7D}, {}},
            {"print_f64", {0x7C}, {}},
            {"print_str", {0x7F, 0x7F}, {}},
            {"flush_buf", {}, {}}
        };

        for(auto import_func:import_funcs){
            wasm::emit_import_fn(m_import_section, m_al, "js", import_func.name, cur_func_idx);
            // add their types to type section
            wasm::emit_b8(m_type_section, m_al, 0x60);  // type section

            wasm::emit_u32(m_type_section, m_al, import_func.param_types.size());
            for(auto &param_type:import_func.param_types){
                wasm::emit_b8(m_type_section, m_al, param_type);
            }

            wasm::emit_u32(m_type_section, m_al, import_func.result_types.size());
            for(auto &result_type:import_func.result_types){
                wasm::emit_b8(m_type_section, m_al, result_type);
            }

            m_func_name_idx_map[import_func.name] = cur_func_idx++;
            no_of_imports++;
        }

        wasm::emit_import_mem(m_import_section, m_al, "js", "memory", 10U /* min page limit */, 10U /* max page limit */);
        no_of_imports++;
    }
    
    
    void visit_Program(const ASR::Program_t &x) {
        
        uint32_t len_idx_type_section = wasm::emit_len_placeholder(m_type_section, m_al);
        uint32_t len_idx_import_section = wasm::emit_len_placeholder(m_import_section, m_al);
        uint32_t len_idx_func_section = wasm::emit_len_placeholder(m_func_section, m_al);
        uint32_t len_idx_export_section = wasm::emit_len_placeholder(m_export_section, m_al);
        uint32_t len_idx_code_section = wasm::emit_len_placeholder(m_code_section, m_al);
        uint32_t len_idx_data_section = wasm::emit_len_placeholder(m_data_section, m_al);

        emit_imports();

        no_of_functions = 0;

        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Subroutine_t>(*item.second)) {
                throw CodeGenError("Sub Routine not yet supported");
            }
            if (ASR::is_a<ASR::Function_t>(*item.second)) {
                ASR::Function_t *s = ASR::down_cast<ASR::Function_t>(item.second);
                visit_Function(*s);
                no_of_functions++;
            }
        }

        // Generate main program code
        sprintf(x.m_name, "_lcompilers_main");
        m_var_name_idx_map.clear(); // clear all previous variable and their indices
        wasm::emit_b8(m_type_section, m_al, 0x60);  // new type starts
        m_func_name_idx_map[x.m_name] = cur_func_idx;

        wasm::emit_u32(m_type_section, m_al, 0U); // emit parameter types length = 0
        wasm::emit_u32(m_type_section, m_al, 0U); // emit result types length = 0
        
        /********************* Function Body Starts Here *********************/
        uint32_t len_idx_code_section_func_size = wasm::emit_len_placeholder(m_code_section, m_al);
        
        /********************* Local Vars Types List *********************/
        uint32_t len_idx_code_section_local_vars_list = wasm::emit_len_placeholder(m_code_section, m_al);
        int local_vars_cnt = 0, cur_idx = 0;
        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Variable_t>(*item.second)) {
                ASR::Variable_t *v = ASR::down_cast<ASR::Variable_t>(item.second);
                wasm::emit_u32(m_code_section, m_al, 1U);    // count of local vars of this type
                emit_var_type(m_code_section, v); // emit the type of this var
                m_var_name_idx_map[v->m_name] = cur_idx++;
                local_vars_cnt++;
            }
        }
        // fixup length of local vars list
        wasm::emit_u32_b32_idx(m_code_section, m_al, len_idx_code_section_local_vars_list, local_vars_cnt);

        for (size_t i = 0; i < x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
        }

        wasm::emit_expr_end(m_code_section, m_al);
        wasm::fixup_len(m_code_section, m_al, len_idx_code_section_func_size);

        wasm::emit_u32(m_func_section, m_al, cur_func_idx);

        wasm::emit_export_fn(m_export_section, m_al, x.m_name, cur_func_idx);

        cur_func_idx++;
        no_of_functions++;

        wasm::emit_u32_b32_idx(m_func_section, m_al, len_idx_func_section, no_of_functions);
        wasm::emit_u32_b32_idx(m_export_section, m_al, len_idx_export_section, no_of_functions);
        wasm::emit_u32_b32_idx(m_code_section, m_al, len_idx_code_section, no_of_functions);
        wasm::emit_u32_b32_idx(m_data_section, m_al, len_idx_data_section, no_of_data_segements);
        wasm::emit_u32_b32_idx(m_type_section, m_al, len_idx_type_section, cur_func_idx); // cur_func_idx indicate the total number of functions present
        wasm::emit_u32_b32_idx(m_import_section, m_al, len_idx_import_section, no_of_imports);

    }

    void emit_var_type(Vec<uint8_t> &code, ASR::Variable_t *v){
        if (ASRUtils::is_integer(*v->m_type)) {
            // checking for array is currently omitted
            ASR::Integer_t* v_int = ASR::down_cast<ASR::Integer_t>(v->m_type);
            if (v_int->m_kind == 4) {
                wasm::emit_b8(code, m_al, 0x7F); // i32
            }
            else if (v_int->m_kind == 8) {
                wasm::emit_b8(code, m_al, 0x7E); // i64
            }
            else{
                throw CodeGenError("Integers of kind 4 and 8 only supported");
            }
        } else if (ASRUtils::is_real(*v->m_type)) {
            // checking for array is currently omitted
            ASR::Real_t* v_float = ASR::down_cast<ASR::Real_t>(v->m_type);
            if (v_float->m_kind == 4) {
                wasm::emit_b8(code, m_al, 0x7D); // f32
            }
            else if(v_float->m_kind == 8){
                wasm::emit_b8(code, m_al, 0x7C); // f64
            } 
            else {
                throw CodeGenError("Floating Points of kind 4 and 8 only supported");
            }
        } else {
            throw CodeGenError("Param, Result, Var Types other than integer and floating point not yet supported");
        }
    }

    void visit_Function(const ASR::Function_t &x) {
        m_var_name_idx_map.clear(); // clear all previous variable and their indices

        wasm::emit_b8(m_type_section, m_al, 0x60);  // type section
        
        m_func_name_idx_map[x.m_name] = cur_func_idx; // add func to map early to support recursive func calls

        /********************* Parameter Types List *********************/
        uint32_t len_idx_type_section_param_types_list = wasm::emit_len_placeholder(m_type_section, m_al);
        int cur_idx = 0;
        for (size_t i = 0; i < x.n_args; i++) {
            ASR::Variable_t *arg = LFortran::ASRUtils::EXPR2VAR(x.m_args[i]);
            LFORTRAN_ASSERT(LFortran::ASRUtils::is_arg_dummy(arg->m_intent));
            emit_var_type(m_type_section, arg);
            m_var_name_idx_map[arg->m_name] = cur_idx++;
        }
        wasm::fixup_len(m_type_section, m_al, len_idx_type_section_param_types_list);

        /********************* Result Types List *********************/
        uint32_t len_idx_type_section_return_types_list = wasm::emit_len_placeholder(m_type_section, m_al);
        return_var = LFortran::ASRUtils::EXPR2VAR(x.m_return_var);
        emit_var_type(m_type_section, return_var);
        wasm::fixup_len(m_type_section, m_al, len_idx_type_section_return_types_list);
        is_return_visited = false; // for every function initialize is_return_visited to false

        uint32_t len_idx_code_section_func_size = wasm::emit_len_placeholder(m_code_section, m_al);
        
        /********************* Local Vars Types List *********************/
        uint32_t len_idx_code_section_local_vars_list = wasm::emit_len_placeholder(m_code_section, m_al);
        int local_vars_cnt = 0;
        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Variable_t>(*item.second)) {
                ASR::Variable_t *v = ASR::down_cast<ASR::Variable_t>(item.second);
                if (v->m_intent == LFortran::ASRUtils::intent_local || v->m_intent == LFortran::ASRUtils::intent_return_var) {
                    wasm::emit_u32(m_code_section, m_al, 1U);    // count of local vars of this type
                    emit_var_type(m_code_section, v); // emit the type of this var
                    m_var_name_idx_map[v->m_name] = cur_idx++;
                    local_vars_cnt++;
                }
            }
        }
        // fixup length of local vars list
        wasm::emit_u32_b32_idx(m_code_section, m_al, len_idx_code_section_local_vars_list, local_vars_cnt);

        for (size_t i = 0; i < x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
        }

        if(!is_return_visited){
            ASR::Return_t temp;
            visit_Return(temp);
        }

        wasm::emit_expr_end(m_code_section, m_al);
        wasm::fixup_len(m_code_section, m_al, len_idx_code_section_func_size);

        wasm::emit_u32(m_func_section, m_al, cur_func_idx);

        wasm::emit_export_fn(m_export_section, m_al, x.m_name, cur_func_idx);

        cur_func_idx++;
    }

    void visit_Assignment(const ASR::Assignment_t &x) {
        this->visit_expr(*x.m_value);
        // this->visit_expr(*x.m_target);
        if (ASR::is_a<ASR::Var_t>(*x.m_target)) {
            ASR::Variable_t *asr_target = LFortran::ASRUtils::EXPR2VAR(x.m_target);
            LFORTRAN_ASSERT(m_var_name_idx_map.find(asr_target->m_name) != m_var_name_idx_map.end());
            wasm::emit_set_local(m_code_section, m_al, m_var_name_idx_map[asr_target->m_name]);
        } else if (ASR::is_a<ASR::ArrayRef_t>(*x.m_target)) {
            throw CodeGenError("Assignment: Arrays not yet supported");
        } else {
            LFORTRAN_ASSERT(false)
        }
    }

    void visit_IntegerBinOp(const ASR::IntegerBinOp_t &x) {
        if (x.m_value) { 
            visit_expr(*x.m_value); 
            return; 
        }
        this->visit_expr(*x.m_left);
        this->visit_expr(*x.m_right);
        ASR::Integer_t *i = ASR::down_cast<ASR::Integer_t>(x.m_type);
        if (i->m_kind == 4) {
            switch (x.m_op) {
                case ASR::binopType::Add: {
                    wasm::emit_i32_add(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Sub: {
                    wasm::emit_i32_sub(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Mul: {
                    wasm::emit_i32_mul(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Div: {
                    wasm::emit_i32_div_s(m_code_section, m_al);
                    break;
                };
                default:
                    throw CodeGenError("IntegerBinop: Pow Operation not yet implemented");
            }
        } else if (i->m_kind == 8) {
            switch (x.m_op) {
                case ASR::binopType::Add: {
                    wasm::emit_i64_add(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Sub: {
                    wasm::emit_i64_sub(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Mul: {
                    wasm::emit_i64_mul(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Div: {
                    wasm::emit_i64_div_s(m_code_section, m_al);
                    break;
                };
                default:
                    throw CodeGenError("IntegerBinop: Pow Operation not yet implemented");
            }
        } else {
            throw CodeGenError("IntegerBinop: Integer kind not supported");
        }
    }

    void visit_RealBinOp(const ASR::RealBinOp_t &x) {
        if (x.m_value) { 
            visit_expr(*x.m_value); 
            return; 
        }
        this->visit_expr(*x.m_left);
        this->visit_expr(*x.m_right);
        ASR::Real_t *f = ASR::down_cast<ASR::Real_t>(x.m_type);
        if (f->m_kind == 4) {
            switch (x.m_op) {
                case ASR::binopType::Add: {
                    wasm::emit_f32_add(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Sub: {
                    wasm::emit_f32_sub(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Mul: {
                    wasm::emit_f32_mul(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Div: {
                    wasm::emit_f32_div(m_code_section, m_al);
                    break;
                };
                default:
                    throw CodeGenError("RealBinop: Pow Operation not yet implemented");
            }
        } else if (f->m_kind == 8) {
            switch (x.m_op) {
                case ASR::binopType::Add: {
                    wasm::emit_f64_add(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Sub: {
                    wasm::emit_f64_sub(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Mul: {
                    wasm::emit_f64_mul(m_code_section, m_al);
                    break;
                };
                case ASR::binopType::Div: {
                    wasm::emit_f64_div(m_code_section, m_al);
                    break;
                };
                default:
                    throw CodeGenError("RealBinop: Pow Operation not yet implemented");
            }
        } else {
            throw CodeGenError("RealBinop: Real kind not supported");
        }
    }

    void visit_IntegerUnaryMinus(const ASR::IntegerUnaryMinus_t &x) {
         if (x.m_value) { 
            visit_expr(*x.m_value); 
            return; 
        }
        ASR::Integer_t *i = ASR::down_cast<ASR::Integer_t>(x.m_type);
        // there seems no direct unary-minus inst in wasm, so subtracting from 0
        if(i->m_kind == 4){
            wasm::emit_i32_const(m_code_section, m_al, 0);
            this->visit_expr(*x.m_arg);
            wasm::emit_i32_sub(m_code_section, m_al);
        }
        else if(i->m_kind == 8){
            wasm::emit_i64_const(m_code_section, m_al, 0LL);
            this->visit_expr(*x.m_arg);
            wasm::emit_i64_sub(m_code_section, m_al);
        }
        else{
            throw CodeGenError("IntegerUnaryMinus: Only kind 4 and 8 supported");
        }
    }

    void visit_RealUnaryMinus(const ASR::RealUnaryMinus_t &x) {
         if (x.m_value) { 
            visit_expr(*x.m_value); 
            return; 
        }
        ASR::Real_t *f = ASR::down_cast<ASR::Real_t>(x.m_type);
        if(f->m_kind == 4){
            this->visit_expr(*x.m_arg);
            wasm::emit_f32_neg(m_code_section, m_al);
        }
        else if(f->m_kind == 8){
            this->visit_expr(*x.m_arg);
            wasm::emit_f64_neg(m_code_section, m_al);
        }
        else{
            throw CodeGenError("RealUnaryMinus: Only kind 4 and 8 supported");
        }
    }

    void visit_Var(const ASR::Var_t &x) {
        const ASR::symbol_t *s = ASRUtils::symbol_get_past_external(x.m_v);
        auto v = ASR::down_cast<ASR::Variable_t>(s);
        switch (v->m_type->type) {
            case ASR::ttypeType::Integer:{
                LFORTRAN_ASSERT(m_var_name_idx_map.find(v->m_name) != m_var_name_idx_map.end());
                wasm::emit_get_local(m_code_section, m_al, m_var_name_idx_map[v->m_name]);
                break;
            }

            case ASR::ttypeType::Real: {
                LFORTRAN_ASSERT(m_var_name_idx_map.find(v->m_name) != m_var_name_idx_map.end());
                wasm::emit_get_local(m_code_section, m_al, m_var_name_idx_map[v->m_name]);
                break;
            }
            
            default:
                throw CodeGenError("Only Integer and Float Variable types currently supported");
        }
    }

    void visit_Return(const ASR::Return_t & /* x */) {
        LFORTRAN_ASSERT(m_var_name_idx_map.find(return_var->m_name) != m_var_name_idx_map.end());
        wasm::emit_get_local(m_code_section, m_al, m_var_name_idx_map[return_var->m_name]);
        wasm::emit_b8(m_code_section, m_al, 0x0F); // return instruction
        is_return_visited = true;
    }

    void visit_IntegerConstant(const ASR::IntegerConstant_t &x) {
        int64_t val = x.m_n;
        int a_kind = ((ASR::Integer_t *)(&(x.m_type->base)))->m_kind;
        switch (a_kind) {
            case 4: {
                wasm::emit_i32_const(m_code_section, m_al, val);
                break;
            }
            case 8: {
                wasm::emit_i64_const(m_code_section, m_al, val);
                break;
            }
            default: {
                throw CodeGenError("Constant Integer: Only kind 4 and 8 supported");
            }
        }
    }

    void visit_RealConstant(const ASR::RealConstant_t &x) {
        double val = x.m_r;
        int a_kind = ((ASR::Real_t *)(&(x.m_type->base)))->m_kind;
        switch (a_kind) {
            case 4: {
                wasm::emit_f32_const(m_code_section, m_al, val);
                break;
            }
            case 8: {
                wasm::emit_f64_const(m_code_section, m_al, val);
                break;
            }
            default: {
                throw CodeGenError("Constant Real: Only kind 4 and 8 supported");
            }
        }
    }

    void visit_StringConstant(const ASR::StringConstant_t &x){
        // Todo: Add a check here if there is memory available to store the given string
        wasm::emit_str_const(m_data_section, m_al, avail_mem_loc, x.m_s);
        last_str_len = strlen(x.m_s);
        no_of_data_segements++;
    }

    void visit_FunctionCall(const ASR::FunctionCall_t &x) {
        ASR::Function_t *fn = ASR::down_cast<ASR::Function_t>(LFortran::ASRUtils::symbol_get_past_external(x.m_name));

        for (size_t i = 0; i < x.n_args; i++) {
            visit_expr(*x.m_args[i].m_value);
        }

        LFORTRAN_ASSERT(m_func_name_idx_map.find(fn->m_name) != m_func_name_idx_map.end())
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[fn->m_name]);
    }


    void handle_print(const ASR::Print_t &x){
        for (size_t i=0; i<x.n_values; i++) {
            this->visit_expr(*x.m_values[i]);
            ASR::expr_t *v = x.m_values[i];
            ASR::ttype_t *t = ASRUtils::expr_type(v);
            int a_kind = ASRUtils::extract_kind_from_ttype_t(t);

            if (ASRUtils::is_integer(*t)) {
                switch( a_kind ) {
                    case 4 : {
                        // the value is already on stack. call JavaScript print_i32
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map["print_i32"]);
                        break;
                    }
                    case 8 : {
                        // the value is already on stack. call JavaScript print_i64
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map["print_i64"]);
                        break;
                    }
                    default: {
                        throw CodeGenError(R"""(Printing support is currently available only
                                            for 32, and 64 bit integer kinds.)""");
                    }
                }
            } else if (ASRUtils::is_real(*t)) {
                switch( a_kind ) {
                    case 4 : {
                        // the value is already on stack. call JavaScript print_f32
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map["print_f32"]);
                        break;
                    }
                    case 8 : {
                        // the value is already on stack. call JavaScript print_f64
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map["print_f64"]);
                        break;
                    }
                    default: {
                        throw CodeGenError(R"""(Printing support is available only
                                            for 32, and 64 bit real kinds.)""");
                    }
                }
            } else if (t->type == ASR::ttypeType::Character) {
                // push string location and its size on function stack
                wasm::emit_i32_const(m_code_section, m_al, avail_mem_loc);
                wasm::emit_i32_const(m_code_section, m_al, last_str_len);
                avail_mem_loc += last_str_len;

                // call JavaScript printStr
                wasm::emit_call(m_code_section, m_al, m_func_name_idx_map["print_str"]);
                
            } 
        }

        // call JavaScript Flush
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map["flush_buf"]);
    }

    void visit_Print(const ASR::Print_t &x){
        if (x.m_fmt != nullptr) {
            diag.codegen_warning_label("format string in `print` is not implemented yet and it is currently treated as '*'",
                {x.m_fmt->base.loc}, "treated as '*'");
        }
        handle_print(x);
    }
};

Result<Vec<uint8_t>> asr_to_wasm_bytes_stream(ASR::TranslationUnit_t &asr, Allocator &al, diag::Diagnostics &diagnostics) {
    ASRToWASMVisitor v(al, diagnostics);
    Vec<uint8_t> wasm_bytes;
    
    pass_wrap_global_stmts_into_function(al, asr, "f");
    pass_replace_do_loops(al, asr);
    
    try {
        v.visit_asr((ASR::asr_t &)asr);
    } catch (const CodeGenError &e) {
        diagnostics.diagnostics.push_back(e.d);
        return Error();
    }

    {
        wasm_bytes.reserve(al, 8U /* preamble size */ + 8U /* (section id + section size) */ * 6U /* number of sections */ 
            + v.m_type_section.size() + v.m_import_section.size() + v.m_func_section.size() 
            + v.m_export_section.size() + v.m_code_section.size() + v.m_data_section.size());
        
        wasm::emit_header(wasm_bytes, al);  // emit header and version
        wasm::encode_section(wasm_bytes, v.m_type_section, al, 1U);
        wasm::encode_section(wasm_bytes, v.m_import_section, al, 2U);
        wasm::encode_section(wasm_bytes, v.m_func_section, al, 3U);
        wasm::encode_section(wasm_bytes, v.m_export_section, al, 7U);
        wasm::encode_section(wasm_bytes, v.m_code_section, al, 10U);
        wasm::encode_section(wasm_bytes, v.m_data_section, al, 11U);
    }

    return wasm_bytes;
}

Result<int> asr_to_wasm(ASR::TranslationUnit_t &asr, Allocator &al, const std::string &filename,
    bool time_report, diag::Diagnostics &diagnostics) {
    int time_visit_asr = 0;
    int time_save = 0;

    auto t1 = std::chrono::high_resolution_clock::now();
    Result<Vec<uint8_t>> wasm = asr_to_wasm_bytes_stream(asr, al, diagnostics);
    auto t2 = std::chrono::high_resolution_clock::now();
    time_visit_asr = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    if (!wasm.ok) {
        return wasm.error;
    }

    {
        auto t1 = std::chrono::high_resolution_clock::now();
        wasm::save_bin(wasm.result, filename);
        auto t2 = std::chrono::high_resolution_clock::now();
        time_save = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    }

    if (time_report) {
        std::cout << "Codegen Time report:" << std::endl;
        std::cout << "ASR -> wasm: " << std::setw(5) << time_visit_asr << std::endl;
        std::cout << "Save:       " << std::setw(5) << time_save << std::endl;
        int total = time_visit_asr + time_save;
        std::cout << "Total:      " << std::setw(5) << total << std::endl;
    }
    return 0;
}

}  // namespace LFortran
