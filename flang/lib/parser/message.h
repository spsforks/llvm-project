// Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef FORTRAN_PARSER_MESSAGE_H_
#define FORTRAN_PARSER_MESSAGE_H_

// Defines a representation for sequences of compiler messages.
// Supports nested contextualization.

#include "char-block.h"
#include "char-set.h"
#include "provenance.h"
#include "../common/idioms.h"
#include "../common/reference-counted.h"
#include <cstddef>
#include <cstring>
#include <forward_list>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

namespace Fortran::parser {

// Use "..."_err_en_US and "..."_en_US literals to define the static
// text and fatality of a message.
class MessageFixedText {
public:
  constexpr MessageFixedText(
      const char str[], std::size_t n, bool isFatal = false)
    : text_{str, n}, isFatal_{isFatal} {}
  constexpr MessageFixedText(const MessageFixedText &) = default;
  constexpr MessageFixedText(MessageFixedText &&) = default;
  constexpr MessageFixedText &operator=(const MessageFixedText &) = default;
  constexpr MessageFixedText &operator=(MessageFixedText &&) = default;

  const CharBlock &text() const { return text_; }
  bool isFatal() const { return isFatal_; }

private:
  CharBlock text_;
  bool isFatal_{false};
};

inline namespace literals {
constexpr MessageFixedText operator""_en_US(const char str[], std::size_t n) {
  return MessageFixedText{str, n, false /* not fatal */};
}

constexpr MessageFixedText operator""_err_en_US(
    const char str[], std::size_t n) {
  return MessageFixedText{str, n, true /* fatal */};
}
}  // namespace literals

class MessageFormattedText {
public:
  MessageFormattedText(MessageFixedText, ...);
  MessageFormattedText(const MessageFormattedText &) = default;
  MessageFormattedText(MessageFormattedText &&) = default;
  MessageFormattedText &operator=(const MessageFormattedText &) = default;
  MessageFormattedText &operator=(MessageFormattedText &&) = default;
  const std::string &string() const { return string_; }
  bool isFatal() const { return isFatal_; }
  std::string MoveString() { return std::move(string_); }

private:
  std::string string_;
  bool isFatal_{false};
};

// Represents a formatted rendition of "expected '%s'"_err_en_US
// on a constant text or a set of characters.
class MessageExpectedText {
public:
  MessageExpectedText(const char *s, std::size_t n)
    : u_{CharBlock{s, n == std::string::npos ? std::strlen(s) : n}} {}
  constexpr explicit MessageExpectedText(CharBlock cb) : u_{cb} {}
  constexpr explicit MessageExpectedText(char ch) : u_{SetOfChars{ch}} {}
  constexpr explicit MessageExpectedText(SetOfChars set) : u_{set} {}
  MessageExpectedText(const MessageExpectedText &) = default;
  MessageExpectedText(MessageExpectedText &&) = default;
  MessageExpectedText &operator=(const MessageExpectedText &) = default;
  MessageExpectedText &operator=(MessageExpectedText &&) = default;

  std::string ToString() const;
  void Incorporate(const MessageExpectedText &);

private:
  std::variant<CharBlock, SetOfChars> u_;
};

class Message : public common::ReferenceCounted<Message> {
public:
  using Reference = common::CountedReference<Message>;

  Message(const Message &) = default;
  Message(Message &&) = default;
  Message &operator=(const Message &) = default;
  Message &operator=(Message &&) = default;

  Message(ProvenanceRange pr, const MessageFixedText &t)
    : location_{pr}, text_{t} {}
  Message(ProvenanceRange pr, const MessageFormattedText &s)
    : location_{pr}, text_{std::move(s)} {}
  Message(ProvenanceRange pr, MessageFormattedText &&s)
    : location_{pr}, text_{std::move(s)} {}
  Message(ProvenanceRange pr, const MessageExpectedText &t)
    : location_{pr}, text_{t} {}

  Message(CharBlock csr, const MessageFixedText &t)
    : location_{csr}, text_{t} {}
  Message(CharBlock csr, const MessageFormattedText &s)
    : location_{csr}, text_{std::move(s)} {}
  Message(CharBlock csr, MessageFormattedText &&s)
    : location_{csr}, text_{std::move(s)} {}
  Message(CharBlock csr, const MessageExpectedText &t)
    : location_{csr}, text_{t} {}

  bool attachmentIsContext() const { return attachmentIsContext_; }
  Reference attachment() const { return attachment_; }

  void SetContext(Message *c) {
    attachment_ = c;
    attachmentIsContext_ = true;
  }
  void Attach(Message *);
  template<typename... A> void Attach(A &&... args) {
    Attach(new Message{std::forward<A>(args)...});  // reference-counted
  }

  bool SortBefore(const Message &that) const;
  bool IsFatal() const;
  std::string ToString() const;
  ProvenanceRange GetProvenanceRange(const CookedSource &) const;
  void Emit(
      std::ostream &, const CookedSource &, bool echoSourceLine = true) const;

  void Incorporate(Message &);

private:
  bool AtSameLocation(const Message &) const;

  std::variant<ProvenanceRange, CharBlock> location_;
  std::variant<MessageFixedText, MessageFormattedText, MessageExpectedText>
      text_;
  bool attachmentIsContext_{false};
  Reference attachment_;
};

class Messages {
public:
  Messages() {}
  Messages(Messages &&that) : messages_{std::move(that.messages_)} {
    if (!messages_.empty()) {
      last_ = that.last_;
      that.last_ = that.messages_.before_begin();
    }
  }
  Messages &operator=(Messages &&that) {
    messages_ = std::move(that.messages_);
    if (messages_.empty()) {
      last_ = messages_.before_begin();
    } else {
      last_ = that.last_;
      that.last_ = that.messages_.before_begin();
    }
    return *this;
  }

  bool empty() const { return messages_.empty(); }

  Message &Put(Message &&m) {
    last_ = messages_.emplace_after(last_, std::move(m));
    return *last_;
  }

  template<typename... A> Message &Say(A &&... args) {
    last_ = messages_.emplace_after(last_, std::forward<A>(args)...);
    return *last_;
  }

  void Annex(Messages &that) {
    if (!that.messages_.empty()) {
      messages_.splice_after(last_, that.messages_);
      last_ = that.last_;
      that.last_ = that.messages_.before_begin();
    }
  }

  void Restore(Messages &&that) {
    that.Annex(*this);
    *this = std::move(that);
  }

  void Incorporate(Messages &);
  void Copy(const Messages &);
  void Emit(std::ostream &, const CookedSource &cooked,
      bool echoSourceLines = true) const;

  bool AnyFatalError() const;

private:
  std::forward_list<Message> messages_;
  std::forward_list<Message>::iterator last_{messages_.before_begin()};
};

}  // namespace Fortran::parser
#endif  // FORTRAN_PARSER_MESSAGE_H_
