/* -*- C++ -*- */

/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#ifndef FIX_FIELDMAP
#define FIX_FIELDMAP

#ifdef _MSC_VER
#pragma warning(disable : 4786)
#endif

#include "Exceptions.h"
#include "Field.h"
#include "MessageSorters.h"
#include "Utility.h"
#include <algorithm>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

namespace FIX {

class GroupArena {
public:
  static constexpr size_t SLOT_SIZE = 128;
  static constexpr size_t DEFAULT_CAPACITY = 32;

  GroupArena() : GroupArena(DEFAULT_CAPACITY) {}
  explicit GroupArena(size_t capacity)
      : m_storage(new char[capacity * SLOT_SIZE]),
        m_capacity(capacity) {}

  [[nodiscard]] void *allocate() {
    if (m_count < m_capacity)
      return m_storage.get() + m_count++ * SLOT_SIZE;
    m_overflowCount++;
    return ::operator new(SLOT_SIZE);
  }

  void deallocate(void *p) {
    if (!owns(p)) {
      ::operator delete(p);
      if (m_overflowCount > 0) m_overflowCount--;
    }
  }

  [[nodiscard]] bool owns(const void *p) const {
    auto *cp = static_cast<const char *>(p);
    return cp >= m_storage.get() && cp < m_storage.get() + m_capacity * SLOT_SIZE;
  }

  void reset() { m_count = 0; m_overflowCount = 0; }
  size_t count() const { return m_count + m_overflowCount; }

private:
  std::unique_ptr<char[]> m_storage;
  size_t m_capacity;
  size_t m_count = 0;
  size_t m_overflowCount = 0;
};

/**
 * Stores and organizes a collection of Fields.
 *
 * This is the basis for a message, header, and trailer.  This collection
 * class uses a sorter to keep the fields in a particular order.
 */
class FieldMap {

  class sorter {
  public:
    explicit sorter(const message_order &order)
        : m_order(order) {}

    bool operator()(int tag, const FieldBase &right) const { return m_order(tag, right.getTag()); }

    bool operator()(const FieldBase &left, int tag) const { return m_order(left.getTag(), tag); }

    bool operator()(const FieldBase &left, const FieldBase &right) const {
      return m_order(left.getTag(), right.getTag());
    }

  private:
    const message_order &m_order;
  };

  class finder {
  public:
    explicit finder(int tag)
        : m_tag(tag) {}

    bool operator()(const FieldBase &field) const { return m_tag == field.getTag(); }

  private:
    int m_tag;
  };

  enum {
    DEFAULT_SIZE = 16
  };

protected:
  FieldMap(const message_order &order, int size);

public:
  typedef std::vector<FieldBase, ALLOCATOR<FieldBase>> Fields;
  typedef std::
      map<int, std::vector<FieldMap *>, std::less<int>, ALLOCATOR<std::pair<const int, std::vector<FieldMap *>>>>
          Groups;

  typedef Fields::iterator iterator;
  typedef Fields::const_iterator const_iterator;
  typedef Fields::value_type value_type;
  typedef Groups::iterator g_iterator;
  typedef Groups::const_iterator g_const_iterator;
  typedef Groups::value_type g_value_type;

  FieldMap(const message_order &order = message_order(message_order::normal));

  FieldMap(const int order[]);

  FieldMap(const FieldMap &copy);
  FieldMap(FieldMap &&rhs);

  virtual ~FieldMap();

  FieldMap &operator=(const FieldMap &rhs);
  FieldMap &operator=(FieldMap &&rhs);

  /// Set a field without type checking
  void setField(const FieldBase &field, bool overwrite = true) EXCEPT(RepeatedTag) {
    if (!overwrite) {
      addField(field);
    } else {
      Fields::iterator foundField = findTag(field.getTag());
      if (foundField == m_fields.end()) {
        addField(field);
      } else {
        foundField->setString(field.getString());
      }
    }
  }

  /// Set a field without a field class
  void setField(int tag, const std::string &value) EXCEPT(RepeatedTag, NoTagValue) {
    FieldBase fieldBase(tag, value);
    setField(fieldBase);
  }

