#include <iostream>
#include <memory>

#include <libasr/asr.h>
#include <libasr/containers.h>
#include <libasr/codegen/asr_to_c.h>
#include <libasr/codegen/asr_to_c_cpp.h>
#include <libasr/codegen/c_utils.h>
#include <libasr/exception.h>
#include <libasr/asr_utils.h>
#include <libasr/string_utils.h>
#include <libasr/pass/unused_functions.h>
#include <libasr/pass/class_constructor.h>
#include <libasr/pass/array_op.h>
#include <libasr/pass/subroutine_from_function.h>

#include <map>
#include <utility>


namespace LFortran {

class ASRToCVisitor : public BaseCCPPVisitor<ASRToCVisitor>
{
public:

    std::string array_types_decls;

    std::unique_ptr<CUtils::CUtilFunctions> c_utils_functions;

    int counter;

    ASRToCVisitor(diag::Diagnostics &diag, Platform &platform,
                  int64_t default_lower_bound)
         : BaseCCPPVisitor(diag, platform, false, false, true, default_lower_bound),
           array_types_decls(std::string("\nstruct dimension_descriptor\n"
                                         "{\n    int32_t lower_bound, length;\n};\n")),
           c_utils_functions{std::make_unique<CUtils::CUtilFunctions>()},
           counter{0} {
           }

    std::string convert_dims_c(size_t n_dims, ASR::dimension_t *m_dims,
                               ASR::ttype_t* element_type, bool& is_fixed_size,
                               bool convert_to_1d=false)
    {
        std::string dims = "";
        size_t size = 1;
        std::string array_size = "";
        for (size_t i=0; i<n_dims; i++) {
            ASR::expr_t *length = m_dims[i].m_length;
            if (!length) {
                is_fixed_size = false;
                return dims;
            } else {
                visit_expr(*length);
                array_size += "*" + src;
                ASR::expr_t* length_value = ASRUtils::expr_value(length);
                if( length_value ) {
                    int64_t length_int = -1;
                    ASRUtils::extract_value(length_value, length_int);
                    size *= length_int;
                    dims += "[" + std::to_string(length_int) + "]";
                } else {
                    size = 0;
                    dims += "[ /* FIXME symbolic dimensions */ ]";
                }
            }
        }
        if( size == 0 ) {
            std::string element_type_str = CUtils::get_c_type_from_ttype_t(element_type);
            dims = "(" + element_type_str + "*)" + " malloc(sizeof(" + element_type_str + ")" + array_size + ")";
            is_fixed_size = false;
            return dims;
        }
        if( convert_to_1d && size != 0 ) {
            dims = "[" + std::to_string(size) + "]";
        }
        return dims;
    }

    void generate_array_decl(std::string& sub, std::string v_m_name,
                             std::string& type_name, std::string& dims,
                             std::string& encoded_type_name,
                             ASR::dimension_t* m_dims, int n_dims,
                             bool use_ref, bool dummy,
                             bool declare_value, bool is_fixed_size,
                             bool is_pointer=false) {
        std::string indent(indentation_level*indentation_spaces, ' ');
        std::string type_name_copy = type_name;
        type_name = c_ds_api->get_array_type(type_name, encoded_type_name, array_types_decls);
        std::string type_name_without_ptr = c_ds_api->get_array_type(type_name, encoded_type_name, array_types_decls, false);
        if( declare_value ) {
            std::string variable_name = std::string(v_m_name) + "_value";
            sub = format_type_c("", type_name_without_ptr, variable_name, use_ref, dummy) + ";\n";
            sub += indent + format_type_c("", type_name, v_m_name, use_ref, dummy);
            sub += " = &" + variable_name + ";\n";
            if( !is_pointer ) {
                if( !is_fixed_size ) {
                    sub += indent + format_type_c("*", type_name_copy, std::string(v_m_name) + "_data",
                                                use_ref, dummy);
                    if( dims.size() > 0 ) {
                        sub += " = " + dims + ";\n";
                    } else {
                        sub += ";\n";
                    }
                } else {
                    sub += indent + format_type_c(dims, type_name_copy, std::string(v_m_name) + "_data",
                                                use_ref, dummy) + ";\n";
                }
                sub += indent + std::string(v_m_name) + "->data = " + std::string(v_m_name) + "_data;\n";
                sub += indent + std::string(v_m_name) + "->n_dims = " + std::to_string(n_dims) + ";\n";
                for (int i = 0; i < n_dims; i++) {
                    if( m_dims[i].m_start ) {
                        this->visit_expr(*m_dims[i].m_start);
                        sub += indent + std::string(v_m_name) +
                            "->dims[" + std::to_string(i) + "].lower_bound = " + src + ";\n";
                    } else {
                        sub += indent + std::string(v_m_name) +
                            "->dims[" + std::to_string(i) + "].lower_bound = 0" + ";\n";
                    }
                    if( m_dims[i].m_length ) {
                        this->visit_expr(*m_dims[i].m_length);
                        sub += indent + std::string(v_m_name) +
                            "->dims[" + std::to_string(i) + "].length = " + src + ";\n";
                    } else {
                        sub += indent + std::string(v_m_name) +
                            "->dims[" + std::to_string(i) + "].length = 0" + ";\n";
                    }
                }
                sub.pop_back();
                sub.pop_back();
            }
        } else {
            sub = format_type_c("", type_name, v_m_name, use_ref, dummy);
        }
    }

