#pragma once

#include "../ir.h"
#include "../ir_builder.h"

namespace dxbc_spv::ir {

/** Pass to convert between raw/structured and typed buffers. This is necessary to
 *  ensure raw and structured alignment requirements, and to support sparse feedback
 *  on raw and structured buffers in SPIR-V environments.
 */
class ConvertBufferKindPass {

public:

  struct Options {
    /* Demote all raw buffers to typed. */
    bool useTypedForRaw = false;
    /* Demote all structured buffers to typed whose struct size
     * is not a multiple of the given minimum alignment. */
    bool useTypedForStructured = false;
    /* Demote buffers with sparse feedback loads to typed. */
    bool useTypedForSparseFeedback = false;
    /* Use raw buffers for typed buffers used with 32-bit atomics. This works
     * because client APIs require the buffer to use a 32-bit integer format. */
    bool useRawForTypedAtomic = false;
    /* Force R32 formats for typed UAVs that are read. */
    bool forceFormatForTypedUavRead = false;
    /* Minimum structure alignment for structured buffers. Any structured
     * buffer with a struct size that is a multiple of the given alignment
     * will be kept as a structured buffer regardless of he option. */
    uint32_t minStructureAlignment = 0u;
  };

  ConvertBufferKindPass(Builder& builder, const Options& options);

  ~ConvertBufferKindPass();

  ConvertBufferKindPass             (const ConvertBufferKindPass&) = delete;
  ConvertBufferKindPass& operator = (const ConvertBufferKindPass&) = delete;

  /** Runs pass with the given options. This expects that the resource
   *  type propagation pass has already run and raw buffers are typed. */
  void run();

  /** Initializes and runs pass on the given builder. */
  static void runPass(Builder& builder, const Options& options);

private:

  ir::Builder&  m_builder;
  Options       m_options;

  void forceFormatForTypedUavLoad(SsaDef def);

  void convertTypedToRaw(SsaDef def);

  void convertRawStructuredToTyped(SsaDef def);

  void convertRawStructuredBufferLoadToTyped(SsaDef use, const Type& resourceType);

  void convertRawStructuredBufferStoreToTyped(SsaDef use, const Type& resourceType);

  void convertRawStructuredBufferAtomicToTyped(SsaDef use, const Type& resourceType);

  void convertRawStructuredBufferQueryToTyped(SsaDef use, const Type& resourceType);

  void convertTypedBufferLoadToRaw(SsaDef use);

  void convertTypedBufferStoreToRaw(SsaDef use);

  SsaDef flattenAddress(Type type, SsaDef ref, SsaDef address) const;

  template<typename Fn>
  void forEachResourceUse(SsaDef def, const Fn& fn);

  bool shouldConvertToTypedBuffer(SsaDef def) const;

  bool shouldConvertToRawBuffer(SsaDef def) const;

  bool resourceHasSparseFeedbackLoads(SsaDef def) const;

  uint32_t getConstantAddress(SsaDef address, uint32_t component) const;

  SsaDef extractFromVector(SsaDef ref, SsaDef vector, uint32_t component) const;

};

}
