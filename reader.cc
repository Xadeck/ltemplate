#include "xdk/ltemplate/reader.h"
#include "absl/base/macros.h"
#include "absl/strings/match.h"
#include "absl/strings/strip.h"

namespace xdk {
namespace ltemplate {
namespace {

constexpr char kOpeningExpression[] = {"{{"};
constexpr char kClosingExpression[] = {"}}"};
LazyRE2 kOpeningStatement = {R"RE({%-?|\n *{%-)RE"};
LazyRE2 kClosingStatement = {R"RE(-%} *\n|-?%})RE"};

template <size_t N>
const char *Produce(const char (&literal)[N], size_t *size) {
  *size = N - 1;
  return literal;
}

bool IsQuote(char c) { return c == '"' || c == '\''; }

} // namespace

Reader::Reader(const char *data, size_t size) : source_(data, size) {}

bool Reader::Match(size_t size, char prefix) const {
  return size >= source_.size() || source_[size] == prefix;
}

bool Reader::Match(size_t size, const char prefix[]) const {
  return size >= source_.size() ||
         absl::StartsWith(source_.substr(size), prefix);
}

bool Reader::Match(size_t size, const LazyRE2 &re, absl::string_view *match,
                   int n_match) const {
  return size >= source_.size() || re->Match(source_, size, source_.size(),
                                             RE2::ANCHOR_START, match, n_match);
}

bool Reader::TryConsume(const char prefix[]) {
  return absl::ConsumePrefix(&source_, prefix);
}

bool Reader::TryConsume(const LazyRE2 &re) {
  return RE2::Consume(&source_, *re);
}

const char *Reader::Read(lua_State *L, void *data, size_t *size) {
  return reinterpret_cast<Reader *>(data)->Read(L, size);
}

const char *Reader::Consume(size_t size) {
  const char *read = source_.data();
  source_.remove_prefix(size);
  return read;
}
const char *Reader::Read(lua_State *L, size_t *size) {
  switch (mode_) {
  case Mode::BEGIN:
    if (TryConsume(kOpeningExpression)) {
      return mode_ = Mode::EXPRESSION, Produce("_e(", size);
    }
    if (TryConsume(kOpeningStatement)) {
      return mode_ = Mode::STATEMENT, Produce(" ", size);
    }
    if (!source_.empty()) {
      return mode_ = Mode::TEXT, Produce("_s([[", size);
    }
    return nullptr;
  case Mode::TEXT:
    for (*size = 0;
         !Match(*size, kOpeningExpression) && !Match(*size, kOpeningStatement);
         ++*size) {
      if (source_[*size] == '\\' && *size + 1 < source_.size()) {
        ++*size;
      }
    }
    return mode_ = Mode::TEXT_END, Consume(*size);
  case Mode::TEXT_END:
    return mode_ = Mode::BEGIN, Produce("]])", size);
  case Mode::EXPRESSION:
    for (*size = 0; !Match(*size, kClosingExpression); ++*size) {
      if (IsQuote(delimiter_ = source_[*size])) {
        return mode_ = Mode::STRING,     //
               from_ = Mode::EXPRESSION, //
               Consume(++*size);
      }
    }
    if (*size || source_.empty()) {
      return mode_ = Mode::EXPRESSION_END, Consume(*size);
    }
    ABSL_FALLTHROUGH_INTENDED;
  case Mode::EXPRESSION_END:
    TryConsume(kClosingExpression);
    return mode_ = Mode::BEGIN, Produce(")", size);
  case Mode::STATEMENT:
    for (*size = 0; !Match(*size, kClosingStatement); ++*size) {
      if (IsQuote(delimiter_ = source_[*size])) {
        return mode_ = Mode::STRING,    //
               from_ = Mode::STATEMENT, //
               Consume(++*size);
      }
    }
    if (*size || source_.empty()) {
      return mode_ = Mode::STATEMENT_END, Consume(*size);
    }
    ABSL_FALLTHROUGH_INTENDED;
  case Mode::STATEMENT_END:
    TryConsume(kClosingStatement);
    return mode_ = Mode::BEGIN, Produce(" ", size);
  case Mode::STRING:
    for (*size = 0; *size < source_.size(); ++*size) {
      if (source_[*size] == delimiter_) {
        return mode_ = from_, Consume(++*size);
      }
      if (source_[*size] == '\\' && *size + 1 < source_.size()) {
        ++*size;
      }
    }
    return mode_ = from_, Consume(*size);
  }
}

} // namespace ltemplate
} // namespace xdk