  /// Get a field if set
  bool getFieldIfSet(FieldBase &field) const {
    Fields::const_iterator foundField = findTag(field.getTag());
    if (foundField == m_fields.end()) {
      return false;
    }
    field = (*foundField);
    return true;
  }

  /// Get a field without type checking
  FieldBase &getField(FieldBase &field) const EXCEPT(FieldNotFound) {
    field = getFieldRef(field.getTag());
    return field;
  }

  /// Get a field without type checking
  template <typename T> const T &getField() const EXCEPT(FieldNotFound) {
    return *reinterpret_cast<const T *>(&getFieldRef(T::tag));
  }

  /// Get a field without a field class
  const std::string &getField(int tag) const EXCEPT(FieldNotFound) { return getFieldRef(tag).getString(); }

  /// Get direct access to a field through a reference
  const FieldBase &getFieldRef(int tag) const EXCEPT(FieldNotFound) {
    Fields::const_iterator field = findTag(tag);
    if (field == m_fields.end()) {
      throw FieldNotFound(tag);
    }
    return (*field);
  }

  /// Get direct access to a field through a pointer
  const FieldBase *const getFieldPtr(int tag) const EXCEPT(FieldNotFound) { return &getFieldRef(tag); }

  /// Check to see if a field is set
  bool isSetField(const FieldBase &field) const { return isSetField(field.getTag()); }
  /// Check to see if a field is set by referencing its number
  bool isSetField(int tag) const { return findTag(tag) != m_fields.end(); }

  /// Remove a field. If field is not present, this is a no-op.
  void removeField(int tag);

  /// Add a group.
  void addGroup(int tag, const FieldMap &group, bool setCount = true);

  /// Acquire ownership of Group object
  void addGroupPtr(int tag, FieldMap *group, bool setCount = true);

  /// Replace a specific instance of a group.
  void replaceGroup(int num, int tag, const FieldMap &group);

  /// Get a specific instance of a group.
  FieldMap &getGroup(int num, int tag, FieldMap &group) const EXCEPT(FieldNotFound) {
    return group = getGroupRef(num, tag);
  }

  /// Get direct access to a field through a reference
  FieldMap &getGroupRef(int num, int tag) const EXCEPT(FieldNotFound) {
    Groups::const_iterator tagWithGroups = m_groups.find(tag);
    if (tagWithGroups == m_groups.end()) {
      throw FieldNotFound(tag);
    }
    if (num <= 0) {
      throw FieldNotFound(tag);
    }
    if (tagWithGroups->second.size() < static_cast<unsigned>(num)) {
      throw FieldNotFound(tag);
    }
    return *(*(tagWithGroups->second.begin() + (num - 1)));
  }

  /// Get direct access to a field through a pointer
  FieldMap *getGroupPtr(int num, int tag) const EXCEPT(FieldNotFound) { return &getGroupRef(num, tag); }

  const Groups &groups() const { return m_groups; }

  /// Remove a specific instance of a group.
  void removeGroup(int num, int tag);
  /// Remove all instances of a group.
  void removeGroup(int tag);

  /// Check to see any instance of a group exists
  bool hasGroup(int tag) const;
  /// Check to see if a specific instance of a group exists
  bool hasGroup(int num, int tag) const;
  /// Count the number of instance of a group
  size_t groupCount(int tag) const;

  /// Clear all fields from the map
  void clear();
  /// Swap contents with another FieldMap
  void swap(FieldMap &rhs) noexcept;
  /// Check if map contains any fields
  bool isEmpty();

  size_t totalFields() const;

  std::string &calculateString(std::string &) const;

  int calculateLength(
      int beginStringField = FIELD::BeginString,
      int bodyLengthField = FIELD::BodyLength,
      int checkSumField = FIELD::CheckSum) const;

  int calculateTotal(int checkSumField = FIELD::CheckSum) const;

