#include "ir_pass_function.h"

namespace dxbc_spv::ir {

FunctionCleanupPass::FunctionCleanupPass(Builder& builder)
: m_builder(builder) {

}


FunctionCleanupPass::~FunctionCleanupPass() {

}


void FunctionCleanupPass::resolveSharedTemps() {

}


void FunctionCleanupPass::runResolveSharedTempPass(Builder& builder) {
  FunctionCleanupPass(builder).resolveSharedTemps();
}

}
