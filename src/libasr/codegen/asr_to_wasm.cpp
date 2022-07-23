#include <iostream>
#include <memory>
#include <chrono>
#include <iomanip>
#include <fstream>

#include <libasr/asr.h>
#include <libasr/containers.h>
#include <libasr/codegen/asr_to_wasm.h>
#include <libasr/codegen/wasm_assembler.h>
#include <libasr/pass/implied_do_loops.h>
#include <libasr/pass/do_loops.h>
#include <libasr/pass/global_stmts.h>
#include <libasr/pass/unused_functions.h>
#include <libasr/exception.h>
#include <libasr/asr_utils.h>

namespace LFortran {

namespace {

    // This exception is used to abort the visitor pattern when an error occurs.
    class CodeGenAbort
    {
    };

    // Local exception that is only used in this file to exit the visitor
    // pattern and caught later (not propagated outside)
    class CodeGenError {
    public:
        diag::Diagnostic d;

    public:
        CodeGenError(const std::string &msg)
            : d{diag::Diagnostic(msg, diag::Level::Error, diag::Stage::CodeGen)}
        { }

        CodeGenError(const std::string &msg, const Location &loc)
            : d{diag::Diagnostic(msg, diag::Level::Error, diag::Stage::CodeGen, {
                diag::Label("", {loc})
            })}
        { }
    };

}  // namespace

// Platform dependent fast unique hash:
static uint64_t get_hash(ASR::asr_t *node)
{
    return (uint64_t)node;
}

struct import_func{
    std::string name;
    std::vector<std::pair<ASR::ttypeType, uint32_t>> param_types, result_types;
};

struct SymbolInfo
{
    bool needs_declaration;
    bool intrinsic_function;
    bool is_subroutine;
    uint32_t index;
    uint32_t no_of_variables;
    ASR::Variable_t *return_var;
    Vec<ASR::Variable_t *> subroutine_return_vars;

    SymbolInfo(bool is_subroutine) {
        this->needs_declaration = true;
        this->intrinsic_function = false;
        this->is_subroutine = is_subroutine;
        this->index = 0;
        this->no_of_variables = 0;
        this->return_var = nullptr;
    }
};

class ASRToWASMVisitor : public ASR::BaseVisitor<ASRToWASMVisitor> {
   public:
    Allocator &m_al;
    diag::Diagnostics &diag;

    bool intrinsic_module;
    SymbolInfo* cur_sym_info;
    uint32_t nesting_level;
    uint32_t cur_loop_nesting_level;

    Vec<uint8_t> m_type_section;
    Vec<uint8_t> m_import_section;
    Vec<uint8_t> m_func_section;
    Vec<uint8_t> m_export_section;
    Vec<uint8_t> m_code_section;
    Vec<uint8_t> m_data_section;

    uint32_t no_of_types;
    uint32_t no_of_functions;
    uint32_t no_of_imports;
    uint32_t no_of_data_segments;
    uint32_t last_str_len;
    uint32_t avail_mem_loc;

    std::map<uint64_t, uint32_t> m_var_name_idx_map;
    std::map<uint64_t, SymbolInfo *> m_func_name_idx_map;
    std::map<std::string, ASR::asr_t *> m_import_func_asr_map;

   public:
    ASRToWASMVisitor(Allocator &al, diag::Diagnostics &diagnostics): m_al(al), diag(diagnostics) {
        intrinsic_module = false;
        nesting_level = 0;
        cur_loop_nesting_level = 0;
        no_of_types = 0;
        avail_mem_loc = 0;
        no_of_functions = 0;
        no_of_imports = 0;
        no_of_data_segments = 0;
        m_type_section.reserve(m_al, 1024 * 128);
        m_import_section.reserve(m_al, 1024 * 128);
        m_func_section.reserve(m_al, 1024 * 128);
        m_export_section.reserve(m_al, 1024 * 128);
        m_code_section.reserve(m_al, 1024 * 128);
        m_data_section.reserve(m_al, 1024 * 128);
    }

    void get_wasm(Vec<uint8_t> &code){
        code.reserve(m_al, 8U /* preamble size */ + 8U /* (section id + section size) */ * 6U /* number of sections */
            + m_type_section.size() + m_import_section.size() + m_func_section.size()
            + m_export_section.size() + m_code_section.size() + m_data_section.size());

        wasm::emit_header(code, m_al);  // emit header and version
        wasm::encode_section(code, m_type_section, m_al, 1U, no_of_types);  // no_of_types indicates total (imported + defined) no of functions
        wasm::encode_section(code, m_import_section, m_al, 2U, no_of_imports);
        wasm::encode_section(code, m_func_section, m_al, 3U, no_of_functions);
        wasm::encode_section(code, m_export_section, m_al, 7U, no_of_functions);
        wasm::encode_section(code, m_code_section, m_al, 10U, no_of_functions);
        wasm::encode_section(code, m_data_section, m_al, 11U, no_of_data_segments);
    }

    ASR::asr_t* get_import_func_var_type(const ASR::TranslationUnit_t &x, std::pair<ASR::ttypeType, uint32_t> &type) {
        switch (type.first)
        {
            case ASR::ttypeType::Integer: return ASR::make_Integer_t(m_al, x.base.base.loc, type.second, nullptr, 0);
            case ASR::ttypeType::Real: return ASR::make_Real_t(m_al, x.base.base.loc, type.second, nullptr, 0);
            default: throw CodeGenError("Unsupported Type in Import Function");
        }
        return nullptr;
    }

    void emit_imports(const ASR::TranslationUnit_t &x){
        std::vector<import_func> import_funcs = {
            {"print_i32", { {ASR::ttypeType::Integer, 4} }, {}},
            {"print_i64", { {ASR::ttypeType::Integer, 8} }, {}},
            {"print_f32", { {ASR::ttypeType::Real, 4} }, {}},
            {"print_f64", { {ASR::ttypeType::Real, 8} }, {}},
            {"print_str", { {ASR::ttypeType::Integer, 4}, {ASR::ttypeType::Integer, 4} }, {}},
            {"flush_buf", {}, {}}
         };

        for (auto import_func:import_funcs) {
            Vec<ASR::expr_t*> params;
            params.reserve(m_al, import_func.param_types.size());
            uint32_t var_idx;
            for(var_idx = 0; var_idx < import_func.param_types.size(); var_idx++) {
                auto param = import_func.param_types[var_idx];
                auto type = get_import_func_var_type(x, param);
                auto variable = ASR::make_Variable_t(m_al, x.base.base.loc, nullptr, s2c(m_al, std::to_string(var_idx)),
                    ASR::intentType::In, nullptr, nullptr, ASR::storage_typeType::Default,
                    ASRUtils::TYPE(type), ASR::abiType::Source, ASR::accessType::Public,
                    ASR::presenceType::Required, false);
                auto var = ASR::make_Var_t(m_al, x.base.base.loc, ASR::down_cast<ASR::symbol_t>(variable));
                params.push_back(m_al, ASRUtils::EXPR(var));
            }

            auto func = ASR::make_Function_t(m_al, x.base.base.loc, x.m_global_scope, s2c(m_al, import_func.name),
                    params.data(), params.size(), nullptr, 0, nullptr, ASR::abiType::Source, ASR::accessType::Public,
                    ASR::deftypeType::Implementation, nullptr);
            m_import_func_asr_map[import_func.name] = func;


            wasm::emit_import_fn(m_import_section, m_al, "js", import_func.name, no_of_types);
            emit_function_prototype(*((ASR::Function_t *)func));
            no_of_imports++;
        }

        wasm::emit_import_mem(m_import_section, m_al, "js", "memory", 10U /* min page limit */, 10U /* max page limit */);
        no_of_imports++;
    }