  iterator begin() { return m_fields.begin(); }
  iterator end() { return m_fields.end(); }
  const_iterator begin() const { return m_fields.begin(); }
  const_iterator end() const { return m_fields.end(); }
  g_iterator g_begin() { return m_groups.begin(); }
  g_iterator g_end() { return m_groups.end(); }
  g_const_iterator g_begin() const { return m_groups.begin(); }
  g_const_iterator g_end() const { return m_groups.end(); }

  virtual FieldMap *cloneInto(GroupArena &arena) const {
    void *p = arena.allocate();
    return new (p) FieldMap(*this);
  }

protected:
  friend class Message;

  GroupArena &getArena() {
    if (!m_arena) m_arena = std::make_unique<GroupArena>();
    return *m_arena;
  }

  void addField(const FieldBase &field) {
    Fields::iterator iter = findPositionFor(field.getTag());
    if (iter == m_fields.end()) {
      m_fields.push_back(field);
    } else {
      m_fields.insert(iter, field);
    }
  }

  // used to find data length fields during message decoding
  // message fields are not yet sorted so regular find*** functions might return wrong results
  const FieldBase &reverse_find(int tag) const {
    Fields::const_reverse_iterator field = std::find_if(m_fields.rbegin(), m_fields.rend(), finder(tag));
    if (field == m_fields.rend()) {
      throw FieldNotFound(tag);
    }

    return *field;
  }

  void appendField(const FieldBase &field) { m_fields.push_back(field); }
  void appendField(FieldBase &&field) { m_fields.push_back(std::move(field)); }

  void sortFields() {
    if (!std::is_sorted(m_fields.begin(), m_fields.end(), sorter(m_order)))
      std::sort(m_fields.begin(), m_fields.end(), sorter(m_order));
  }

private:
  Fields::const_iterator findTag(int tag) const { return lookup(m_fields.begin(), m_fields.end(), tag); }

  Fields::iterator findTag(int tag) { return lookup(m_fields.begin(), m_fields.end(), tag); }

  template <typename Iterator> Iterator lookup(Iterator begin, Iterator end, int tag) const {
#if defined(__SUNPRO_CC)
    std::size_t numElements;
    std::distance(begin, end, numElements);
#else
    std::size_t numElements = std::distance(begin, end);
#endif
    if (numElements < 16) {
      return std::find_if(begin, end, finder(tag));
    }

    Iterator field = std::lower_bound(begin, end, tag, sorter(m_order));
    if (field != end && field->getTag() == tag) {
      return field;
    }

    return end;
  }

  Fields::iterator findPositionFor(int tag) {
    if (m_fields.empty()) {
      return m_fields.end();
    }

    const FieldBase &lastField = m_fields.back();
    if (m_order(lastField.getTag(), tag) || lastField.getTag() == tag) {
      return m_fields.end();
    }

    return std::upper_bound(m_fields.begin(), m_fields.end(), tag, sorter(m_order));
  }

  Fields m_fields;
  Groups m_groups;
  message_order m_order;
  std::unique_ptr<GroupArena> m_arena;
};
/*! @} */
} // namespace FIX

#define FIELD_SET(MAP, FIELD)                                                                                          \
  bool isSet(const FIELD &field) const { return (MAP).isSetField(field); }                                             \
  void set(const FIELD &field) { (MAP).setField(field); }                                                              \
  FIELD &get(FIELD &field) const { return (FIELD &)(MAP).getField(field); }                                            \
  bool getIfSet(FIELD &field) const { return (MAP).getFieldIfSet(field); }

#define FIELD_GET_PTR(MAP, FLD) (const FIX::FLD *)MAP.getFieldPtr(FIX::FIELD::FLD)
#define FIELD_GET_REF(MAP, FLD) (const FIX::FLD &)MAP.getFieldRef(FIX::FIELD::FLD)
#define FIELD_THROW_IF_NOT_FOUND(MAP, FLD)                                                                             \
  if (!(MAP).isSetField(FIX::FIELD::FLD))                                                                              \
  throw FieldNotFound(FIX::FIELD::FLD)
#endif // FIX_FIELDMAP
