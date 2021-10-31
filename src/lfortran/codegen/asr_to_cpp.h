#ifndef LFORTRAN_ASR_TO_CPP_H
#define LFORTRAN_ASR_TO_CPP_H

#include <lfortran/asr.h>
#include <lfortran/codegen/fortran_evaluator.h>

namespace LFortran {

    Result<std::string> asr_to_cpp(Allocator &al, ASR::TranslationUnit_t &asr,
        diag::Diagnostics &diagnostics);

} // namespace LFortran

#endif // LFORTRAN_ASR_TO_CPP_H
