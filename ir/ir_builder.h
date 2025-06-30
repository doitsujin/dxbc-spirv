#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#include "ir.h"

#include "../util/util_small_vector.h"

namespace dxbc_spv::ir {

using util::small_vector;

/** Doubly-linked list of operations. */
struct OpList {
  SsaDef head = { };
  SsaDef tail = { };
};


/** Doubly-linked list of operations. If used as a free list,
 *  acts as a single-linked list with the prev link being null. */
struct OpMetadata {
  SsaDef prev = { };
  SsaDef next = { };

  small_vector<SsaDef, 4> uses = { };
};


/** IR builder. */
class Builder {

public:

  class iterator {

  public:

    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Op;
    using reference_type = const Op&;
    using pointer = const Op*;

    iterator() = default;

    iterator(const Builder& builder, SsaDef def)
    : m_builder(&builder), m_def(def) { }

    reference_type operator * () const {
      return m_builder->getOp(m_def);
    }

    pointer operator -> () const {
      return &m_builder->getOp(m_def);
    }

    iterator& operator ++ () {
      m_def = m_builder->getNext(m_def);
      return *this;
    }

    iterator operator ++ (int) {
      iterator result = *this;
      m_def = m_builder->getNext(m_def);
      return result;
    }

    iterator& operator -- () {
      m_def = m_builder->getPrev(m_def);
      return *this;
    }

    iterator operator -- (int) {
      iterator result = *this;
      m_def = m_builder->getPrev(m_def);
      return result;
    }

    bool operator == (const iterator& other) const {
      return m_builder == other.m_builder && m_def == other.m_def;
    }

    bool operator != (const iterator& other) const {
      return !(this->operator == (other));
    }

  private:

    const Builder* m_builder = nullptr;
    SsaDef m_def = { };

  };

  Builder();

  ~Builder();

  /** Queries instruction for given SSA def. Note that references to
   *  instructions get invalidated any time instructions are modified.
   *  Will return a \c eUnknown op if \c def is a null def. */
  const Op& getOp(SsaDef def) const {
    return m_ops.at(def.getId());
  }

  /** Queries SSA def of next or previous instruction in stream. */
  SsaDef getNext(SsaDef def) const { return def ? m_metadata.at(def.getId()).next : SsaDef(); }
  SsaDef getPrev(SsaDef def) const { return def ? m_metadata.at(def.getId()).prev : SsaDef(); }

  /** Queries number of users of a given SSA def. */
  uint32_t getUseCount(SsaDef def) const {
    return uint32_t(m_metadata.at(def.getId()).uses.size());
  }

  /** Gets iterator pair over all uses of an instruction. Note that
   *  these iterators get invalidated when modifying, adding or
   *  removing any instructions. */
  auto getUses(SsaDef def) const {
    auto& uses = m_metadata.at(def.getId()).uses;
    return std::make_pair(uses.begin(), uses.end());
  }

  /** Writes instruction uses into a container. Convenience method to
   *  create a local copy of the use array when iterators cannot be used. */
  template<typename Container>
  void getUses(SsaDef def, Container& container) {
    auto& uses = m_metadata.at(def.getId()).uses;

    for (auto use : uses)
      container.push_back(use);
  }

  /** Iterator pointing to first instruction. */
  iterator begin() const {
    return iter(m_code.head);
  }

  /** Instruction end iterator. */
  iterator end() const {
    return iter(SsaDef());
  }

  /** Iterator starting at given instruction. */
  iterator iter(SsaDef def) const {
    return iterator(*this, def);
  }

  /** Iterator pair over all instructions. */
  std::pair<iterator, iterator> getInstructions() const {
    return { begin(), end() };
  }

  /** Iterator pair over all declarative instructions. */
  std::pair<iterator, iterator> getDeclarations() const {
    return { begin(), iter(m_codeBlockStart) };
  }

  /** Iterator pair over all non-declarative instructions. */
  std::pair<iterator, iterator> getCode() const {
    return { iter(m_codeBlockStart), end() };
  }

  /** Queries current number of SSA definitions. Not all defs within the range
   *  are necessarly used, but passes can use this to allocate look-up tables
   *  from SSA IDs to their own internal metadata. */
  uint32_t getDefCount() const {
    return uint32_t(m_ops.size());
  }

  /** Convenince method to add an instruction either to the
   *  end of the module, or the end of the declarative block
   *  if the op in question is declarative. */
  SsaDef add(Op op);

  /** Inserts an instruction into the code before another in
   *  the code. If the reference is 0, the instruction will
   *  be appended to the stream. */
  SsaDef addBefore(SsaDef ref, Op op);

  /** Inserts an instruction into the code after another in
   *  the code. If the reference is 0, the instruction will
   *  be prepended to the stream. */
  SsaDef addAfter(SsaDef ref, Op op);

  /** Removes an instruction. The caller must make sure that
   *  no instructions reference removed instructions. */
  void remove(SsaDef def);

  /** Convenience method to removes an instruction by reference. */
  void removeOp(const Op& op);

  /** Replaces operation for the given SSA definition. Useful when
   *  a 1:1 replcement is required, or when changing operands. */
  void rewriteOp(SsaDef def, Op op);

  /** Rewrites SSA definition. All uses of the previous definition
   *  will be replaced with the new definition, and the previous
   *  instruction will be removed. */
  void rewriteDef(SsaDef oldDef, SsaDef newDef);

private:

  std::vector<Op>         m_ops;
  std::vector<OpMetadata> m_metadata;

  OpList m_code;
  SsaDef m_codeBlockStart;
  SsaDef m_free = { };

  SsaDef writeOp(Op&& op);

  void addUse(SsaDef target, SsaDef user);

  void removeUse(SsaDef target, SsaDef user);

  void insertNode(SsaDef def);

  void removeNode(SsaDef def);

  SsaDef allocSsaDef();

};

}
