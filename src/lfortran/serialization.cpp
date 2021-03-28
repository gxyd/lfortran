#include <string>

#include <lfortran/serialization.h>
#include <lfortran/parser/parser.h>
#include <lfortran/parser/parser.tab.hh>
#include <lfortran/asr_utils.h>
#include <lfortran/asr_verify.h>


namespace LFortran {

std::string uint64_to_string(uint64_t i) {
    char bytes[4];
    bytes[0] = (i >> 24) & 0xFF;
    bytes[1] = (i >> 16) & 0xFF;
    bytes[2] = (i >>  8) & 0xFF;
    bytes[3] =  i        & 0xFF;
    return std::string(bytes, 4);
}

uint64_t string_to_uint64(const char *s) {
    // The cast from signed char to unsigned char is important,
    // otherwise the signed char shifts return wrong value for negative numbers
    const uint8_t *p = (const unsigned char*)s;
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

uint64_t string_to_uint64(const std::string &s) {
    return string_to_uint64(&s[0]);
}

class ASTSerializationVisitor : public
                             AST::SerializationBaseVisitor<ASTSerializationVisitor>
{
private:
    std::string s;
public:
    std::string get_str() {
        return s;
    }

    void write_int8(uint8_t i) {
        char c=i;
        s.append(std::string(&c, 1));
    }

    void write_int64(uint64_t i) {
        s.append(uint64_to_string(i));
    }

    void write_bool(bool b) {
        if (b) {
            write_int8(1);
        } else {
            write_int8(0);
        }
    }

    void write_string(const std::string &t) {
        write_int64(t.size());
        s.append(t);
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

class ASTDeserializationVisitor : public
                             AST::DeserializationBaseVisitor<ASTDeserializationVisitor>
{
private:
    std::string s;
    size_t pos;
public:
    ASTDeserializationVisitor(Allocator &al, const std::string &s) :
            DeserializationBaseVisitor(al, true), s{s}, pos{0} {}

    uint8_t read_int8() {
        if (pos+1 > s.size()) {
            throw LFortranException("String is too short for deserialization.");
        }
        uint8_t n = s[pos];
        pos += 1;
        return n;
    }

    uint64_t read_int64() {
        if (pos+4 > s.size()) {
            throw LFortranException("String is too short for deserialization.");
        }
        uint64_t n = string_to_uint64(&s[pos]);
        pos += 4;
        return n;
    }

    bool read_bool() {
        uint8_t b = read_int8();
        return (b == 1);
    }

    std::string read_string() {
        size_t n = read_int64();
        if (pos+n > s.size()) {
            throw LFortranException("String is too short for deserialization.");
        }
        std::string r = std::string(&s[pos], n);
        pos += n;
        return r;
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

class ASRSerializationVisitor : public
                             ASR::SerializationBaseVisitor<ASRSerializationVisitor>
{
private:
    std::string s;
public:
    std::string get_str() {
        return s;
    }

    void write_int8(uint8_t i) {
        char c=i;
        s.append(std::string(&c, 1));
    }

    void write_int64(uint64_t i) {
        s.append(uint64_to_string(i));
    }

    void write_bool(bool b) {
        if (b) {
            write_int8(1);
        } else {
            write_int8(0);
        }
    }

    void write_string(const std::string &t) {
        write_int64(t.size());
        s.append(t);
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

class ASRDeserializationVisitor : public
                             ASR::DeserializationBaseVisitor<ASRDeserializationVisitor>
{
private:
    std::string s;
    size_t pos;
public:
    ASRDeserializationVisitor(Allocator &al, const std::string &s,
        bool load_symtab_id) :
            DeserializationBaseVisitor(al, load_symtab_id), s{s}, pos{0} {}

    uint8_t read_int8() {
        if (pos+1 > s.size()) {
            throw LFortranException("String is too short for deserialization.");
        }
        uint8_t n = s[pos];
        pos += 1;
        return n;
    }

    uint64_t read_int64() {
        if (pos+4 > s.size()) {
            throw LFortranException("String is too short for deserialization.");
        }
        uint64_t n = string_to_uint64(&s[pos]);
        pos += 4;
        return n;
    }

    bool read_bool() {
        uint8_t b = read_int8();
        return (b == 1);
    }

    std::string read_string() {
        size_t n = read_int64();
        if (pos+n > s.size()) {
            throw LFortranException("String is too short for deserialization.");
        }
        std::string r = std::string(&s[pos], n);
        pos += n;
        return r;
    }

    char* read_cstring() {
        std::string s = read_string();
        LFortran::Str cs;
        cs.from_str_view(s);
        char* p = cs.c_str(al);
        return p;
    }

#define READ_SYMBOL_CASE(x)                                \
    case (ASR::symbolType::x) : {                          \
        s = (ASR::symbol_t*)al.make_new<ASR::x##_t>();     \
        s->type = ASR::symbolType::x;                      \
        s->base.type = ASR::asrType::symbol;               \
        s->base.loc.first_line = 123;                      \
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
            LFORTRAN_ASSERT(sym2->base.loc.first_line == 123);
            switch (sym->type) {
                INSERT_SYMBOL_CASE(Program)
                INSERT_SYMBOL_CASE(Module)
                INSERT_SYMBOL_CASE(Subroutine)
                INSERT_SYMBOL_CASE(Function)
                INSERT_SYMBOL_CASE(GenericProcedure)
                INSERT_SYMBOL_CASE(ExternalSymbol)
                INSERT_SYMBOL_CASE(DerivedType)
                INSERT_SYMBOL_CASE(Variable)
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
        for (auto &a : x.m_global_scope->scope) {
            this->visit_symbol(*a.second);
        }
    }

    void visit_Program(const Program_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_Module(const Module_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_Subroutine(const Subroutine_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_Function(const Function_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

    void visit_DerivedType(const DerivedType_t &x) {
        SymbolTable *parent_symtab = current_symtab;
        current_symtab = x.m_symtab;
        x.m_symtab->parent = parent_symtab;
        for (auto &a : x.m_symtab->scope) {
            this->visit_symbol(*a.second);
        }
        current_symtab = parent_symtab;
    }

};

} // namespace ASR

ASR::asr_t* deserialize_asr(Allocator &al, const std::string &s,
        bool load_symtab_id) {
    ASRDeserializationVisitor v(al, s, load_symtab_id);
    ASR::asr_t *node = v.deserialize_node();
    ASR::TranslationUnit_t *tu = ASR::down_cast2<ASR::TranslationUnit_t>(node);

    // Connect the `parent` member of symbol tables
    ASR::FixParentSymtabVisitor p;
    p.visit_TranslationUnit(*tu);

    LFORTRAN_ASSERT(asr_verify(*tu));

    return node;
}

} // namespace LFortran
