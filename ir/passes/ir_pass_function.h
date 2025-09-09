#pragma once

#include <map>
#include <unordered_map>
#include <unordered_set>

#include "../ir.h"
#include "../ir_builder.h"

#include "../../util/util_hash.h"
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

  /* Removes unused parameters and return values from functions */
  void removeUnusedParameters();

  static void runRemoveParameterPass(Builder& builder);

private:

  struct TmpParamInfo {
    BasicType type = { };
    SsaDef sharedTemp = { };
    SsaDef localTemp = { };
  };

  struct ParamEntry {
    SsaDef function = { };
    SsaDef param = { };

    bool operator == (const ParamEntry& other) const {
      return function == other.function && param == other.param;
    }

    bool operator != (const ParamEntry& other) const {
      return function != other.function || param != other.param;
    }
  };

  struct ParamEntryHash {
    size_t operator () (const ParamEntry& e) const {
      std::hash<SsaDef> hash;
      return util::hash_combine(hash(e.function), hash(e.param));
    }
  };

  Builder& m_builder;

  std::unordered_map<SsaDef, SsaDef> m_sharedTemps;
  std::unordered_map<SsaDef, uint32_t> m_callDepth;

  std::multimap<SsaDef, SsaDef> m_functionTemps;
  std::multimap<SsaDef, SsaDef> m_functionCalls;

  void gatherSharedTempUses();

  void propagateSharedTempUses();

  bool propagateSharedTempUsesRound();

  void removeLocalTempsFromLookupTables();

  void resolveSharedTempsForFunction(SsaDef fn);

  bool isEntryPointFunction(SsaDef function) const;

  void rewriteSharedTempUses(SsaDef function, TmpParamInfo* a, TmpParamInfo* b);

  void resolveLocalTempTypes(TmpParamInfo* a, TmpParamInfo* b);

  void adjustFunctionCallsForSharedTemps(SsaDef function, TmpParamInfo* a, TmpParamInfo* b);

  void adjustFunctionForSharedTemps(SsaDef function, TmpParamInfo* a, TmpParamInfo* b);

  void determineFunctionCallDepth();

  void removeReturnValue(SsaDef function);

  Builder::iterator findFunctionStart(SsaDef function);

  BasicType determineLocalTempType(BasicType type, const Op& op) const;

  static bool insertUnique(std::multimap<SsaDef, SsaDef>& map, SsaDef fn, SsaDef value);

};

}
