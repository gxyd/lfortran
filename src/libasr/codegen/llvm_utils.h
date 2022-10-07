#ifndef LFORTRAN_LLVM_UTILS_H
#define LFORTRAN_LLVM_UTILS_H

#include <memory>

#include <llvm/IR/Value.h>
#include <llvm/IR/IRBuilder.h>
#include <libasr/asr.h>

#include <map>
#include <tuple>

namespace LFortran {

    static inline void printf(llvm::LLVMContext &context, llvm::Module &module,
        llvm::IRBuilder<> &builder, const std::vector<llvm::Value*> &args)
    {
        llvm::Function *fn_printf = module.getFunction("_lfortran_printf");
        if (!fn_printf) {
            llvm::FunctionType *function_type = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(context), {llvm::Type::getInt8PtrTy(context)}, true);
            fn_printf = llvm::Function::Create(function_type,
                    llvm::Function::ExternalLinkage, "_lfortran_printf", &module);
        }
        builder.CreateCall(fn_printf, args);
    }

    static inline void exit(llvm::LLVMContext &context, llvm::Module &module,
        llvm::IRBuilder<> &builder, llvm::Value* exit_code)
    {
        llvm::Function *fn_exit = module.getFunction("exit");
        if (!fn_exit) {
            llvm::FunctionType *function_type = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(context), {llvm::Type::getInt32Ty(context)},
                    false);
            fn_exit = llvm::Function::Create(function_type,
                    llvm::Function::ExternalLinkage, "exit", &module);
        }
        builder.CreateCall(fn_exit, {exit_code});
    }

    namespace LLVM {

        llvm::Value* CreateLoad(llvm::IRBuilder<> &builder, llvm::Value *x);
        llvm::Value* CreateStore(llvm::IRBuilder<> &builder, llvm::Value *x, llvm::Value *y);
        llvm::Value* CreateGEP(llvm::IRBuilder<> &builder, llvm::Value *x, std::vector<llvm::Value *> &idx);
        llvm::Value* CreateInBoundsGEP(llvm::IRBuilder<> &builder, llvm::Value *x, std::vector<llvm::Value *> &idx);
        llvm::Value* lfortran_malloc(llvm::LLVMContext &context, llvm::Module &module,
                llvm::IRBuilder<> &builder, llvm::Value* arg_size);
        llvm::Value* lfortran_realloc(llvm::LLVMContext &context, llvm::Module &module,
                llvm::IRBuilder<> &builder, llvm::Value* ptr, llvm::Value* arg_size);
        llvm::Value* lfortran_calloc(llvm::LLVMContext &context, llvm::Module &module,
                llvm::IRBuilder<> &builder, llvm::Value* count, llvm::Value* type_size);
        llvm::Value* lfortran_free(llvm::LLVMContext &context, llvm::Module &module,
                llvm::IRBuilder<> &builder, llvm::Value* ptr);
        static inline bool is_llvm_struct(ASR::ttype_t* asr_type) {
            return ASR::is_a<ASR::Tuple_t>(*asr_type) ||
                   ASR::is_a<ASR::List_t>(*asr_type) ||
                   ASR::is_a<ASR::Derived_t>(*asr_type) ||
                   ASR::is_a<ASR::Class_t>(*asr_type);
        }
    }

    class LLVMList;
    class LLVMTuple;
    class LLVMDict;

    class LLVMUtils {

        private:

            llvm::LLVMContext& context;
            llvm::IRBuilder<>* builder;
            llvm::AllocaInst *str_cmp_itr;

            bool are_iterators_set;

        public:

            LLVMTuple* tuple_api;
            LLVMList* list_api;
            LLVMDict* dict_api;

            LLVMUtils(llvm::LLVMContext& context,
                llvm::IRBuilder<>* _builder);

            llvm::Value* create_gep(llvm::Value* ds, int idx);

            llvm::Value* create_gep(llvm::Value* ds, llvm::Value* idx);

            llvm::Value* create_ptr_gep(llvm::Value* ptr, int idx);

            llvm::Value* create_ptr_gep(llvm::Value* ptr, llvm::Value* idx);

            llvm::Type* getIntType(int a_kind, bool get_pointer=false);

            void start_new_block(llvm::BasicBlock *bb);

            llvm::Value* lfortran_str_cmp(llvm::Value* left_arg, llvm::Value* right_arg,
                                          std::string runtime_func_name, llvm::Module& module);

            llvm::Value* is_equal_by_value(llvm::Value* left, llvm::Value* right,
                                           llvm::Module& module, ASR::ttype_t* asr_type);

            void set_iterators();

            void reset_iterators();

            void deepcopy(llvm::Value* src, llvm::Value* dest,
                          ASR::ttype_t* asr_type, llvm::Module& module);

    }; // LLVMUtils

    class LLVMList {
        private:

            llvm::LLVMContext& context;
            LLVMUtils* llvm_utils;
            llvm::IRBuilder<>* builder;

            std::map<std::string, std::tuple<llvm::Type*, int32_t, llvm::Type*>> typecode2listtype;

            void resize_if_needed(llvm::Value* list, llvm::Value* n,
                                  llvm::Value* capacity, int32_t type_size,
                                  llvm::Type* el_type, llvm::Module& module);

            void shift_end_point_by_one(llvm::Value* list);

        public:

            LLVMList(llvm::LLVMContext& context_, LLVMUtils* llvm_utils,
                     llvm::IRBuilder<>* builder);

            llvm::Type* get_list_type(llvm::Type* el_type, std::string& type_code,
                                        int32_t type_size);

            void list_init(std::string& type_code, llvm::Value* list,
                           llvm::Module& module, llvm::Value* initial_capacity,
                           llvm::Value* n);

            void list_init(std::string& type_code, llvm::Value* list,
                            llvm::Module& module, int32_t initial_capacity=1,
                            int32_t n=0);

            llvm::Value* get_pointer_to_list_data(llvm::Value* list);

            llvm::Value* get_pointer_to_current_end_point(llvm::Value* list);

            llvm::Value* get_pointer_to_current_capacity(llvm::Value* list);

            void list_deepcopy(llvm::Value* src, llvm::Value* dest,
                                ASR::List_t* list_type,
                                llvm::Module& module);

            void list_deepcopy(llvm::Value* src, llvm::Value* dest,
                                ASR::ttype_t* element_type,
                                llvm::Module& module);

            llvm::Value* read_item(llvm::Value* list, llvm::Value* pos,
                                   bool get_pointer=false, bool check_index_bound=true);

            llvm::Value* len(llvm::Value* list);

            void check_index_within_bounds(llvm::Value* list, llvm::Value* pos);

            void write_item(llvm::Value* list, llvm::Value* pos,
                            llvm::Value* item, ASR::ttype_t* asr_type,
                            llvm::Module& module, bool check_index_bound=true);

            void write_item(llvm::Value* list, llvm::Value* pos,
                            llvm::Value* item, bool check_index_bound=true);

            void append(llvm::Value* list, llvm::Value* item,
                        ASR::ttype_t* asr_type, llvm::Module& module);

            void insert_item(llvm::Value* list, llvm::Value* pos,
                            llvm::Value* item, ASR::ttype_t* asr_type,
                            llvm::Module& module);

            void remove(llvm::Value* list, llvm::Value* item,
                        ASR::ttype_t* item_type, llvm::Module& module);

            void list_clear(llvm::Value* list);

            llvm::Value* find_item_position(llvm::Value* list,
                llvm::Value* item, ASR::ttype_t* item_type,
                llvm::Module& module);

            void free_data(llvm::Value* list, llvm::Module& module);
    };

    class LLVMTuple {
        private:

            llvm::LLVMContext& context;
            LLVMUtils* llvm_utils;
            llvm::IRBuilder<>* builder;

            std::map<std::string, std::pair<llvm::Type*, size_t>> typecode2tupletype;

        public:

            LLVMTuple(llvm::LLVMContext& context_,
                      LLVMUtils* llvm_utils,
                      llvm::IRBuilder<>* builder);

            llvm::Type* get_tuple_type(std::string& type_code,
                                       std::vector<llvm::Type*>& el_types);

            void tuple_init(llvm::Value* llvm_tuple, std::vector<llvm::Value*>& values);

            llvm::Value* read_item(llvm::Value* llvm_tuple, llvm::Value* pos,
                                   bool get_pointer=false);

            llvm::Value* read_item(llvm::Value* llvm_tuple, size_t pos,
                                   bool get_pointer=false);

            void tuple_deepcopy(llvm::Value* src, llvm::Value* dest,
                                ASR::Tuple_t* type_code, llvm::Module& module);

            llvm::Value* check_tuple_equality(llvm::Value* t1, llvm::Value* t2,
                ASR::Tuple_t* tuple_type, llvm::LLVMContext& context,
                llvm::IRBuilder<>* builder, llvm::Module& module);
    };

    class LLVMDict {
        protected:

            llvm::LLVMContext& context;
            LLVMUtils* llvm_utils;
            llvm::IRBuilder<>* builder;
            llvm::AllocaInst *pos_ptr, *is_key_matching_var;
            llvm::AllocaInst *idx_ptr, *hash_iter, *hash_value;
            llvm::AllocaInst *polynomial_powers;
            bool are_iterators_set;

            std::map<std::pair<std::string, std::string>,
                     std::tuple<llvm::Type*, std::pair<int32_t, int32_t>,
                                std::pair<llvm::Type*, llvm::Type*>>> typecode2dicttype;

        public:

            bool is_dict_present;

            LLVMDict(llvm::LLVMContext& context_,
                     LLVMUtils* llvm_utils,
                     llvm::IRBuilder<>* builder);

            llvm::Type* get_dict_type(std::string key_type_code, std::string value_type_code,
                                      int32_t key_type_size, int32_t value_type_size,
                                      llvm::Type* key_type, llvm::Type* value_type);

            void dict_init(std::string key_type_code, std::string value_type_code,
                           llvm::Value* dict, llvm::Module* module, size_t initial_capacity);

            llvm::Value* get_key_list(llvm::Value* dict);

            llvm::Value* get_value_list(llvm::Value* dict);

            llvm::Value* get_pointer_to_occupancy(llvm::Value* dict);

            llvm::Value* get_pointer_to_capacity(llvm::Value* dict);

            llvm::Value* get_key_hash(llvm::Value* capacity, llvm::Value* key,
                                      ASR::ttype_t* key_asr_type, llvm::Module& module);

            virtual
            void resolve_collision(llvm::Value* capacity, llvm::Value* key_hash,
                                llvm::Value* key, llvm::Value* key_list,
                                llvm::Value* key_mask, llvm::Module& module,
                                ASR::ttype_t* key_asr_type, bool for_read=false);

            virtual
            void resolve_collision_for_write(llvm::Value* dict, llvm::Value* key_hash,
                                          llvm::Value* key, llvm::Value* value,
                                          llvm::Module& module, ASR::ttype_t* key_asr_type,
                                          ASR::ttype_t* value_asr_type);

            virtual
            llvm::Value* resolve_collision_for_read(llvm::Value* dict, llvm::Value* key_hash,
                                                 llvm::Value* key, llvm::Module& module,
                                                 ASR::ttype_t* key_asr_type);

            void rehash(llvm::Value* dict, llvm::Module* module,
                        ASR::ttype_t* key_asr_type,
                        ASR::ttype_t* value_asr_type);

            void rehash_all_at_once_if_needed(llvm::Value* dict,
                                              llvm::Module* module,
                                              ASR::ttype_t* key_asr_type,
                                              ASR::ttype_t* value_asr_type);

            void write_item(llvm::Value* dict, llvm::Value* key,
                            llvm::Value* value, llvm::Module* module,
                            ASR::ttype_t* key_asr_type, ASR::ttype_t* value_asr_type);

            llvm::Value* read_item(llvm::Value* dict, llvm::Value* key,
                                   llvm::Module& module, ASR::ttype_t* key_asr_type,
                                   bool get_pointer=false);

            llvm::Value* pop_item(llvm::Value* dict, llvm::Value* key,
                                   llvm::Module& module, ASR::Dict_t* dict_type,
                                   bool get_pointer=false);

            llvm::Value* get_pointer_to_keymask(llvm::Value* dict);

            void set_iterators();

            void reset_iterators();

            void dict_deepcopy(llvm::Value* src, llvm::Value* dest,
                               ASR::Dict_t* dict_type, llvm::Module* module);

            llvm::Value* len(llvm::Value* dict);

            virtual ~LLVMDict();
    };

    class LLVMDictOptimizedLinearProbing: public LLVMDict {

        public:

            LLVMDictOptimizedLinearProbing(llvm::LLVMContext& context_,
                                    LLVMUtils* llvm_utils,
                                    llvm::IRBuilder<>* builder);

            virtual
            void resolve_collision(llvm::Value* capacity, llvm::Value* key_hash,
                                llvm::Value* key, llvm::Value* key_list,
                                llvm::Value* key_mask, llvm::Module& module,
                                ASR::ttype_t* key_asr_type, bool for_read=false);

            virtual
            void resolve_collision_for_write(llvm::Value* dict, llvm::Value* key_hash,
                                            llvm::Value* key, llvm::Value* value,
                                            llvm::Module& module, ASR::ttype_t* key_asr_type,
                                            ASR::ttype_t* value_asr_type);

            virtual
            llvm::Value* resolve_collision_for_read(llvm::Value* dict, llvm::Value* key_hash,
                                                    llvm::Value* key, llvm::Module& module,
                                                    ASR::ttype_t* key_asr_type);

            virtual ~LLVMDictOptimizedLinearProbing();

    };

} // LFortran

#endif // LFORTRAN_LLVM_UTILS_H
