#ifndef LIBASR_PASS_DO_LOOPS_H
#define LIBASR_PASS_DO_LOOPS_H

#include <libasr/asr.h>
#include <libasr/utils.h>

namespace LCompilers {

    void pass_replace_do_loops(Allocator &al, ASR::TranslationUnit_t &unit,
                                const PassOptions &pass_options);

} // namespace LCompilers

#endif // LIBASR_PASS_DO_LOOPS_H