    void allocate_array_members_of_struct(ASR::StructType_t* der_type_t, std::string& sub,
        std::string indent, std::string name) {
        for( auto itr: der_type_t->m_symtab->get_scope() ) {
            ASR::ttype_t* mem_type = ASRUtils::symbol_type(itr.second);
            if( ASRUtils::is_character(*mem_type) ) {
                sub += indent + name + "->" + itr.first + " = (char*) malloc(40 * sizeof(char));\n";
            } else if( ASRUtils::is_array(mem_type) &&
                        ASR::is_a<ASR::Variable_t>(*itr.second) ) {
                ASR::Variable_t* mem_var = ASR::down_cast<ASR::Variable_t>(itr.second);
                std::string mem_var_name = current_scope->get_unique_name(itr.first + std::to_string(counter));
                counter += 1;
                sub += indent + convert_variable_decl(*mem_var, true, true, true, true, mem_var_name) + ";\n";
                if( mem_var->m_abi == ASR::abiType::BindC &&
                    ASR::is_a<ASR::Integer_t>(*mem_var->m_type) &&
                    ASRUtils::extract_kind_from_ttype_t(mem_var->m_type) == 1 ) {
                    sub += indent + "strcpy(" + name + "->" + itr.first + ", " + mem_var_name + ");\n";
                } else {
                    sub += indent + name + "->" + itr.first + " = " + mem_var_name + ";\n";
                }
            } else if( ASR::is_a<ASR::Struct_t>(*mem_type) ) {
                ASR::Struct_t* struct_t = ASR::down_cast<ASR::Struct_t>(mem_type);
                ASR::StructType_t* struct_type_t = ASR::down_cast<ASR::StructType_t>(
                    ASRUtils::symbol_get_past_external(struct_t->m_derived_type));
                allocate_array_members_of_struct(struct_type_t, sub, indent, "(&(" + name + "->" + itr.first + "))");
            }
        }
    }