    void visit_TranslationUnit(const ASR::TranslationUnit_t &x) {
        // All loose statements must be converted to a function, so the items
        // must be empty:
        LFORTRAN_ASSERT(x.n_items == 0);

        emit_imports(x);

        {
            // Process intrinsic modules in the right order
            std::vector<std::string> build_order
                = ASRUtils::determine_module_dependencies(x);
            for (auto &item : build_order) {
                LFORTRAN_ASSERT(x.m_global_scope->get_scope().find(item)
                    != x.m_global_scope->get_scope().end());
                if (startswith(item, "lfortran_intrinsic")) {
                    ASR::symbol_t *mod = x.m_global_scope->get_symbol(item);
                    this->visit_symbol(*mod);
                }
            }
        }

        //  // Process procedures first:
        // for (auto &item : x.m_global_scope->get_scope()) {
        //     if (ASR::is_a<ASR::Function_t>(*item.second)
        //         || ASR::is_a<ASR::Subroutine_t>(*item.second)) {
        //         visit_symbol(*item.second);
        //         // std::cout << "I am here -1: " << src << std::endl;
        //     }
        // }

        // // Then do all the modules in the right order
        // std::vector<std::string> build_order
        //     = LFortran::ASRUtils::determine_module_dependencies(x);
        // for (auto &item : build_order) {
        //     LFORTRAN_ASSERT(x.m_global_scope->get_scope().find(item)
        //         != x.m_global_scope->get_scope().end());
        //     if (!startswith(item, "lfortran_intrinsic")) {
        //         ASR::symbol_t *mod = x.m_global_scope->get_symbol(item);
        //         visit_symbol(*mod);
        //         // std::cout << "I am here -2: " << src << std::endl;
        //     }
        // }

        // then the main program:
        for (auto &item : x.m_global_scope->get_scope()) {
            if (ASR::is_a<ASR::Program_t>(*item.second)) {
                visit_symbol(*item.second);
            }
        }
    }

