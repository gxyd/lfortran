#include <string>

#include <lfortran/config.h>
#include <lfortran/serialization.h>
#include <lfortran/parser/parser.h>
#include <lfortran/parser/parser.tab.hh>
#include <libasr/asr_utils.h>
#include <libasr/asr_verify.h>
#include <libasr/bwriter.h>
#include <libasr/string_utils.h>

using LFortran::ASRUtils::symbol_parent_symtab;
using LFortran::ASRUtils::symbol_name;

namespace LFortran {

class ASTSerializationVisitor :
#ifdef WITH_LFORTRAN_BINARY_MODFILES
        public BinaryWriter,
#else
        public TextWriter,
#endif
        public AST::SerializationBaseVisitor<ASTSerializationVisitor>
{
public:
    void write_bool(bool b) {
        if (b) {
            write_int8(1);
        } else {
            write_int8(0);
        }
    }
};

std::string serialize(const AST::ast_t &ast) {
    ASTSerializationVisitor v;
    v.write_int8(ast.type);
    v.visit_ast(ast);
    return v.get_str();
}

std::string serialize(const AST::TranslationUnit_t &unit) {
    return serialize((AST::ast_t&)(unit));
}

class ASTDeserializationVisitor :
#ifdef WITH_LFORTRAN_BINARY_MODFILES
    public BinaryReader,
#else
    public TextReader,
#endif
    public AST::DeserializationBaseVisitor<ASTDeserializationVisitor>
{
public:
    ASTDeserializationVisitor(Allocator &al, const std::string &s) :
#ifdef WITH_LFORTRAN_BINARY_MODFILES
        BinaryReader(s),
#else
        TextReader(s),
#endif
        DeserializationBaseVisitor(al, true) {}

    bool read_bool() {
        uint8_t b = read_int8();
        return (b == 1);
    }

    char* read_cstring() {
        std::string s = read_string();
        LFortran::Str cs;
        cs.from_str_view(s);
        char* p = cs.c_str(al);
        return p;
    }
};

AST::ast_t* deserialize_ast(Allocator &al, const std::string &s) {
    ASTDeserializationVisitor v(al, s);
    return v.deserialize_node();
}

// ----------------------------------------------------------------

class ASRSerializationVisitor :
#ifdef WITH_LFORTRAN_BINARY_MODFILES
        public BinaryWriter,
#else
        public TextWriter,
#endif
        public ASR::SerializationBaseVisitor<ASRSerializationVisitor>
{
public:
    void write_bool(bool b) {
        if (b) {
            write_int8(1);
        } else {
            write_int8(0);
        }
    }

    void write_symbol(const ASR::symbol_t &x) {
        write_int64(symbol_parent_symtab(&x)->counter);
        write_int8(x.type);
        write_string(symbol_name(&x));
    }
};

std::string serialize(const ASR::asr_t &asr) {
    ASRSerializationVisitor v;
    v.write_int8(asr.type);
    v.visit_asr(asr);
    return v.get_str();
}

std::string serialize(const ASR::TranslationUnit_t &unit) {
    return serialize((ASR::asr_t&)(unit));
}

class ASRDeserializationVisitor :
#ifdef WITH_LFORTRAN_BINARY_MODFILES
        public BinaryReader,
#else
        public TextReader,
#endif
        public ASR::DeserializationBaseVisitor<ASRDeserializationVisitor>
{
public:
    ASRDeserializationVisitor(Allocator &al, const std::string &s,
        bool load_symtab_id) :
#ifdef WITH_LFORTRAN_BINARY_MODFILES
            BinaryReader(s),
#else
            TextReader(s),
#endif
            DeserializationBaseVisitor(al, load_symtab_id) {}

    bool read_bool() {
        uint8_t b = read_int8();
        return (b == 1);
    }

    char* read_cstring() {
        std::string s = read_string();
        LFortran::Str cs;
        cs.from_str_view(s);
        char* p = cs.c_str(al);
        return p;
    }

// FIXME LOCATION: document if this is just initialization that will
// get overriden, or if this needs to be read from the file
// It seems we do not save/read location information, we need to fix it
#define READ_SYMBOL_CASE(x)                                \
    case (ASR::symbolType::x) : {                          \
        s = (ASR::symbol_t*)al.make_new<ASR::x##_t>();     \
        s->type = ASR::symbolType::x;                      \
        s->base.type = ASR::asrType::symbol;               \
        s->base.loc.first = 123;                           \
        break;                                             \
    }

#define INSERT_SYMBOL_CASE(x)                              \
    case (ASR::symbolType::x) : {                          \
        memcpy(sym2, sym, sizeof(ASR::x##_t));             \
        break;                                             \
    }

    ASR::symbol_t *read_symbol() {
        uint64_t symtab_id = read_int64();
        uint64_t symbol_type = read_int8();
        std::string symbol_name  = read_string();
        LFORTRAN_ASSERT(id_symtab_map.find(symtab_id) != id_symtab_map.end());
        SymbolTable *symtab = id_symtab_map[symtab_id];
        if (symtab->scope.find(symbol_name) == symtab->scope.end()) {
            // Symbol is not in the symbol table yet. We construct an empty
            // symbol of the correct type and put it in the symbol table.
            // Later when constructing the symbol table, we will check for this
            // and fill it in correctly.
            ASR::symbolType ty = static_cast<ASR::symbolType>(symbol_type);
            ASR::symbol_t *s;
            switch (ty) {
                READ_SYMBOL_CASE(Program)
                READ_SYMBOL_CASE(Module)
                READ_SYMBOL_CASE(Subroutine)
                READ_SYMBOL_CASE(Function)
                READ_SYMBOL_CASE(GenericProcedure)
                READ_SYMBOL_CASE(ExternalSymbol)
                READ_SYMBOL_CASE(DerivedType)
                READ_SYMBOL_CASE(Variable)
                READ_SYMBOL_CASE(ClassProcedure)
                default : throw LFortranException("Symbol type not supported");
            }
            symtab->scope[symbol_name] = s;
        }
        ASR::symbol_t *sym = symtab->scope[symbol_name];
        return sym;
    }

    void symtab_insert_symbol(SymbolTable &symtab, const std::string &name,
        ASR::symbol_t *sym) {
        if (symtab.scope.find(name) == symtab.scope.end()) {
            symtab.scope[name] = sym;
        } else {
            // We have to copy the contents of `sym` into `sym2` without
            // changing the `sym2` pointer already in the table
            ASR::symbol_t *sym2 = symtab.scope[name];
            // FIXME LOCATION: document what is going on:
            LFORTRAN_ASSERT(sym2->base.loc.first == 123);
            switch (sym->type) {
                INSERT_SYMBOL_CASE(Program)
                INSERT_SYMBOL_CASE(Module)
                INSERT_SYMBOL_CASE(Subroutine)
                INSERT_SYMBOL_CASE(Function)
                INSERT_SYMBOL_CASE(GenericProcedure)
                INSERT_SYMBOL_CASE(ExternalSymbol)
                INSERT_SYMBOL_CASE(DerivedType)
                INSERT_SYMBOL_CASE(Variable)
                INSERT_SYMBOL_CASE(ClassProcedure)
                default : throw LFortranException("Symbol type not supported");
            }
        }
    }
};

namespace ASR {

class FixParentSymtabVisitor : public BaseWalkVisitor<FixParentSymtabVisitor>
{
private:
    SymbolTable *current_symtab;
public:
    void visit_TranslationUnit(const TranslationUnit_t &x) {
        current_symtab = x.m_global_scope;
        x.m_global_scope->asr_owner = (asr_t*)&x;
        for (auto &a : x.m_global_scope->scope) {
            this->visit_symbol(*a.second);
        }
    }

    void visit_Program(const Program_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        x.m_symtab->asr_owner = (asr_t*)&x;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_Module(const Module_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        x.m_symtab->asr_owner = (asr_t*)&x;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_Subroutine(const Subroutine_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        x.m_symtab->asr_owner = (asr_t*)&x;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_Function(const Function_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        x.m_symtab->asr_owner = (asr_t*)&x;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_DerivedType(const DerivedType_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        x.m_symtab->asr_owner = (asr_t*)&x;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

};

class FixExternalSymbolsVisitor : public BaseWalkVisitor<FixExternalSymbolsVisitor>
{
private:
    SymbolTable *global_symtab;
    SymbolTable *external_symtab;
public:
    FixExternalSymbolsVisitor(SymbolTable &symtab) : external_symtab{&symtab} {}

    void visit_TranslationUnit(const TranslationUnit_t &x) {
        global_symtab = x.m_global_scope;
        for (auto &a : x.m_global_scope->scope) {
            this->visit_symbol(*a.second);
        }
    }

    void visit_ExternalSymbol(const ExternalSymbol_t &x) {
        if (x.m_external != nullptr) {
            // Nothing to do, the external symbol is already resolved
            return;
        }
        LFORTRAN_ASSERT(x.m_external == nullptr);
        if (x.m_module_name == nullptr) {
            throw LFortranException("The ExternalSymbol was referenced in some ASR node, but it was not loaded as part of the SymbolTable");
        }
        std::string module_name = x.m_module_name;
        std::string original_name = x.m_original_name;
        if (startswith(module_name, "lfortran_intrinsic_iso")) {
            module_name = module_name.substr(19);
        }
        if (global_symtab->scope.find(module_name) != global_symtab->scope.end()) {
            Module_t *m = down_cast<Module_t>(global_symtab->scope[module_name]);
            symbol_t *sym = m->m_symtab->find_scoped_symbol(original_name, x.n_scope_names, x.m_scope_names);
            if (sym) {
                // FIXME: this is a hack, we need to pass in a non-const `x`.
                ExternalSymbol_t &xx = const_cast<ExternalSymbol_t&>(x);
                xx.m_external = sym;
            } else {
                throw LFortranException("ExternalSymbol cannot be resolved, the symbol '"
                    + original_name + "' was not found in the module '"
                    + module_name + "' (but the module was found)");
            }
        } else if (external_symtab->scope.find(module_name) != external_symtab->scope.end()) {
            Module_t *m = down_cast<Module_t>(external_symtab->scope[module_name]);
            symbol_t *sym = m->m_symtab->find_scoped_symbol(original_name, x.n_scope_names, x.m_scope_names);
            if (sym) {
                // FIXME: this is a hack, we need to pass in a non-const `x`.
                ExternalSymbol_t &xx = const_cast<ExternalSymbol_t&>(x);
                xx.m_external = sym;
            } else {
                throw LFortranException("ExternalSymbol cannot be resolved, the symbol '"
                    + original_name + "' was not found in the module '"
                    + module_name + "' (but the module was found)");
            }
        } else {
            throw LFortranException("ExternalSymbol cannot be resolved, the module '"
                + module_name + "' was not found, so the symbol '"
                + original_name + "' could not be resolved");
        }
    }


};

} // namespace ASR

// Fix ExternalSymbol's symbol to point to symbols from `external_symtab`
// or from `unit`.
void fix_external_symbols(ASR::TranslationUnit_t &unit,
        SymbolTable &external_symtab) {
    ASR::FixExternalSymbolsVisitor e(external_symtab);
    e.visit_TranslationUnit(unit);
}

ASR::asr_t* deserialize_asr(Allocator &al, const std::string &s,
        bool load_symtab_id, SymbolTable &external_symtab) {
    ASRDeserializationVisitor v(al, s, load_symtab_id);
    ASR::asr_t *node = v.deserialize_node();
    ASR::TranslationUnit_t *tu = ASR::down_cast2<ASR::TranslationUnit_t>(node);

    // Connect the `parent` member of symbol tables
    // Also set the `asr_owner` member correctly for all symbol tables
    ASR::FixParentSymtabVisitor p;
    p.visit_TranslationUnit(*tu);

    LFORTRAN_ASSERT(asr_verify(*tu, false));

    // Suppress a warning for now
    if ((bool&)external_symtab) {}

    return node;
}

} // namespace LFortran
