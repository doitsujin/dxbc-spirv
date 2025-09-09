#pragma once

#include <unordered_map>
#include <set>

#include "../ir.h"
#include "../ir_builder.h"

#include "../../util/util_small_vector.h"

namespace dxbc_spv::ir {

class FunctionCleanupPass {

public:

  FunctionCleanupPass(Builder& builder);

  ~FunctionCleanupPass();

  FunctionCleanupPass             (const FunctionCleanupPass&) = delete;
  FunctionCleanupPass& operator = (const FunctionCleanupPass&) = delete;

  /* Converts shared temporary variables to function parameters on input,
   * and a return struct on output. Must be run before SSA construction. */
  void resolveSharedTemps();

  static void runResolveSharedTempPass(Builder& builder);

private:

  struct TempEntry {
    SsaDef function;
    SsaDef var;

    bool operator < (const TempEntry& other) const {
      if (function != other.function)
        return function < other.function;
      return var < other.var;
    }
  };

  Builder& m_builder;

  std::unordered_map<SsaDef, SsaDef> m_tempFunctions;

  std::set<TempEntry> m_temps;



};

}