    void visit_Module(const ASR::Module_t &x) {
        if (startswith(x.m_name, "lfortran_intrinsic_")) {
            intrinsic_module = true;
        } else {
            intrinsic_module = false;
        }

        std::string contains;

        // Generate the bodies of subroutines
        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Subroutine_t>(*item.second)) {
                ASR::Subroutine_t *s = ASR::down_cast<ASR::Subroutine_t>(item.second);
                this->visit_Subroutine(*s);
            }
            if (ASR::is_a<ASR::Function_t>(*item.second)) {
                ASR::Function_t *s = ASR::down_cast<ASR::Function_t>(item.second);
                this->visit_Function(*s);
            }
        }
        intrinsic_module = false;
    }

    void visit_Program(const ASR::Program_t &x) {

        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Subroutine_t>(*item.second)) {
                ASR::Subroutine_t *s = ASR::down_cast<ASR::Subroutine_t>(item.second);
                this->visit_Subroutine(*s);
            }
            if (ASR::is_a<ASR::Function_t>(*item.second)) {
                ASR::Function_t *s = ASR::down_cast<ASR::Function_t>(item.second);
                visit_Function(*s);
            }
        }

        // Generate main program code
        auto main_func = ASR::make_Function_t(m_al, x.base.base.loc, x.m_symtab, s2c(m_al, "_lcompilers_main"),
            nullptr, 0, x.m_body, x.n_body, nullptr, ASR::abiType::Source, ASR::accessType::Public,
            ASR::deftypeType::Implementation, nullptr);
        this->visit_Function(*((ASR::Function_t *)main_func));
    }

    void emit_var_type(Vec<uint8_t> &code, ASR::Variable_t *v){
        // bool is_array = ASRUtils::is_array(v->m_type);
        // bool dummy = ASRUtils::is_arg_dummy(v->m_intent);
        if (ASRUtils::is_pointer(v->m_type)) {
            ASR::ttype_t *t2 = ASR::down_cast<ASR::Pointer_t>(v->m_type)->m_type;
            if (ASRUtils::is_integer(*t2)) {
                ASR::Integer_t *t = ASR::down_cast<ASR::Integer_t>(t2);
                // size_t size;
                diag.codegen_warning_label("Pointers are not currently supported",
                    {v->base.base.loc}, "emitting integer for now");
                if (t->m_kind == 4) {
                    wasm::emit_b8(code, m_al, wasm::type::i32);
                }
                else if (t->m_kind == 8) {
                    wasm::emit_b8(code, m_al, wasm::type::i64);
                }
                else{
                    throw CodeGenError("Integers of kind 4 and 8 only supported");
                }
                // throw CodeGenError("Pointers are not yet supported");
                // std::string dims = convert_dims(t->n_dims, t->m_dims, size);
                // std::string type_name = "int" + std::to_string(t->m_kind * 8) + "_t";
                // if( is_array ) {
                //     if( use_templates_for_arrays ) {
                //         sub += generate_templates_for_arrays(std::string(v->m_name));
                //     } else {
                //         std::string encoded_type_name = "i" + std::to_string(t->m_kind * 8);
                //         generate_array_decl(sub, std::string(v->m_name), type_name, dims,
                //                             encoded_type_name, t->m_dims, t->n_dims, size,
                //                             use_ref, dummy,
                //                             v->m_intent != ASRUtils::intent_in &&
                //                             v->m_intent != ASRUtils::intent_inout &&
                //                             v->m_intent != ASRUtils::intent_out, true);
                //     }
                // } else {
                //     sub = format_type(dims, type_name, v->m_name, use_ref, dummy);
                // }
            } else {
                diag.codegen_error_label("Type number '"
                    + std::to_string(v->m_type->type)
                    + "' not supported", {v->base.base.loc}, "");
                throw CodeGenAbort();
            }
        } else if (ASRUtils::is_integer(*v->m_type)) {
            // checking for array is currently omitted
            ASR::Integer_t* v_int = ASR::down_cast<ASR::Integer_t>(v->m_type);
            if (v_int->m_kind == 4) {
                wasm::emit_b8(code, m_al, wasm::type::i32);
            }
            else if (v_int->m_kind == 8) {
                wasm::emit_b8(code, m_al, wasm::type::i64);
            }
            else{
                throw CodeGenError("Integers of kind 4 and 8 only supported");
            }
        } else if (ASRUtils::is_real(*v->m_type)) {
            // checking for array is currently omitted
            ASR::Real_t* v_float = ASR::down_cast<ASR::Real_t>(v->m_type);
            if (v_float->m_kind == 4) {
                wasm::emit_b8(code, m_al, wasm::type::f32);
            }
            else if(v_float->m_kind == 8){
                wasm::emit_b8(code, m_al, wasm::type::f64);
            }
            else {
                throw CodeGenError("Floating Points of kind 4 and 8 only supported");
            }
        } else if (ASRUtils::is_logical(*v->m_type)) {
            // checking for array is currently omitted
            ASR::Logical_t* v_logical = ASR::down_cast<ASR::Logical_t>(v->m_type);
            if (v_logical->m_kind == 4) {
                wasm::emit_b8(code, m_al, wasm::type::i32);
            }
            else if(v_logical->m_kind == 8){
                wasm::emit_b8(code, m_al, wasm::type::i64);
            }
            else {
                throw CodeGenError("Logicals of kind 4 and 8 only supported");
            }
        } else if (ASRUtils::is_character(*v->m_type)) {
            // Todo: Implement this

            // checking for array is currently omitted
            ASR::Character_t* v_int = ASR::down_cast<ASR::Character_t>(v->m_type);
            /* Currently Assuming character as integer of kind 1 */
            if (v_int->m_kind == 1) {
                wasm::emit_b8(code, m_al, wasm::type::i32);
            }
            else{
                throw CodeGenError("Characters of kind 1 only supported");
            }
        } else {
            // throw CodeGenError("Param, Result, Var Types other than integer, floating point and logical not yet supported");
            diag.codegen_warning_label("Unsupported variable type: " + ASRUtils::type_to_str(v->m_type),
                    {v->base.base.loc}, "here");
        }
    }

    template<typename T>
    void emit_local_vars(const T& x, int var_idx /* starting index for local vars */) {
        /********************* Local Vars Types List *********************/
        uint32_t len_idx_code_section_local_vars_list = wasm::emit_len_placeholder(m_code_section, m_al);
        int local_vars_cnt = 0;
        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Variable_t>(*item.second)) {
                ASR::Variable_t *v = ASR::down_cast<ASR::Variable_t>(item.second);
                if (v->m_intent == ASRUtils::intent_local || v->m_intent == ASRUtils::intent_return_var) {
                    wasm::emit_u32(m_code_section, m_al, 1U);    // count of local vars of this type
                    emit_var_type(m_code_section, v); // emit the type of this var
                    m_var_name_idx_map[get_hash((ASR::asr_t *)v)] = var_idx++;
                    local_vars_cnt++;
                }
            }
        }
        // fixup length of local vars list
        wasm::emit_u32_b32_idx(m_code_section, m_al, len_idx_code_section_local_vars_list, local_vars_cnt);

        // initialize the value for local variables if initialization exists
        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Variable_t>(*item.second)) {
                ASR::Variable_t *v = ASR::down_cast<ASR::Variable_t>(item.second);
                if (v->m_intent == ASRUtils::intent_local || v->m_intent == ASRUtils::intent_return_var) {
                    if(v->m_symbolic_value) {
                        this->visit_expr(*v->m_symbolic_value);
                        // Todo: Checking for Array is currently omitted
                        LFORTRAN_ASSERT(m_var_name_idx_map.find(get_hash((ASR::asr_t *)v)) != m_var_name_idx_map.end())
                        wasm::emit_set_local(m_code_section, m_al, m_var_name_idx_map[get_hash((ASR::asr_t *)v)]);
                    }
                }
            }
        }
    }

    template<typename T>
    void emit_function_prototype(const T& x) {
        /********************* New Type Declaration *********************/
        wasm::emit_b8(m_type_section, m_al, 0x60);
        SymbolInfo* s = new SymbolInfo(false);

        /********************* Parameter Types List *********************/
        wasm::emit_u32(m_type_section, m_al, x.n_args);
        for (size_t i = 0; i < x.n_args; i++) {
            ASR::Variable_t *arg = ASRUtils::EXPR2VAR(x.m_args[i]);
            LFORTRAN_ASSERT(ASRUtils::is_arg_dummy(arg->m_intent));
            emit_var_type(m_type_section, arg);
            m_var_name_idx_map[get_hash((ASR::asr_t *)arg)] = i;
            s->no_of_variables++;
        }

        /********************* Result Types List *********************/
        if (x.m_return_var) {
            wasm::emit_u32(m_type_section, m_al, 1U); // there is just one return variable
            s->return_var = ASRUtils::EXPR2VAR(x.m_return_var);
            emit_var_type(m_type_section, s->return_var);
        } else {
            wasm::emit_u32(m_type_section, m_al, 0U); // the function does not return
            s->return_var = nullptr;
        }

        /********************* Add Type to Map *********************/
        s->index = no_of_types++;
        m_func_name_idx_map[get_hash((ASR::asr_t *)&x)] = s; // add function to map
    }

    template<typename T>
    void emit_function_body(const T& x) {
        LFORTRAN_ASSERT(m_func_name_idx_map.find(get_hash((ASR::asr_t *)&x)) != m_func_name_idx_map.end());

        cur_sym_info =  m_func_name_idx_map[get_hash((ASR::asr_t *)&x)];

        /********************* Reference Function Prototype *********************/
        wasm::emit_u32(m_func_section, m_al, cur_sym_info->index);

        /********************* Function Body Starts Here *********************/
        uint32_t len_idx_code_section_func_size = wasm::emit_len_placeholder(m_code_section, m_al);

        emit_local_vars(x, cur_sym_info->no_of_variables);
        for (size_t i = 0; i < x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
        }
        if ((x.n_body > 0) && !ASR::is_a<ASR::Return_t>(*x.m_body[x.n_body - 1])) {
            handle_return();
        }
        wasm::emit_expr_end(m_code_section, m_al);

        wasm::fixup_len(m_code_section, m_al, len_idx_code_section_func_size);

        /********************* Export the function *********************/
        wasm::emit_export_fn(m_export_section, m_al, x.m_name, cur_sym_info->index); //  add function to export
        no_of_functions++;
    }

    template <typename T>
    bool is_unsupported_function(const T& x) {
        std::string func_or_sub = "";
        if (x.class_type == ASR::symbolType::Function) {
            func_or_sub = "Function";
        } else if (x.class_type == ASR::symbolType::Subroutine) {
            func_or_sub = "Subroutine";
        } else {
            throw CodeGenError("has_c_function_call: C call unknown type");
        }
        if(!x.n_body) {
            diag.codegen_warning_label(func_or_sub + " with no body", {x.base.base.loc}, std::string(x.m_name));
            return true;
        }
        if (x.m_abi == ASR::abiType::BindC
                && x.m_deftype == ASR::deftypeType::Interface) {
                diag.codegen_warning_label("WASM: BindC and Interface " + func_or_sub + " not yet spported", { x.base.base.loc }, std::string(x.m_name));
                return true;
        }
        for (size_t i = 0; i < x.n_body; i++) {
            if (x.m_body[i]->type == ASR::stmtType::SubroutineCall) {
                diag.codegen_warning_label("WASM: Calls to C " + func_or_sub + " are not yet supported", {x.base.base.loc}, std::string(x.m_name));
                return true;
            }
        }
        return false;
    }

    void visit_Function(const ASR::Function_t &x) {
        if (is_unsupported_function(x)) {
            return;
        }

        emit_function_prototype(x);
        emit_function_body(x);
    }

    void emit_subroutine_prototype(const ASR::Subroutine_t & x) {
        /********************* New Type Declaration *********************/
        wasm::emit_b8(m_type_section, m_al, 0x60);
        SymbolInfo* s = new SymbolInfo(true);

        /********************* Parameter Types List *********************/
        uint32_t len_idx_type_section_param_types_list = wasm::emit_len_placeholder(m_type_section, m_al);
        s->subroutine_return_vars.reserve(m_al, x.n_args);
        for (size_t i = 0; i < x.n_args; i++) {
            ASR::Variable_t *arg = ASRUtils::EXPR2VAR(x.m_args[i]);
            if (arg->m_intent == ASR::intentType::In || arg->m_intent == ASR::intentType::Out) {
                emit_var_type(m_type_section, arg);
                m_var_name_idx_map[get_hash((ASR::asr_t *)arg)] = s->no_of_variables++;
                if (arg->m_intent == ASR::intentType::Out) {
                    s->subroutine_return_vars.push_back(m_al, arg);
                }
            }
        }
        wasm::fixup_len(m_type_section, m_al, len_idx_type_section_param_types_list);

        /********************* Result Types List *********************/
        uint32_t len_idx_type_section_return_types_list = wasm::emit_len_placeholder(m_type_section, m_al);
        for (size_t i = 0; i < x.n_args; i++) {
            ASR::Variable_t *arg = ASRUtils::EXPR2VAR(x.m_args[i]);
            if (arg->m_intent == ASR::intentType::Out) {
                emit_var_type(m_type_section, arg);
            }
        }
        wasm::fixup_len(m_type_section, m_al, len_idx_type_section_return_types_list);

        /********************* Add Type to Map *********************/
        s->index = no_of_types++;
        m_func_name_idx_map[get_hash((ASR::asr_t *)&x)] = s; // add function to map
    }

    void visit_Subroutine(const ASR::Subroutine_t & x) {
        if (is_unsupported_function(x)) {
            return;
        }

        emit_subroutine_prototype(x);
        emit_function_body(x);
    }

    void visit_Assignment(const ASR::Assignment_t &x) {
        this->visit_expr(*x.m_value);
        // this->visit_expr(*x.m_target);
        if (ASR::is_a<ASR::Var_t>(*x.m_target)) {
            ASR::Variable_t *asr_target = ASRUtils::EXPR2VAR(x.m_target);
            LFORTRAN_ASSERT(m_var_name_idx_map.find(get_hash((ASR::asr_t *)asr_target)) != m_var_name_idx_map.end());
            wasm::emit_set_local(m_code_section, m_al, m_var_name_idx_map[get_hash((ASR::asr_t *)asr_target)]);
        } else if (ASR::is_a<ASR::ArrayItem_t>(*x.m_target)) {
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
                default: {
                    // Todo: Implement Pow Operation
                    throw CodeGenError("IntegerBinop: Pow Operation not yet implemented");
                }
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
                default: {
                    // Todo: Implement Pow Operation
                    throw CodeGenError("IntegerBinop: Pow Operation not yet implemented");
                }
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
                default: {
                    // Todo: Implement Pow Operation
                    throw CodeGenError("RealBinop: Pow Operation not yet implemented");
                }
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
                default: {
                    // Todo: Implement Pow Operation
                    throw CodeGenError("RealBinop: Pow Operation not yet implemented");
                }
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

    template<typename T>
    void handle_integer_compare(const T &x){
        if (x.m_value) {
            visit_expr(*x.m_value);
            return;
        }
        this->visit_expr(*x.m_left);
        this->visit_expr(*x.m_right);
        int a_kind = ASRUtils::extract_kind_from_ttype_t(x.m_type);
        if (a_kind == 4) {
            switch (x.m_op) {
                case (ASR::cmpopType::Eq) : { wasm::emit_i32_eq(m_code_section, m_al); break; }
                case (ASR::cmpopType::Gt) : { wasm::emit_i32_gt_s(m_code_section, m_al);  break; }
                case (ASR::cmpopType::GtE) : { wasm::emit_i32_ge_s(m_code_section, m_al); break; }
                case (ASR::cmpopType::Lt) : { wasm::emit_i32_lt_s(m_code_section, m_al);  break; }
                case (ASR::cmpopType::LtE) : { wasm::emit_i32_le_s(m_code_section,m_al); break; }
                case (ASR::cmpopType::NotEq): { wasm::emit_i32_ne(m_code_section, m_al); break; }
                default : throw CodeGenError("handle_integer_compare: Kind 4: Unhandled switch case");
            }
        } else if (a_kind == 8) {
            switch (x.m_op) {
                case (ASR::cmpopType::Eq) : { wasm::emit_i64_eq(m_code_section, m_al); break; }
                case (ASR::cmpopType::Gt) : { wasm::emit_i64_gt_s(m_code_section, m_al);  break; }
                case (ASR::cmpopType::GtE) : { wasm::emit_i64_ge_s(m_code_section, m_al); break; }
                case (ASR::cmpopType::Lt) : { wasm::emit_i64_lt_s(m_code_section, m_al);  break; }
                case (ASR::cmpopType::LtE) : { wasm::emit_i64_le_s(m_code_section,m_al); break; }
                case (ASR::cmpopType::NotEq): { wasm::emit_i64_ne(m_code_section, m_al); break; }
                default : throw CodeGenError("handle_integer_compare: Kind 8: Unhandled switch case");
            }
        } else {
            throw CodeGenError("IntegerCompare: kind 4 and 8 supported only");
        }
    }

    void handle_real_compare(const ASR::RealCompare_t &x){
        if (x.m_value) {
            visit_expr(*x.m_value);
            return;
        }
        this->visit_expr(*x.m_left);
        this->visit_expr(*x.m_right);
        int a_kind = ASRUtils::extract_kind_from_ttype_t(x.m_type);
        if (a_kind == 4) {
            switch (x.m_op) {
                case (ASR::cmpopType::Eq) : { wasm::emit_f32_eq(m_code_section, m_al); break; }
                case (ASR::cmpopType::Gt) : { wasm::emit_f32_gt(m_code_section, m_al);  break; }
                case (ASR::cmpopType::GtE) : { wasm::emit_f32_ge(m_code_section, m_al); break; }
                case (ASR::cmpopType::Lt) : { wasm::emit_f32_lt(m_code_section, m_al);  break; }
                case (ASR::cmpopType::LtE) : { wasm::emit_f32_le(m_code_section,m_al); break; }
                case (ASR::cmpopType::NotEq): { wasm::emit_f32_ne(m_code_section, m_al); break; }
                default : throw CodeGenError("handle_real_compare: Kind 4: Unhandled switch case");
            }
        } else if (a_kind == 8) {
            switch (x.m_op) {
                case (ASR::cmpopType::Eq) : { wasm::emit_f64_eq(m_code_section, m_al); break; }
                case (ASR::cmpopType::Gt) : { wasm::emit_f64_gt(m_code_section, m_al);  break; }
                case (ASR::cmpopType::GtE) : { wasm::emit_f64_ge(m_code_section, m_al); break; }
                case (ASR::cmpopType::Lt) : { wasm::emit_f64_lt(m_code_section, m_al);  break; }
                case (ASR::cmpopType::LtE) : { wasm::emit_f64_le(m_code_section,m_al); break; }
                case (ASR::cmpopType::NotEq): { wasm::emit_f64_ne(m_code_section, m_al); break; }
                default : throw CodeGenError("handle_real_compare: Kind 8: Unhandled switch case");
            }
        } else {
            throw CodeGenError("RealCompare: kind 4 and 8 supported only");
        }
    }

    void visit_IntegerCompare(const ASR::IntegerCompare_t &x) {
        handle_integer_compare(x);
    }

    void visit_RealCompare(const ASR::RealCompare_t &x) {
        handle_real_compare(x);
    }

    void visit_ComplexCompare(const ASR::ComplexCompare_t & /*x*/) {
        throw CodeGenError("Complex Types not yet supported");
    }

    void visit_LogicalCompare(const ASR::LogicalCompare_t &x) {
        handle_integer_compare(x);
    }

    void visit_StringCompare(const ASR::StringCompare_t & /*x*/) {
        throw CodeGenError("String Types not yet supported");
    }

    void visit_LogicalBinOp(const ASR::LogicalBinOp_t &x) {
        if(x.m_value){
            visit_expr(*x.m_value);
            return;
        }
        this->visit_expr(*x.m_left);
        this->visit_expr(*x.m_right);
        int a_kind = ASRUtils::extract_kind_from_ttype_t(x.m_type);
        if (a_kind == 4) {
            switch (x.m_op) {
                case (ASR::logicalbinopType::And): { wasm::emit_i32_and(m_code_section, m_al); break; }
                case (ASR::logicalbinopType::Or): { wasm::emit_i32_or(m_code_section, m_al); break; }
                case ASR::logicalbinopType::Xor: { wasm::emit_i32_xor(m_code_section, m_al); break; }
                case (ASR::logicalbinopType::NEqv): { wasm::emit_i32_xor(m_code_section, m_al); break; }
                case (ASR::logicalbinopType::Eqv): { wasm::emit_i32_eq(m_code_section, m_al); break; }
                default : throw CodeGenError("LogicalBinOp: Kind 4: Unhandled switch case");
            }
        } else if (a_kind == 8) {
            switch (x.m_op) {
                case (ASR::logicalbinopType::And): { wasm::emit_i64_and(m_code_section, m_al); break; }
                case (ASR::logicalbinopType::Or): { wasm::emit_i64_or(m_code_section, m_al); break; }
                case ASR::logicalbinopType::Xor: { wasm::emit_i64_xor(m_code_section, m_al); break; }
                case (ASR::logicalbinopType::NEqv): { wasm::emit_i64_xor(m_code_section, m_al); break; }
                case (ASR::logicalbinopType::Eqv): { wasm::emit_i64_eq(m_code_section, m_al); break; }
                default : throw CodeGenError("LogicalBinOp: Kind 8: Unhandled switch case");
            }
        } else {
            throw CodeGenError("LogicalBinOp: kind 4 and 8 supported only");
        }
    }

    void visit_LogicalNot(const ASR::LogicalNot_t &x) {
        if (x.m_value) {
            this->visit_expr(*x.m_value);
            return;
        }
        this->visit_expr(*x.m_arg);
        int a_kind = ASRUtils::extract_kind_from_ttype_t(x.m_type);
        if (a_kind == 4) {
            wasm::emit_i32_eqz(m_code_section, m_al);
        } else if (a_kind == 8) {
            wasm::emit_i64_eqz(m_code_section, m_al);
        } else {
            throw CodeGenError("LogicalNot: kind 4 and 8 supported only");
        }
    }

    void visit_Var(const ASR::Var_t &x) {
        const ASR::symbol_t *s = ASRUtils::symbol_get_past_external(x.m_v);
        auto v = ASR::down_cast<ASR::Variable_t>(s);
        switch (v->m_type->type) {
            case ASR::ttypeType::Integer:
            case ASR::ttypeType::Logical:
            case ASR::ttypeType::Real: {
                LFORTRAN_ASSERT(m_var_name_idx_map.find(get_hash((ASR::asr_t *)v)) != m_var_name_idx_map.end());
                wasm::emit_get_local(m_code_section, m_al, m_var_name_idx_map[get_hash((ASR::asr_t *)v)]);
                break;
            }

            default:
                throw CodeGenError("Only Integer and Float Variable types currently supported");
        }
    }

    void handle_return() {
        if (cur_sym_info->return_var) {
            LFORTRAN_ASSERT(m_var_name_idx_map.find(get_hash((ASR::asr_t *)cur_sym_info->return_var)) != m_var_name_idx_map.end());
            wasm::emit_get_local(m_code_section, m_al, m_var_name_idx_map[get_hash((ASR::asr_t *)cur_sym_info->return_var)]);
            wasm::emit_b8(m_code_section, m_al, 0x0F); // emit wasm return instruction
        } else if(cur_sym_info->is_subroutine) {
            for(auto return_var:cur_sym_info->subroutine_return_vars) {
                wasm::emit_get_local(m_code_section, m_al, m_var_name_idx_map[get_hash((ASR::asr_t *)(return_var))]);
            }
            wasm::emit_b8(m_code_section, m_al, 0x0F); // emit wasm return instruction
        }
    }

    void visit_Return(const ASR::Return_t & /* x */) {
        handle_return();
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

    void visit_LogicalConstant(const ASR::LogicalConstant_t &x) {
        bool val = x.m_value;
        int a_kind = ((ASR::Logical_t *)(&(x.m_type->base)))->m_kind;
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
                throw CodeGenError("Constant Logical: Only kind 4 and 8 supported");
            }
        }
    }

    void visit_StringConstant(const ASR::StringConstant_t &x){
        // Todo: Add a check here if there is memory available to store the given string
        wasm::emit_str_const(m_data_section, m_al, avail_mem_loc, x.m_s);
        last_str_len = strlen(x.m_s);
        avail_mem_loc += last_str_len;
        no_of_data_segments++;
    }

    void visit_FunctionCall(const ASR::FunctionCall_t &x) {
        ASR::Function_t *fn = ASR::down_cast<ASR::Function_t>(ASRUtils::symbol_get_past_external(x.m_name));

        for (size_t i = 0; i < x.n_args; i++) {
            visit_expr(*x.m_args[i].m_value);
        }

        LFORTRAN_ASSERT(m_func_name_idx_map.find(get_hash((ASR::asr_t *)fn)) != m_func_name_idx_map.end())
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash((ASR::asr_t *)fn)]->index);
    }

    void visit_SubroutineCall(const ASR::SubroutineCall_t &x) {
        ASR::Subroutine_t *s = ASR::down_cast<ASR::Subroutine_t>(ASRUtils::symbol_get_past_external(x.m_name));
        // TODO: use a mapping with a hash(s) instead:
        // std::string sym_name = s->m_name;
        // if (sym_name == "exit") {
        //     sym_name = "_xx_lcompilers_changed_exit_xx";
        // }

        Vec<ASR::Variable_t *> intent_out_passed_vars;
        intent_out_passed_vars.reserve(m_al, s->n_args);
        if (x.n_args == s->n_args) {
            for (size_t i = 0; i < x.n_args; i++) {
                ASR::Variable_t *arg = ASRUtils::EXPR2VAR(s->m_args[i]);
                if (arg->m_intent == ASRUtils::intent_out) {
                    intent_out_passed_vars.push_back(m_al, ASRUtils::EXPR2VAR(x.m_args[i].m_value));
                }
                visit_expr(*x.m_args[i].m_value);
            }
        } else {
            throw CodeGenError("visitSubroutineCall: Number of arguments passed do not match the number of parameters");
        }

        LFORTRAN_ASSERT(m_func_name_idx_map.find(get_hash((ASR::asr_t *)s)) != m_func_name_idx_map.end());
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash((ASR::asr_t *)s)]->index);

        for(auto return_var:intent_out_passed_vars) {
            LFORTRAN_ASSERT(m_var_name_idx_map.find(get_hash((ASR::asr_t *)return_var)) != m_var_name_idx_map.end());
            wasm::emit_set_local(m_code_section, m_al, m_var_name_idx_map[get_hash((ASR::asr_t *)return_var)]);
        }
    }

    inline ASR::ttype_t* extract_ttype_t_from_expr(ASR::expr_t* expr) {
        return ASRUtils::expr_type(expr);
    }

    void extract_kinds(const ASR::Cast_t& x,
                       int& arg_kind, int& dest_kind)
    {
        dest_kind = ASRUtils::extract_kind_from_ttype_t(x.m_type);
        ASR::ttype_t* curr_type = extract_ttype_t_from_expr(x.m_arg);
        LFORTRAN_ASSERT(curr_type != nullptr)
        arg_kind = ASRUtils::extract_kind_from_ttype_t(curr_type);
    }

    void visit_Cast(const ASR::Cast_t &x) {
        if (x.m_value) {
            this->visit_expr(*x.m_value);
            return;
        }
        this->visit_expr(*x.m_arg);
        switch (x.m_kind) {
            case (ASR::cast_kindType::IntegerToReal) : {
                int arg_kind = -1, dest_kind = -1;
                extract_kinds(x, arg_kind, dest_kind);
                if( arg_kind > 0 && dest_kind > 0){
                    if( arg_kind == 4 && dest_kind == 4 ) {
                        wasm::emit_f32_convert_i32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 8 ) {
                        wasm::emit_f64_convert_i64_s(m_code_section, m_al);
                    } else if( arg_kind == 4 && dest_kind == 8 ) {
                        wasm::emit_f64_convert_i32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 4 ) {
                        wasm::emit_f32_convert_i64_s(m_code_section, m_al);
                    } else {
                        std::string msg = "Conversion from " + std::to_string(arg_kind) +
                                          " to " + std::to_string(dest_kind) + " not implemented yet.";
                        throw CodeGenError(msg);
                    }
                }
                break;
            }
            case (ASR::cast_kindType::RealToInteger) : {
                int arg_kind = -1, dest_kind = -1;
                extract_kinds(x, arg_kind, dest_kind);
                if( arg_kind > 0 && dest_kind > 0){
                    if( arg_kind == 4 && dest_kind == 4 ) {
                        wasm::emit_i32_trunc_f32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 8 ) {
                        wasm::emit_i64_trunc_f64_s(m_code_section, m_al);
                    } else if( arg_kind == 4 && dest_kind == 8 ) {
                        wasm::emit_i64_trunc_f32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 4 ) {
                        wasm::emit_i32_trunc_f64_s(m_code_section, m_al);
                    } else {
                        std::string msg = "Conversion from " + std::to_string(arg_kind) +
                                          " to " + std::to_string(dest_kind) + " not implemented yet.";
                        throw CodeGenError(msg);
                    }
                }
                break;
            }
            case (ASR::cast_kindType::RealToComplex) : {
                throw CodeGenError("Complex types are not supported yet.");
                break;
            }
            case (ASR::cast_kindType::IntegerToComplex) : {
                throw CodeGenError("Complex types are not supported yet.");
                break;
            }
            case (ASR::cast_kindType::IntegerToLogical) : {
                int arg_kind = -1, dest_kind = -1;
                extract_kinds(x, arg_kind, dest_kind);
                if( arg_kind > 0 && dest_kind > 0){
                    if( arg_kind == 4 && dest_kind == 4 ) {
                        wasm::emit_i32_eqz(m_code_section, m_al);
                        wasm::emit_i32_eqz(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 8 ) {
                        wasm::emit_i64_eqz(m_code_section, m_al);
                        wasm::emit_i64_eqz(m_code_section, m_al);
                    } else if( arg_kind == 4 && dest_kind == 8 ) {
                        wasm::emit_i64_eqz(m_code_section, m_al);
                        wasm::emit_i64_eqz(m_code_section, m_al);
                        wasm::emit_i32_wrap_i64(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 4 ) {
                        wasm::emit_i32_eqz(m_code_section, m_al);
                        wasm::emit_i32_eqz(m_code_section, m_al);
                        wasm::emit_i64_extend_i32_s(m_code_section, m_al);
                    } else {
                        std::string msg = "Conversion from " + std::to_string(arg_kind) +
                                          " to " + std::to_string(dest_kind) + " not implemented yet.";
                        throw CodeGenError(msg);
                    }
                }
                break;
            }
            case (ASR::cast_kindType::RealToLogical) : {
                int arg_kind = -1, dest_kind = -1;
                extract_kinds(x, arg_kind, dest_kind);
                if( arg_kind > 0 && dest_kind > 0){
                    if( arg_kind == 4 && dest_kind == 4 ) {
                        wasm::emit_f32_const(m_code_section, m_al, 0.0);
                        wasm::emit_f32_eq(m_code_section, m_al);
                        wasm::emit_i32_eqz(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 8 ) {
                        wasm::emit_f64_const(m_code_section, m_al, 0.0);
                        wasm::emit_f64_eq(m_code_section, m_al);
                        wasm::emit_i64_eqz(m_code_section, m_al);
                    } else if( arg_kind == 4 && dest_kind == 8 ) {
                        wasm::emit_f32_const(m_code_section, m_al, 0.0);
                        wasm::emit_f32_eq(m_code_section, m_al);
                        wasm::emit_i32_eqz(m_code_section, m_al);
                        wasm::emit_i64_extend_i32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 4 ) {
                        wasm::emit_f64_const(m_code_section, m_al, 0.0);
                        wasm::emit_f64_eq(m_code_section, m_al);
                        wasm::emit_i64_eqz(m_code_section, m_al);
                        wasm::emit_i32_wrap_i64(m_code_section, m_al);
                    } else {
                        std::string msg = "Conversion from " + std::to_string(arg_kind) +
                                          " to " + std::to_string(dest_kind) + " not implemented yet.";
                        throw CodeGenError(msg);
                    }
                }
                break;
            }
            case (ASR::cast_kindType::CharacterToLogical) : {
                throw CodeGenError(R"""(STrings are not supported yet)""",
                                            x.base.base.loc);
                break;
            }
            case (ASR::cast_kindType::ComplexToLogical) : {
               throw CodeGenError("Complex types are not supported yet.");
                break;
            }
            case (ASR::cast_kindType::LogicalToInteger) : {
                // do nothing as logicals are already implemented as integers in wasm backend
                break;
            }
            case (ASR::cast_kindType::LogicalToReal) : {
                int arg_kind = -1, dest_kind = -1;
                extract_kinds(x, arg_kind, dest_kind);
                if( arg_kind > 0 && dest_kind > 0){
                    if( arg_kind == 4 && dest_kind == 4 ) {
                        wasm::emit_f32_convert_i32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 8 ) {
                        wasm::emit_f64_convert_i64_s(m_code_section, m_al);
                    } else if( arg_kind == 4 && dest_kind == 8 ) {
                        wasm::emit_f64_convert_i32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 4 ) {
                        wasm::emit_f32_convert_i64_s(m_code_section, m_al);
                    } else {
                        std::string msg = "Conversion from " + std::to_string(arg_kind) +
                                          " to " + std::to_string(dest_kind) + " not implemented yet.";
                        throw CodeGenError(msg);
                    }
                }
                break;
            }
            case (ASR::cast_kindType::IntegerToInteger) : {
                int arg_kind = -1, dest_kind = -1;
                extract_kinds(x, arg_kind, dest_kind);
                if( arg_kind > 0 && dest_kind > 0 &&
                    arg_kind != dest_kind )
                {
                    if( arg_kind == 4 && dest_kind == 8 ) {
                        wasm::emit_i64_extend_i32_s(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 4 ) {
                        wasm::emit_i32_wrap_i64(m_code_section, m_al);
                    } else {
                        std::string msg = "Conversion from " + std::to_string(arg_kind) +
                                          " to " + std::to_string(dest_kind) + " not implemented yet.";
                        throw CodeGenError(msg);
                    }
                }
                break;
            }
            case (ASR::cast_kindType::RealToReal) : {
                int arg_kind = -1, dest_kind = -1;
                extract_kinds(x, arg_kind, dest_kind);
                if( arg_kind > 0 && dest_kind > 0 &&
                    arg_kind != dest_kind )
                {
                    if( arg_kind == 4 && dest_kind == 8 ) {
                        wasm::emit_f64_promote_f32(m_code_section, m_al);
                    } else if( arg_kind == 8 && dest_kind == 4 ) {
                        wasm::emit_f32_demote_f64(m_code_section, m_al);
                    } else {
                        std::string msg = "Conversion from " + std::to_string(arg_kind) +
                                          " to " + std::to_string(dest_kind) + " not implemented yet.";
                        throw CodeGenError(msg);
                    }
                }
                break;
            }
            case (ASR::cast_kindType::ComplexToComplex) : {
                throw CodeGenError("Complex types are not supported yet.");
                break;
            }
            case (ASR::cast_kindType::ComplexToReal) : {
                throw CodeGenError("Complex types are not supported yet.");
                break;
            }
            default : throw CodeGenError("Cast kind not implemented");
        }
    }

    template <typename T>
    void handle_print(const T &x){
        for (size_t i=0; i<x.n_values; i++) {
            this->visit_expr(*x.m_values[i]);
            ASR::expr_t *v = x.m_values[i];
            ASR::ttype_t *t = ASRUtils::expr_type(v);
            int a_kind = ASRUtils::extract_kind_from_ttype_t(t);

            if (ASRUtils::is_integer(*t) || ASRUtils::is_logical(*t)) {
                switch( a_kind ) {
                    case 4 : {
                        // the value is already on stack. call JavaScript print_i32
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["print_i32"])]->index);
                        break;
                    }
                    case 8 : {
                        // the value is already on stack. call JavaScript print_i64
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["print_i64"])]->index);
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
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["print_f32"])]->index);
                        break;
                    }
                    case 8 : {
                        // the value is already on stack. call JavaScript print_f64
                        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["print_f64"])]->index);
                        break;
                    }
                    default: {
                        throw CodeGenError(R"""(Printing support is available only
                                            for 32, and 64 bit real kinds.)""");
                    }
                }
            } else if (t->type == ASR::ttypeType::Character) {
                // push string location and its size on function stack
                wasm::emit_i32_const(m_code_section, m_al, avail_mem_loc - last_str_len);
                wasm::emit_i32_const(m_code_section, m_al, last_str_len);

                // call JavaScript print_str
                wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["print_str"])]->index);

            }
        }

        // call JavaScript flush_buf
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["flush_buf"])]->index);
    }

    void visit_Print(const ASR::Print_t &x){
        if (x.m_fmt != nullptr) {
            diag.codegen_warning_label("format string in `print` is not implemented yet and it is currently treated as '*'",
                {x.m_fmt->base.loc}, "treated as '*'");
        }
        handle_print(x);
    }

    void visit_FileWrite(const ASR::FileWrite_t &x) {
        if (x.m_fmt != nullptr) {
            diag.codegen_warning_label("format string in `print` is not implemented yet and it is currently treated as '*'",
                {x.m_fmt->base.loc}, "treated as '*'");
        }
        if (x.m_unit != nullptr) {
            diag.codegen_error_label("unit in write() is not implemented yet",
                {x.m_unit->base.loc}, "not implemented");
            throw CodeGenAbort();
        }
        handle_print(x);
    }

    void visit_FileRead(const ASR::FileRead_t &x) {
        if (x.m_fmt != nullptr) {
            diag.codegen_warning_label("format string in read() is not implemented yet and it is currently treated as '*'",
                {x.m_fmt->base.loc}, "treated as '*'");
        }
        if (x.m_unit != nullptr) {
            diag.codegen_error_label("unit in read() is not implemented yet",
                {x.m_unit->base.loc}, "not implemented");
            throw CodeGenAbort();
        }
        diag.codegen_error_label("The intrinsic function read() is not implemented yet in the LLVM backend",
            {x.base.base.loc}, "not implemented");
        throw CodeGenAbort();
    }

    void print_msg(std::string msg) {
        ASR::StringConstant_t n;
        n.m_s = new char[msg.length() + 1];
        strcpy(n.m_s, msg.c_str());
        visit_StringConstant(n);
        wasm::emit_i32_const(m_code_section, m_al, avail_mem_loc - last_str_len);
        wasm::emit_i32_const(m_code_section, m_al, last_str_len);
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["print_str"])]->index);
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["flush_buf"])]->index);
    }

    void exit() {
        // exit_code would be on stack, so add it to JavaScript Output buffer by printing it.
        // this exit code would be read by JavaScript glue code
        wasm::emit_call(m_code_section, m_al, m_func_name_idx_map[get_hash(m_import_func_asr_map["print_i32"])]->index);
        wasm::emit_unreachable(m_code_section, m_al); // raise trap/exception
    }

    void visit_Stop(const ASR::Stop_t &x) {
        print_msg("STOP");
        if (x.m_code && ASRUtils::expr_type(x.m_code)->type == ASR::ttypeType::Integer) {
            this->visit_expr(*x.m_code);
        } else {
            wasm::emit_i32_const(m_code_section, m_al, 0); // zero exit code
        }
        exit();
    }

    void visit_ErrorStop(const ASR::ErrorStop_t & /* x */) {
        print_msg("ERROR STOP");
        wasm::emit_i32_const(m_code_section, m_al, 1); // non-zero exit code
        exit();
    }

    void visit_If(const ASR::If_t &x) {
        this->visit_expr(*x.m_test);
        wasm::emit_b8(m_code_section, m_al, 0x04); // emit if start
        wasm::emit_b8(m_code_section, m_al, 0x40); // empty block type
        nesting_level++;
        for (size_t i=0; i<x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
        }
        if(x.n_orelse){
            wasm::emit_b8(m_code_section, m_al, 0x05); // starting of else
            for (size_t i=0; i<x.n_orelse; i++) {
                this->visit_stmt(*x.m_orelse[i]);
            }
        }
        nesting_level--;
        wasm::emit_expr_end(m_code_section, m_al); // emit if end
    }

    void visit_WhileLoop(const ASR::WhileLoop_t &x) {
        uint32_t prev_cur_loop_nesting_level = cur_loop_nesting_level;
        cur_loop_nesting_level = nesting_level;

        wasm::emit_b8(m_code_section, m_al, 0x03); // emit loop start
        wasm::emit_b8(m_code_section, m_al, 0x40); // empty block type

        nesting_level++;

        this->visit_expr(*x.m_test); // emit test condition

        wasm::emit_b8(m_code_section, m_al, 0x04); // emit if
        wasm::emit_b8(m_code_section, m_al, 0x40); // empty block type

        for (size_t i=0; i<x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
        }

        // From WebAssembly Docs:
        // Unlike with other index spaces, indexing of labels is relative by nesting depth,
        // that is, label 0 refers to the innermost structured control instruction enclosing
        // the referring branch instruction, while increasing indices refer to those farther out.

        wasm::emit_branch(m_code_section, m_al, nesting_level - cur_loop_nesting_level); // emit_branch and label the loop
        wasm::emit_expr_end(m_code_section, m_al); // end if

        nesting_level--;
        wasm::emit_expr_end(m_code_section, m_al); // end loop
        cur_loop_nesting_level = prev_cur_loop_nesting_level;
    }

    void visit_Exit(const ASR::Exit_t & /* x */) {
        wasm::emit_branch(m_code_section, m_al, nesting_level - cur_loop_nesting_level - 1U); // branch to end of if
    }

    void visit_Cycle(const ASR::Cycle_t & /* x */) {
        wasm::emit_branch(m_code_section, m_al, nesting_level - cur_loop_nesting_level); // branch to start of loop
    }
};

Result<Vec<uint8_t>> asr_to_wasm_bytes_stream(ASR::TranslationUnit_t &asr, Allocator &al, diag::Diagnostics &diagnostics) {
    ASRToWASMVisitor v(al, diagnostics);
    Vec<uint8_t> wasm_bytes;

    pass_unused_functions(al, asr, true);
    pass_wrap_global_stmts_into_function(al, asr, "f");
    pass_replace_implied_do_loops(al, asr, "f");
    pass_replace_do_loops(al, asr);

    try {
        v.visit_asr((ASR::asr_t &)asr);
    } catch (const CodeGenError &e) {
        diagnostics.diagnostics.push_back(e.d);
        return Error();
    }

    v.get_wasm(wasm_bytes);

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
