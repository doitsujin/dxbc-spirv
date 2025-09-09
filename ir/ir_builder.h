#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ir.h"
#include "ir_container.h"

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
  using UseList = small_vector<SsaDef, 4>;

  SsaDef prev = { };
  SsaDef next = { };

  Op op = { };

  UseList uses = { };
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


  class use_iterator {
    using list_iterator = typename OpMetadata::UseList::const_iterator;
  public:

    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Op;
    using reference_type = const Op&;
    using pointer = const Op*;

    use_iterator() = default;

    use_iterator(const Builder& builder, list_iterator iter)
    : m_builder(&builder), m_iter(iter) { }

    reference_type operator * () const {
      return m_builder->getOp(*m_iter);
    }

    pointer operator -> () const {
      return &m_builder->getOp(*m_iter);
    }

    use_iterator& operator ++ () {
      m_iter++;
      return *this;
    }

    use_iterator operator ++ (int) {
      use_iterator result = *this;
      m_iter++;
      return result;
    }

    use_iterator& operator -- () {
      m_iter--;
      return *this;
    }

    use_iterator operator -- (int) {
      use_iterator result = *this;
      m_iter--;
      return result;
    }

    bool operator == (const use_iterator& other) const {
      return m_builder == other.m_builder && m_iter == other.m_iter;
    }

    bool operator != (const use_iterator& other) const {
      return !(this->operator == (other));
    }

  private:

    const Builder*  m_builder = nullptr;
    list_iterator   m_iter    = nullptr;

  };


  Builder();

  ~Builder();

  /** Queries instruction for given SSA def. Will return
   *  an \c eUnknown op if \c def is a null def. */
  const Op& getOp(SsaDef def) const {
    return m_ops.at(def).op;
  }

  /** Queries instruction for the operand of another instruction. Convenience
   *  method that assumes that the given operand is an SSA def. */
  const Op& getOpForOperand(const Op& op, uint32_t operandIndex) const {
    dxbc_spv_assert(operandIndex < op.getFirstLiteralOperandIndex());
    return getOp(SsaDef(op.getOperand(operandIndex)));
  }

  const Op& getOpForOperand(SsaDef def, uint32_t operandIndex) const {
    return getOpForOperand(getOp(def), operandIndex);
  }

  /** Queries SSA def of next or previous instruction in stream. */
  SsaDef getNext(SsaDef def) const { return def ? m_ops.at(def).next : SsaDef(); }
  SsaDef getPrev(SsaDef def) const { return def ? m_ops.at(def).prev : SsaDef(); }

  /** Queries number of users of a given SSA def. */
  uint32_t getUseCount(SsaDef def) const {
    return uint32_t(m_ops.at(def).uses.size());
  }

  /** Gets iterator pair over all uses of an instruction. Note that
   *  these iterators get invalidated when modifying, adding or
   *  removing any instructions. */
  [[nodiscard]] auto getUses(SsaDef def) const {
    auto& uses = m_ops.at(def).uses;

    return std::make_pair(
      use_iterator(*this, uses.begin()),
      use_iterator(*this, uses.end()));
  }

  /** Writes instruction uses into a container. Convenience method to
   *  create a local copy of the use array when iterators cannot be used. */
  template<typename Container>
  void getUses(SsaDef def, Container& container) {
    auto& uses = m_ops.at(def).uses;

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
    return m_ops.getMaxValidDef().getId() + 1u;
  }

  /** Queries definition with the highest valid ID. Useful to pre-allocate
   *  memory for containers. */
  SsaDef getMaxValidDef() const {
    return m_ops.getMaxValidDef();
  }

  /** Convenience method to create and add a constant op. */
  template<typename... T>
  SsaDef makeConstant(T... args) {
    return add(Op::Constant(args...));
  }

  /** Adds an undefined value */
  SsaDef makeUndef(const Type& type) {
    return add(Op::Undef(type));
  }

  /** Convenience method to create a zero constant for a given type. */
  SsaDef makeConstantZero(const Type& t);

  /** Changes the type of the given operation without having
   *  to rewrite the entire operation. */
  void setOpType(SsaDef def, const Type& type) {
    dxbc_spv_assert(def);
    m_ops.at(def).op.setType(type);
  }

  /** Changes flags of the given operation without having
   *  to rewrite the entire operation. */
  void setOpFlags(SsaDef def, OpFlags flags) {
    dxbc_spv_assert(def);
    m_ops.at(def).op.setFlags(flags);
  }

  /** Convenince method to add an instruction either after the
   *  current insertion cursor, or the end of the declarative
   *  block if the op in question is declarative.
   *  Note that \c Constant declarations are deduplicated. */
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
  SsaDef remove(SsaDef def);

  /** Convenience method to removes an instruction by reference.
   *  Returns the definition of the next instruction. */
  SsaDef removeOp(const Op& op);

  /** Replaces operation for the given SSA definition. Useful when
   *  a 1:1 replcement is required, or when changing operands. Note
   *  that this must not be used on \c Constant declarations in order
   *  to avoid undesired side effects. */
  void rewriteOp(SsaDef def, Op op);

  /** Rewrites SSA definition. All uses of the previous definition
   *  will be replaced with the new definition, and the previous
   *  instruction will be removed. Returns next valid instruction. */
  SsaDef rewriteDef(SsaDef oldDef, SsaDef newDef);

  /** Reorders a block of instructions before another instruction.
   *  This only changes the linked list of instructions. Note that
   *  the reference node must not be included in the first..last
   *  range, or behavioru is undefined, */
  void reorderBefore(SsaDef ref, SsaDef first, SsaDef last);

  /** Reorders a block of instructions after another instruction. */
  void reorderAfter(SsaDef ref, SsaDef first, SsaDef last);

  /** Sets insertion cursor for subsequent calls to \c add. Returns
   *  the current cursor location so it can be changed back after
   *  modifying code at a different location. */
  SsaDef setCursor(SsaDef def) {
    return std::exchange(m_cursor, def);
  }

  /** Resets insertion cursor to end of module. */
  void resetCursor();

  struct ConstantHash {
    size_t operator () (const Op& op) const;
  };

  struct ConstantEq {
    bool operator () (const Op& a, const Op& b) const;
  };

private:

  Container<OpMetadata> m_ops;

  std::unordered_set<Op, ConstantHash, ConstantEq> m_constants;

  OpList m_code;
  SsaDef m_codeBlockStart;
  SsaDef m_free = { };
  SsaDef m_cursor = { };

  std::pair<SsaDef, bool> writeOp(Op&& op);

  void addUse(SsaDef target, SsaDef user);

  void removeUse(SsaDef target, SsaDef user);

  void insertNode(SsaDef def);

  void insertNodes(SsaDef first, SsaDef last);

  SsaDef removeNode(SsaDef def);

  void unlinkNodes(SsaDef first, SsaDef last);

  SsaDef allocSsaDef();

  SsaDef lookupConstant(const Op& op) const;

};

}