    std::string convert_variable_decl(const ASR::Variable_t &v,
                                      bool pre_initialise_derived_type=true,
                                      bool use_ptr_for_derived_type=true,
                                      bool use_static=true,
                                      bool force_declare=false,
                                      std::string force_declare_name="")
    {
        std::string sub;
        bool use_ref = (v.m_intent == LFortran::ASRUtils::intent_out ||
                        v.m_intent == LFortran::ASRUtils::intent_inout ||
                        v.m_intent == LFortran::ASRUtils::intent_unspecified
                        );
        bool is_array = ASRUtils::is_array(v.m_type);
        bool dummy = LFortran::ASRUtils::is_arg_dummy(v.m_intent);
        ASR::ttype_t* v_m_type = v.m_type;
        if (ASR::is_a<ASR::Const_t>(*v_m_type)) {
            if( is_array ) {
                v_m_type = ASR::down_cast<ASR::Const_t>(v_m_type)->m_type;
            }
        }
        if (ASRUtils::is_pointer(v_m_type)) {
            ASR::ttype_t *t2 = ASR::down_cast<ASR::Pointer_t>(v_m_type)->m_type;
            if (ASRUtils::is_integer(*t2)) {
                ASR::Integer_t *t = ASR::down_cast<ASR::Integer_t>(t2);
                std::string type_name = "int" + std::to_string(t->m_kind * 8) + "_t";
                if( !ASRUtils::is_array(v_m_type) ) {
                    type_name.append(" *");
                }
                if( is_array ) {
                    bool is_fixed_size = true;
                    std::string dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size, true);
                    std::string encoded_type_name = "i" + std::to_string(t->m_kind * 8);
                    generate_array_decl(sub, std::string(v.m_name), type_name, dims,
                                        encoded_type_name, t->m_dims, t->n_dims,
                                        use_ref, dummy,
                                        v.m_intent != ASRUtils::intent_in &&
                                        v.m_intent != ASRUtils::intent_inout &&
                                        v.m_intent != ASRUtils::intent_out, is_fixed_size, true);
                } else {
                    bool is_fixed_size = true;
                    std::string dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                    sub = format_type_c(dims, type_name, v.m_name, use_ref, dummy);
                }
            } else if(ASR::is_a<ASR::Struct_t>(*t2)) {
                ASR::Struct_t *t = ASR::down_cast<ASR::Struct_t>(t2);
                std::string der_type_name = ASRUtils::symbol_name(t->m_derived_type);
                if( is_array ) {
                    bool is_fixed_size = true;
                    std::string dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size, true);
                    std::string encoded_type_name = "x" + der_type_name;
                    std::string type_name = std::string("struct ") + der_type_name;
                    generate_array_decl(sub, std::string(v.m_name), type_name, dims,
                                        encoded_type_name, t->m_dims, t->n_dims,
                                        use_ref, dummy,
                                        v.m_intent != ASRUtils::intent_in &&
                                        v.m_intent != ASRUtils::intent_inout,
                                        is_fixed_size);
                 } else {
                    std::string ptr_char = "*";
                    if( !use_ptr_for_derived_type ) {
                        ptr_char.clear();
                    }
                    sub = format_type_c("", "struct " + der_type_name + ptr_char,
                                        v.m_name, use_ref, dummy);
                }
            } else if(ASR::is_a<ASR::CPtr_t>(*t2)) {
                sub = format_type_c("", "void**", v.m_name, false, false);
            } else {
                diag.codegen_error_label("Type number '"
                    + std::to_string(v_m_type->type)
                    + "' not supported", {v.base.base.loc}, "");
                throw Abort();
            }
        } else {
            std::string dims;
            use_ref = use_ref && !is_array;
            if (ASRUtils::is_integer(*v_m_type)) {
                headers.insert("inttypes");
                ASR::Integer_t *t = ASR::down_cast<ASR::Integer_t>(v_m_type);
                std::string type_name = "int" + std::to_string(t->m_kind * 8) + "_t";
                if( is_array ) {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size, true);
                    if( t->m_kind == 1 && v.m_abi == ASR::abiType::BindC && is_fixed_size ) {
                        if( !force_declare ) {
                            force_declare_name = std::string(v.m_name);
                        }
                        sub = "char " + force_declare_name + dims;
                    } else {
                        std::string encoded_type_name = "i" + std::to_string(t->m_kind * 8);
                        bool is_struct_type_member = ASR::is_a<ASR::StructType_t>(
                            *ASR::down_cast<ASR::symbol_t>(v.m_parent_symtab->asr_owner));
                        if( !force_declare ) {
                            force_declare_name = std::string(v.m_name);
                        }
                        generate_array_decl(sub, force_declare_name, type_name, dims,
                                            encoded_type_name, t->m_dims, t->n_dims,
                                            use_ref, dummy,
                                            (v.m_intent != ASRUtils::intent_in &&
                                            v.m_intent != ASRUtils::intent_inout &&
                                            v.m_intent != ASRUtils::intent_out &&
                                            !is_struct_type_member) || force_declare, is_fixed_size);
                    }
                } else {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                    sub = format_type_c(dims, type_name, v.m_name, use_ref, dummy);
                }
            } else if (ASRUtils::is_real(*v_m_type)) {
                ASR::Real_t *t = ASR::down_cast<ASR::Real_t>(v_m_type);
                std::string type_name = "float";
                if (t->m_kind == 8) type_name = "double";
                if( is_array ) {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size, true);
                    std::string encoded_type_name = "r" + std::to_string(t->m_kind * 8);
                    bool is_struct_type_member = ASR::is_a<ASR::StructType_t>(
                         *ASR::down_cast<ASR::symbol_t>(v.m_parent_symtab->asr_owner));
                    if( !force_declare ) {
                        force_declare_name = std::string(v.m_name);
                    }
                    generate_array_decl(sub, force_declare_name, type_name, dims,
                                        encoded_type_name, t->m_dims, t->n_dims,
                                        use_ref, dummy,
                                        (v.m_intent != ASRUtils::intent_in &&
                                        v.m_intent != ASRUtils::intent_inout &&
                                        v.m_intent != ASRUtils::intent_out &&
                                        !is_struct_type_member) || force_declare, is_fixed_size);
                } else {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                    sub = format_type_c(dims, type_name, v.m_name, use_ref, dummy);
                }
            } else if (ASRUtils::is_complex(*v_m_type)) {
                headers.insert("complex");
                ASR::Complex_t *t = ASR::down_cast<ASR::Complex_t>(v_m_type);
                std::string type_name = "float complex";
                if (t->m_kind == 8) type_name = "double complex";
                if( is_array ) {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size, true);
                    std::string encoded_type_name = "c" + std::to_string(t->m_kind * 8);
                    generate_array_decl(sub, std::string(v.m_name), type_name, dims,
                                        encoded_type_name, t->m_dims, t->n_dims,
                                        use_ref, dummy,
                                        v.m_intent != ASRUtils::intent_in &&
                                        v.m_intent != ASRUtils::intent_inout,
                                        is_fixed_size);
                } else {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                    sub = format_type_c(dims, type_name, v.m_name, use_ref, dummy);
                }
            } else if (ASRUtils::is_logical(*v_m_type)) {
                ASR::Logical_t *t = ASR::down_cast<ASR::Logical_t>(v_m_type);
                bool is_fixed_size = true;
                dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                sub = format_type_c(dims, "bool", v.m_name, use_ref, dummy);
            } else if (ASRUtils::is_character(*v_m_type)) {
                ASR::Character_t *t = ASR::down_cast<ASR::Character_t>(v_m_type);
                bool is_fixed_size = true;
                std::string dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                sub = format_type_c(dims, "char *", v.m_name, use_ref, dummy);
                if( v.m_intent == ASRUtils::intent_local &&
                    !(ASR::is_a<ASR::symbol_t>(*v.m_parent_symtab->asr_owner) &&
                      ASR::is_a<ASR::StructType_t>(
                        *ASR::down_cast<ASR::symbol_t>(v.m_parent_symtab->asr_owner))) ) {
                    sub += " = (char*) malloc(40 * sizeof(char))";
                    return sub;
                }
            } else if (ASR::is_a<ASR::Struct_t>(*v_m_type)) {
                std::string indent(indentation_level*indentation_spaces, ' ');
                ASR::Struct_t *t = ASR::down_cast<ASR::Struct_t>(v_m_type);
                std::string der_type_name = ASRUtils::symbol_name(t->m_derived_type);
                 if( is_array ) {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size, true);
                    std::string encoded_type_name = "x" + der_type_name;
                    std::string type_name = std::string("struct ") + der_type_name;
                    generate_array_decl(sub, std::string(v.m_name), type_name, dims,
                                        encoded_type_name, t->m_dims, t->n_dims,
                                        use_ref, dummy,
                                        v.m_intent != ASRUtils::intent_in &&
                                        v.m_intent != ASRUtils::intent_inout,
                                        is_fixed_size);
                } else if( v.m_intent == ASRUtils::intent_local && pre_initialise_derived_type) {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                    std::string value_var_name = v.m_parent_symtab->get_unique_name(std::string(v.m_name) + "_value");
                    sub = format_type_c(dims, "struct " + der_type_name,
                                        value_var_name, use_ref, dummy);
                    if (v.m_symbolic_value) {
                        this->visit_expr(*v.m_symbolic_value);
                        std::string init = src;
                        sub += "=" + init;
                    }
                    sub += ";\n";
                    std::string ptr_char = "*";
                    if( !use_ptr_for_derived_type ) {
                        ptr_char.clear();
                    }
                    sub += indent + format_type_c("", "struct " + der_type_name + ptr_char, v.m_name, use_ref, dummy);
                    if( t->n_dims != 0 ) {
                        sub += " = " + value_var_name;
                    } else {
                        sub += " = &" + value_var_name + ";\n";
                        ASR::StructType_t* der_type_t = ASR::down_cast<ASR::StructType_t>(
                        ASRUtils::symbol_get_past_external(t->m_derived_type));
                        allocate_array_members_of_struct(der_type_t, sub, indent, std::string(v.m_name));
                        sub.pop_back();
                        sub.pop_back();
                    }
                    return sub;
                } else {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                    if( v.m_intent == ASRUtils::intent_in ||
                        v.m_intent == ASRUtils::intent_inout ||
                        v.m_intent == ASRUtils::intent_out ) {
                        use_ref = false;
                        dims = "";
                    }
                    std::string ptr_char = "*";
                    if( !use_ptr_for_derived_type ) {
                        ptr_char.clear();
                    }
                    sub = format_type_c(dims, "struct " + der_type_name + ptr_char,
                                        v.m_name, use_ref, dummy);
                }
            } else if (ASR::is_a<ASR::Union_t>(*v_m_type)) {
                std::string indent(indentation_level*indentation_spaces, ' ');
                ASR::Union_t *t = ASR::down_cast<ASR::Union_t>(v_m_type);
                std::string der_type_name = ASRUtils::symbol_name(t->m_union_type);
                if( is_array ) {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size, true);
                    std::string encoded_type_name = "x" + der_type_name;
                    std::string type_name = std::string("union ") + der_type_name;
                    generate_array_decl(sub, std::string(v.m_name), type_name, dims,
                                        encoded_type_name, t->m_dims, t->n_dims,
                                        use_ref, dummy,
                                        v.m_intent != ASRUtils::intent_in &&
                                        v.m_intent != ASRUtils::intent_inout, is_fixed_size);
                } else {
                    bool is_fixed_size = true;
                    dims = convert_dims_c(t->n_dims, t->m_dims, v_m_type, is_fixed_size);
                    if( v.m_intent == ASRUtils::intent_in ||
                        v.m_intent == ASRUtils::intent_inout ) {
                        use_ref = false;
                        dims = "";
                    }
                    sub = format_type_c(dims, "union " + der_type_name,
                                        v.m_name, use_ref, dummy);
                }
            } else if (ASR::is_a<ASR::List_t>(*v_m_type)) {
                ASR::List_t* t = ASR::down_cast<ASR::List_t>(v_m_type);
                std::string list_type_c = c_ds_api->get_list_type(t);
                sub = format_type_c("", list_type_c, v.m_name,
                                    false, false);
            } else if (ASR::is_a<ASR::Tuple_t>(*v_m_type)) {
                ASR::Tuple_t* t = ASR::down_cast<ASR::Tuple_t>(v_m_type);
                std::string tuple_type_c = c_ds_api->get_tuple_type(t);
                sub = format_type_c("", tuple_type_c, v.m_name,
                                    false, false);
            } else if (ASR::is_a<ASR::CPtr_t>(*v_m_type)) {
                sub = format_type_c("", "void*", v.m_name, false, false);
            } else if (ASR::is_a<ASR::Enum_t>(*v_m_type)) {
                ASR::Enum_t* enum_ = ASR::down_cast<ASR::Enum_t>(v_m_type);
                ASR::EnumType_t* enum_type = ASR::down_cast<ASR::EnumType_t>(enum_->m_enum_type);
                sub = format_type_c("", "enum " + std::string(enum_type->m_name), v.m_name, false, false);
            } else if (ASR::is_a<ASR::Const_t>(*v_m_type)) {
                if( v.m_intent == ASRUtils::intent_local ) {
                    LFORTRAN_ASSERT(v.m_symbolic_value);
                    visit_expr(*v.m_symbolic_value);
                    sub = "#define " + std::string(v.m_name) + " " + src + "\n";
                    return sub;
                } else {
                    std::string const_underlying_type = CUtils::get_c_type_from_ttype_t(
                        ASR::down_cast<ASR::Const_t>(v_m_type)->m_type);
                    sub = format_type_c("", "const " + const_underlying_type + " ",
                                        v.m_name, false, false);
                }
            } else {
                diag.codegen_error_label("Type number '"
                    + std::to_string(v_m_type->type)
                    + "' not supported", {v.base.base.loc}, "");
                throw Abort();
            }
            if (dims.size() == 0 && v.m_storage == ASR::storage_typeType::Save && use_static) {
                sub = "static " + sub;
            }
            if (dims.size() == 0 && v.m_symbolic_value) {
                this->visit_expr(*v.m_symbolic_value);
                std::string init = src;
                sub += "=" + init;
            }
        }
        return sub;
    }


    void visit_TranslationUnit(const ASR::TranslationUnit_t &x) {
        is_string_concat_present = false;
        global_scope = x.m_global_scope;
        // All loose statements must be converted to a function, so the items
        // must be empty:
        LFORTRAN_ASSERT(x.n_items == 0);
        std::string unit_src = "";
        indentation_level = 0;
        indentation_spaces = 4;
        c_ds_api->set_indentation(indentation_level, indentation_spaces);
        c_ds_api->set_global_scope(global_scope);
        c_utils_functions->set_indentation(indentation_level, indentation_spaces);
        c_utils_functions->set_global_scope(global_scope);
        c_ds_api->set_c_utils_functions(c_utils_functions.get());

        std::string head =
R"(
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <lfortran_intrinsics.h>

#define ASSERT(cond)                                                           \
    {                                                                          \
        if (!(cond)) {                                                         \
            printf("%s%s", "ASSERT failed: ", __FILE__);                       \
            printf("%s%s", "\nfunction ", __func__);                           \
            printf("%s%d%s", "(), line number ", __LINE__, " at \n");          \
            printf("%s%s", #cond, "\n");                                       \
            exit(1);                                                           \
        }                                                                      \
    }
#define ASSERT_MSG(cond, msg)                                                  \
    {                                                                          \
        if (!(cond)) {                                                         \
            printf("%s%s", "ASSERT failed: ", __FILE__);                       \
            printf("%s%s", "\nfunction ", __func__);                           \
            printf("%s%d%s", "(), line number ", __LINE__, " at \n");          \
            printf("%s%s", #cond, "\n");                                       \
            printf("%s", "ERROR MESSAGE:\n");                                  \
            printf("%s%s", msg, "\n");                                         \
            exit(1);                                                           \
        }                                                                      \
    }

)";

        std::string indent(indentation_level * indentation_spaces, ' ');
        std::string tab(indentation_spaces, ' ');
        std::string strcat_def = "";
        strcat_def += indent + "char* " + global_scope->get_unique_name("strcat_") + "(char* x, char* y) {\n";
        strcat_def += indent + tab + "char* str_tmp = (char*) malloc((strlen(x) + strlen(y) + 2) * sizeof(char));\n";
        strcat_def += indent + tab + "strcpy(str_tmp, x);\n";
        strcat_def += indent + tab + "return strcat(str_tmp, y);\n";
        strcat_def += indent + "}\n\n";

        for (auto &item : x.m_global_scope->get_scope()) {
            if (ASR::is_a<ASR::Variable_t>(*item.second)) {
                ASR::Variable_t *v = ASR::down_cast<ASR::Variable_t>(item.second);
                unit_src += convert_variable_decl(*v);
                if( !ASR::is_a<ASR::Const_t>(*v->m_type) ||
                    v->m_intent == ASRUtils::intent_return_var ) {
                    unit_src += ";\n";
                }
            }
        }


        std::map<std::string, std::vector<std::string>> struct_dep_graph;
        for (auto &item : x.m_global_scope->get_scope()) {
            if (ASR::is_a<ASR::StructType_t>(*item.second) ||
                ASR::is_a<ASR::EnumType_t>(*item.second) ||
                ASR::is_a<ASR::UnionType_t>(*item.second)) {
                std::vector<std::string> struct_deps_vec;
                std::pair<char**, size_t> struct_deps_ptr = ASRUtils::symbol_dependencies(item.second);
                for( size_t i = 0; i < struct_deps_ptr.second; i++ ) {
                    struct_deps_vec.push_back(std::string(struct_deps_ptr.first[i]));
                }
                struct_dep_graph[item.first] = struct_deps_vec;
            }
        }

        std::vector<std::string> struct_deps = ASRUtils::order_deps(struct_dep_graph);

        for (auto &item : struct_deps) {
            ASR::symbol_t* struct_sym = x.m_global_scope->get_symbol(item);
            visit_symbol(*struct_sym);
            array_types_decls += src;
        }

        // Topologically sort all global functions
        // and then define them in the right order
        std::vector<std::string> global_func_order = ASRUtils::determine_function_definition_order(x.m_global_scope);

        unit_src += "\n";
        unit_src += "// Implementations\n";

        {
            // Process intrinsic modules in the right order
            std::vector<std::string> build_order
                = LFortran::ASRUtils::determine_module_dependencies(x);
            for (auto &item : build_order) {
                LFORTRAN_ASSERT(x.m_global_scope->get_scope().find(item)
                    != x.m_global_scope->get_scope().end());
                if (startswith(item, "lfortran_intrinsic")) {
                    ASR::symbol_t *mod = x.m_global_scope->get_symbol(item);
                    if( ASRUtils::get_body_size(mod) != 0 ) {
                        visit_symbol(*mod);
                        unit_src += src;
                    }
                }
            }
        }

        // Process modules in the right order
        std::vector<std::string> build_order
            = LFortran::ASRUtils::determine_module_dependencies(x);
        for (auto &item : build_order) {
            LFORTRAN_ASSERT(x.m_global_scope->get_scope().find(item)
                != x.m_global_scope->get_scope().end());
            if (!startswith(item, "lfortran_intrinsic")) {
                ASR::symbol_t *mod = x.m_global_scope->get_symbol(item);
                visit_symbol(*mod);
                unit_src += src;
            }
        }

        // Process global functions
        size_t i;
        for (i = 0; i < global_func_order.size(); i++) {
            ASR::symbol_t* sym = x.m_global_scope->get_symbol(global_func_order[i]);
            // Ignore external symbols because they are already defined by the loop above.
            if( !sym || ASR::is_a<ASR::ExternalSymbol_t>(*sym) ) {
                continue ;
            }
            visit_symbol(*sym);
            unit_src += src;
        }

        // Then the main program:
        for (auto &item : x.m_global_scope->get_scope()) {
            if (ASR::is_a<ASR::Program_t>(*item.second)) {
                visit_symbol(*item.second);
                unit_src += src;
            }
        }
        std::string to_include = "";
        for (auto s: headers) {
            to_include += "#include <" + s + ".h>\n";
        }
        if( c_ds_api->get_func_decls().size() > 0 ) {
            array_types_decls += "\n" + c_ds_api->get_func_decls() + "\n";
        }
        if( c_utils_functions->get_util_func_decls().size() > 0 ) {
            array_types_decls += "\n" + c_utils_functions->get_util_func_decls() + "\n";
        }
        std::string ds_funcs_defined = "";
        if( c_ds_api->get_generated_code().size() > 0 ) {
            ds_funcs_defined =  "\n" + c_ds_api->get_generated_code() + "\n";
        }
        std::string util_funcs_defined = "";
        if( c_utils_functions->get_generated_code().size() > 0 ) {
            util_funcs_defined =  "\n" + c_utils_functions->get_generated_code() + "\n";
        }
        if( is_string_concat_present ) {
            head += strcat_def;
        }
        src = to_include + head + array_types_decls + unit_src +
              ds_funcs_defined + util_funcs_defined;
    }

    void visit_Program(const ASR::Program_t &x) {
        // Topologically sort all program functions
        // and then define them in the right order
        std::vector<std::string> func_order = ASRUtils::determine_function_definition_order(x.m_symtab);

        // Generate code for nested subroutines and functions first:
        std::string contains;
        for (auto &item : func_order) {
            ASR::symbol_t* sym = x.m_symtab->get_symbol(item);
            ASR::Function_t *s = ASR::down_cast<ASR::Function_t>(sym);
            visit_Function(*s);
            contains += src;
        }

        // Generate code for the main program
        indentation_level += 1;
        std::string indent1(indentation_level*indentation_spaces, ' ');
        std::string decl;
        for (auto &item : x.m_symtab->get_scope()) {
            if (ASR::is_a<ASR::Variable_t>(*item.second)) {
                ASR::Variable_t *v = ASR::down_cast<ASR::Variable_t>(item.second);
                decl += indent1;
                decl += convert_variable_decl(*v);
                if( !ASR::is_a<ASR::Const_t>(*v->m_type) ||
                    v->m_intent == ASRUtils::intent_return_var ) {
                    decl += ";\n";
                }
            }
        }

        std::string body;
        for (size_t i=0; i<x.n_body; i++) {
            this->visit_stmt(*x.m_body[i]);
            body += src;
        }
        src = contains
                + "int main(int argc, char* argv[])\n{\n"
                + decl + body
                + indent1 + "return 0;\n}\n";
        indentation_level -= 2;
    }

    template <typename T>
    void visit_AggregateTypeUtil(const T& x, std::string c_type_name) {
        std::string indent(indentation_level*indentation_spaces, ' ');
        indentation_level += 1;
        std::string open_struct = indent + c_type_name + " " + std::string(x.m_name) + " {\n";
        std::string body = "";
        indent.push_back(' ');
        for( size_t i = 0; i < x.n_members; i++ ) {
            ASR::symbol_t* member = x.m_symtab->get_symbol(x.m_members[i]);
            LFORTRAN_ASSERT(ASR::is_a<ASR::Variable_t>(*member));
            body += indent + convert_variable_decl(
                        *ASR::down_cast<ASR::Variable_t>(member),
                        false, false);
            if( !ASR::is_a<ASR::Const_t>(*ASRUtils::symbol_type(member)) ||
                ASR::down_cast<ASR::Variable_t>(member)->m_intent == ASRUtils::intent_return_var ) {
                body += ";\n";
            }
        }
        indentation_level -= 1;
        std::string end_struct = "};\n\n";
        array_types_decls += open_struct + body + end_struct;
    }

    void visit_StructType(const ASR::StructType_t& x) {
        src = "";
        std::string c_type_name = "struct";
        if( x.m_is_packed ) {
            std::string attr_args = "(packed";
            if( x.m_alignment ) {
                LFORTRAN_ASSERT(ASRUtils::expr_value(x.m_alignment));
                ASR::expr_t* alignment_value = ASRUtils::expr_value(x.m_alignment);
                int64_t alignment_int = -1;
                if( !ASRUtils::extract_value(alignment_value, alignment_int) ) {
                    LFORTRAN_ASSERT(false);
                }
                attr_args += ", aligned(" + std::to_string(alignment_int) + ")";
            }
            attr_args += ")";
            c_type_name += " __attribute__(" + attr_args + ")";
        }
        visit_AggregateTypeUtil(x, c_type_name);
        src = "";
    }

    void visit_UnionType(const ASR::UnionType_t& x) {
        visit_AggregateTypeUtil(x, "union");
    }

    void visit_EnumType(const ASR::EnumType_t& x) {
        if( x.m_enum_value_type == ASR::enumtypeType::NonInteger ) {
            throw CodeGenError("C backend only supports integer valued Enum. " +
                std::string(x.m_name) + " is not integer valued.");
        }
        if( x.m_enum_value_type == ASR::enumtypeType::IntegerNotUnique ) {
            throw CodeGenError("C backend only supports uniquely valued integer Enum. " +
                std::string(x.m_name) + " Enum is having duplicate values for its members.");
        }
        if( x.m_enum_value_type == ASR::enumtypeType::IntegerUnique &&
            x.m_abi == ASR::abiType::BindC ) {
            throw CodeGenError("C-interoperation support for non-consecutive but uniquely "
                               "valued integer enums isn't available yet.");
        }
        std::string indent(indentation_level*indentation_spaces, ' ');
        std::string tab(indentation_spaces, ' ');
        std::string meta_data = " = {";
        std::string open_struct = indent + "enum " + std::string(x.m_name) + " {\n";
        std::string body = "";
        int64_t min_value = INT64_MAX;
        int64_t max_value = INT64_MIN;
        size_t max_name_len = 0;
        for( size_t i = 0; i < x.n_members; i++ ) {
            ASR::symbol_t* member = x.m_symtab->get_symbol(x.m_members[i]);
            LFORTRAN_ASSERT(ASR::is_a<ASR::Variable_t>(*member));
            ASR::Variable_t* member_var = ASR::down_cast<ASR::Variable_t>(member);
            ASR::expr_t* value = ASRUtils::expr_value(member_var->m_symbolic_value);
            int64_t value_int64 = -1;
            ASRUtils::extract_value(value, value_int64);
            min_value = std::min(value_int64, min_value);
            max_value = std::max(value_int64, max_value);
            max_name_len = std::max(max_name_len, std::string(x.m_members[i]).size());
            this->visit_expr(*member_var->m_symbolic_value);
            body += indent + tab + std::string(member_var->m_name) + " = " + src + ",\n";
        }
        size_t max_names = max_value - min_value + 1;
        std::vector<std::string> enum_names(max_names, "\"\"");
        for( size_t i = 0; i < x.n_members; i++ ) {
            ASR::symbol_t* member = x.m_symtab->get_symbol(x.m_members[i]);
            LFORTRAN_ASSERT(ASR::is_a<ASR::Variable_t>(*member));
            ASR::Variable_t* member_var = ASR::down_cast<ASR::Variable_t>(member);
            ASR::expr_t* value = ASRUtils::expr_value(member_var->m_symbolic_value);
            int64_t value_int64 = -1;
            ASRUtils::extract_value(value, value_int64);
            min_value = std::min(value_int64, min_value);
            enum_names[value_int64 - min_value] = "\"" + std::string(member_var->m_name) + "\"";
        }
        for( auto enum_name: enum_names ) {
            meta_data += enum_name + ", ";
        }
        meta_data.pop_back();
        meta_data.pop_back();
        meta_data += "};\n";
        std::string end_struct = "};\n\n";
        std::string enum_names_type = "char " + global_scope->get_unique_name("enum_names_") +
            std::string(x.m_name) + "[" + std::to_string(max_names) + "][" + std::to_string(max_name_len + 1) + "] ";
        array_types_decls += enum_names_type + meta_data + open_struct + body + end_struct;
        src = "";
    }

    void visit_EnumTypeConstructor(const ASR::EnumTypeConstructor_t& x) {
        LFORTRAN_ASSERT(x.n_args == 1);
        ASR::expr_t* m_arg = x.m_args[0];
        this->visit_expr(*m_arg);
        ASR::EnumType_t* enum_type = ASR::down_cast<ASR::EnumType_t>(x.m_dt_sym);
        src = "(enum " + std::string(enum_type->m_name) + ") (" + src + ")";
    }

    void visit_UnionTypeConstructor(const ASR::UnionTypeConstructor_t& /*x*/) {

    }

    void visit_EnumMember(const ASR::EnumMember_t& x) {
        ASR::Variable_t* enum_var = ASR::down_cast<ASR::Variable_t>(x.m_m);
        src = std::string(enum_var->m_name);
    }

    void visit_EnumValue(const ASR::EnumValue_t& x) {
        visit_expr(*x.m_v);
    }

    void visit_EnumName(const ASR::EnumName_t& x) {
        int64_t min_value = INT64_MAX;
        ASR::Enum_t* enum_t = ASR::down_cast<ASR::Enum_t>(x.m_enum_type);
        ASR::EnumType_t* enum_type = ASR::down_cast<ASR::EnumType_t>(enum_t->m_enum_type);
        for( auto itr: enum_type->m_symtab->get_scope() ) {
            ASR::Variable_t* itr_var = ASR::down_cast<ASR::Variable_t>(itr.second);
            ASR::expr_t* value = ASRUtils::expr_value(itr_var->m_symbolic_value);
            int64_t value_int64 = -1;
            ASRUtils::extract_value(value, value_int64);
            min_value = std::min(value_int64, min_value);
        }
        visit_expr(*x.m_v);
        std::string enum_var_name = src;
        src = global_scope->get_unique_name("enum_names_" + std::string(enum_type->m_name)) +
                "[" + std::string(enum_var_name) + " - " + std::to_string(min_value) + "]";
    }

    void visit_ComplexConstant(const ASR::ComplexConstant_t &x) {
        headers.insert("complex");
        std::string re = std::to_string(x.m_re);
        std::string im = std::to_string(x.m_im);
        src = "CMPLX(" + re + ", " + im + ")";

        last_expr_precedence = 2;
    }

    void visit_LogicalConstant(const ASR::LogicalConstant_t &x) {
        if (x.m_value == true) {
            src = "true";
        } else {
            src = "false";
        }
        last_expr_precedence = 2;
    }

    void visit_Assert(const ASR::Assert_t &x) {
        std::string indent(indentation_level*indentation_spaces, ' ');
        std::string out = indent;
        if (x.m_msg) {
            out += "ASSERT_MSG(";
            visit_expr(*x.m_test);
            out += src + ", ";
            visit_expr(*x.m_msg);
            out += src + ");\n";
        } else {
            out += "ASSERT(";
            visit_expr(*x.m_test);
            out += src + ");\n";
        }
        src = out;
    }

    std::string get_print_type(ASR::ttype_t *t, bool deref_ptr) {
        switch (t->type) {
            case ASR::ttypeType::Integer: {
                ASR::Integer_t *i = (ASR::Integer_t*)t;
                switch (i->m_kind) {
                    case 1: { return "%d"; }
                    case 2: { return "%d"; }
                    case 4: { return "%d"; }
                    case 8: {
                        if (platform == Platform::Linux) {
                            return "%li";
                        } else {
                            return "%lli";
                        }
                    }
                    default: { throw LCompilersException("Integer kind not supported"); }
                }
            }
            case ASR::ttypeType::Real: {
                ASR::Real_t *r = (ASR::Real_t*)t;
                switch (r->m_kind) {
                    case 4: { return "%f"; }
                    case 8: { return "%lf"; }
                    default: { throw LCompilersException("Float kind not supported"); }
                }
            }
            case ASR::ttypeType::Logical: {
                return "%d";
            }
            case ASR::ttypeType::Character: {
                return "%s";
            }
            case ASR::ttypeType::CPtr: {
                return "%p";
            }
            case ASR::ttypeType::Complex: {
                return "(%f, %f)";
            }
            case ASR::ttypeType::Pointer: {
                if( !deref_ptr ) {
                    return "%p";
                } else {
                    ASR::Pointer_t* type_ptr = ASR::down_cast<ASR::Pointer_t>(t);
                    return get_print_type(type_ptr->m_type, false);
                }
            }
            case ASR::ttypeType::Enum: {
                ASR::ttype_t* enum_underlying_type = ASRUtils::get_contained_type(t);
                return get_print_type(enum_underlying_type, deref_ptr);
            }
            case ASR::ttypeType::Const: {
                ASR::ttype_t* const_underlying_type = ASRUtils::get_contained_type(t);
                return get_print_type(const_underlying_type, deref_ptr);
            }
            default : throw LCompilersException("Not implemented");
        }
    }

    void visit_Print(const ASR::Print_t &x) {
        std::string indent(indentation_level*indentation_spaces, ' ');
        std::string out = indent + "printf(\"";
        std::vector<std::string> v;
        std::string separator;
        if (x.m_separator) {
            this->visit_expr(*x.m_separator);
            separator = src;
        } else {
            separator = "\" \"";
        }
        for (size_t i=0; i<x.n_values; i++) {
            this->visit_expr(*x.m_values[i]);
            if( ASRUtils::is_array(ASRUtils::expr_type(x.m_values[i])) ) {
                src += "->data";
            }
            ASR::ttype_t* value_type = ASRUtils::expr_type(x.m_values[i]);
            out += get_print_type(value_type, ASR::is_a<ASR::ArrayItem_t>(*x.m_values[i]));
            v.push_back(src);
            if (value_type->type == ASR::ttypeType::Complex) {
                v.pop_back();
                v.push_back("creal(" + src + ")");
                v.push_back("cimag(" + src + ")");
            }
            if (i+1!=x.n_values) {
                out += "\%s";
                v.push_back(separator);
            }
        }
        if (x.m_end) {
            this->visit_expr(*x.m_end);
            out += "\%s\"";
            v.push_back(src);
        } else {
            out += "\\n\"";
        }
        if (!v.empty()) {
            for (auto s: v) {
                out += ", " + s;
            }
        }
        out += ");\n";
        src = out;
    }

    void visit_ArraySize(const ASR::ArraySize_t& x) {
        visit_expr(*x.m_v);
        std::string var_name = src;
        std::string args = "";
        std::string result_type = CUtils::get_c_type_from_ttype_t(x.m_type);
        if (x.m_dim == nullptr) {
            std::string array_size_func = c_utils_functions->get_array_size();
            ASR::dimension_t* m_dims = nullptr;
            int n_dims = ASRUtils::extract_dimensions_from_ttype(ASRUtils::expr_type(x.m_v), m_dims);
            src = "((" + result_type + ") " + array_size_func + "(" + var_name + "->dims, " + std::to_string(n_dims) + "))";
        } else {
            visit_expr(*x.m_dim);
            std::string idx = src;
            src = "((" + result_type + ")" + var_name + "->dims[" + idx + "-1].length)";
        }
    }

    void visit_ArrayReshape(const ASR::ArrayReshape_t& x) {
        visit_expr(*x.m_array);
        std::string array = src;
        visit_expr(*x.m_shape);
        std::string shape = src;

        ASR::ttype_t* array_type_asr = ASRUtils::expr_type(x.m_array);
        std::string array_type_name = CUtils::get_c_type_from_ttype_t(array_type_asr);
        std::string array_encoded_type_name = ASRUtils::get_type_code(array_type_asr, true, false, false);
        std::string array_type = c_ds_api->get_array_type(array_type_name, array_encoded_type_name, array_types_decls, true);
        std::string return_type = c_ds_api->get_array_type(array_type_name, array_encoded_type_name, array_types_decls, false);

        ASR::ttype_t* shape_type_asr = ASRUtils::expr_type(x.m_shape);
        std::string shape_type_name = CUtils::get_c_type_from_ttype_t(shape_type_asr);
        std::string shape_encoded_type_name = ASRUtils::get_type_code(shape_type_asr, true, false, false);
        std::string shape_type = c_ds_api->get_array_type(shape_type_name, shape_encoded_type_name, array_types_decls, true);

        std::string array_reshape_func = c_utils_functions->get_array_reshape(array_type, shape_type,
            return_type, array_type_name, array_encoded_type_name);
        src = array_reshape_func + "(" + array + ", " + shape + ")";
    }

    void visit_ArrayBound(const ASR::ArrayBound_t& x) {
        visit_expr(*x.m_v);
        std::string var_name = src;
        std::string args = "";
        std::string result_type = CUtils::get_c_type_from_ttype_t(x.m_type);
        visit_expr(*x.m_dim);
        std::string idx = src;
        if( x.m_bound == ASR::arrayboundType::LBound ) {
            src = "((" + result_type + ")" + var_name + "->dims[" + idx + "-1].lower_bound)";
        } else if( x.m_bound == ASR::arrayboundType::UBound ) {
            std::string lower_bound = var_name + "->dims[" + idx + "-1].lower_bound";
            std::string length = var_name + "->dims[" + idx + "-1].length";
            std::string upper_bound = length + " + " + lower_bound + " - 1";
            src = "((" + result_type + ") " + upper_bound + ")";
        }
    }

    void visit_ArrayConstant(const ASR::ArrayConstant_t& x) {
        // TODO: Support and test for multi-dimensional array constants
        headers.insert("stdarg");
        std::string array_const = "";
        for( size_t i = 0; i < x.n_args; i++ ) {
            visit_expr(*x.m_args[i]);
            array_const += src + ", ";
        }
        array_const.pop_back();
        array_const.pop_back();

        ASR::ttype_t* array_type_asr = x.m_type;
        std::string array_type_name = CUtils::get_c_type_from_ttype_t(array_type_asr);
        std::string array_encoded_type_name = ASRUtils::get_type_code(array_type_asr, true, false);
        std::string return_type = c_ds_api->get_array_type(array_type_name, array_encoded_type_name,array_types_decls, false);

        src = c_utils_functions->get_array_constant(return_type, array_type_name, array_encoded_type_name) +
                "(" + std::to_string(x.n_args) + ", " + array_const + ")";
    }

    void visit_ArrayItem(const ASR::ArrayItem_t &x) {
        this->visit_expr(*x.m_v);
        std::string array = src;
        std::string out = array;
        ASR::ttype_t* x_mv_type = ASRUtils::expr_type(x.m_v);
        ASR::dimension_t* m_dims;
        int n_dims = ASRUtils::extract_dimensions_from_ttype(x_mv_type, m_dims);
        bool is_i8_array = (ASR::is_a<ASR::Integer_t>(*x_mv_type) &&
                            ASRUtils::extract_kind_from_ttype_t(x_mv_type) == 1 &&
                            ASRUtils::is_fixed_size_array(m_dims, n_dims) &&
                            ASRUtils::expr_abi(x.m_v) == ASR::abiType::BindC);
        if( is_i8_array ) {
            out += "[";
        } else {
            out += "->data[";
        }
        std::string index = "";
        for (size_t i=0; i<x.n_args; i++) {
            std::string current_index = "";
            if (x.m_args[i].m_right) {
                this->visit_expr(*x.m_args[i].m_right);
            } else {
                src = "/* FIXME right index */";
            }

            if( is_i8_array ) {
                current_index += src;
                for( size_t j = i + 1; j < x.n_args; j++ ) {
                    int64_t dim_size;
                    ASRUtils::extract_value(m_dims[j].m_length, dim_size);
                    std::string length = std::to_string(dim_size);
                    current_index += " * " + length;
                }
                index += current_index;
            } else {
                current_index += "(" + src + " - " + array + "->dims["
                                    + std::to_string(i) + "].lower_bound)";
                for( size_t j = i + 1; j < x.n_args; j++ ) {
                    std::string length = array + "->dims[" + std::to_string(j) + "].length";
                    current_index += " * " + length;
                }
                index += current_index;
            }
            if (i < x.n_args - 1) {
                index += " + ";
            }
        }
        out += index + "]";
        last_expr_precedence = 2;
        src = out;
    }

    void visit_StringItem(const ASR::StringItem_t& x) {
        this->visit_expr(*x.m_idx);
        std::string idx = std::move(src);
        this->visit_expr(*x.m_arg);
        std::string str = std::move(src);
        src = "(char *)memcpy((char *)calloc(2U, sizeof(char)), "
                + str + " + " + idx + " - 1, 1U)";
    }

    void visit_StringLen(const ASR::StringLen_t &x) {
        this->visit_expr(*x.m_arg);
        src = "strlen(" + src + ")";
    }

    void visit_GoTo(const ASR::GoTo_t &x) {
        std::string indent(indentation_level*indentation_spaces, ' ');
        src =  indent + "goto " + std::string(x.m_name) + ";\n";
    }

    void visit_GoToTarget(const ASR::GoToTarget_t &x) {
        src = std::string(x.m_name) + ":\n";
    }
};

Result<std::string> asr_to_c(Allocator &al, ASR::TranslationUnit_t &asr,
    diag::Diagnostics &diagnostics, Platform &platform,
    int64_t default_lower_bound)
{

    LCompilers::PassOptions pass_options;
    pass_options.always_run = true;
    pass_create_subroutine_from_function(al, asr, pass_options);
    pass_replace_array_op(al, asr, pass_options);
    pass_unused_functions(al, asr, pass_options);
    pass_replace_class_constructor(al, asr, pass_options);
    ASRToCVisitor v(diagnostics, platform, default_lower_bound);
    try {
        v.visit_asr((ASR::asr_t &)asr);
    } catch (const CodeGenError &e) {
        diagnostics.diagnostics.push_back(e.d);
        return Error();
    } catch (const Abort &) {
        return Error();
    }
    return v.src;
}

} // namespace LFortran
