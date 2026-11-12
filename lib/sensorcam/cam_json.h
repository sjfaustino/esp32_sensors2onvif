// Writes JSON into a string. It adds the commas, quotes and escapes, so every reply is valid JSON whatever the text in it.
//
//   std::string out;
//   CamJson j(out);
//   j.object().str("name", name).num("size", size).array("files").str(file).end().end().finish();
//
// It only appends to the string it is given, so a caller can reserve the room first and check that the memory is there.
#pragma once
#include <stdio.h>
#include <string>

class CamJson {
 public:
  explicit CamJson(std::string& out) : out_(out) {}

  // Values: in an array, or as a whole reply.
  CamJson& str(const std::string& v) {
    comma();
    quoted(v);
    return *this;
  }
  CamJson& num(long long v) {
    comma();
    out_ += std::to_string(v);
    return *this;
  }
  CamJson& flag(bool v) {
    comma();
    out_ += v ? "true" : "false";
    return *this;
  }
  CamJson& null() {
    comma();
    out_ += "null";
    return *this;
  }
  CamJson& object() { return open('{', '}'); }
  CamJson& array() { return open('[', ']'); }

  // Members of an object.
  CamJson& key(const char* k) {
    comma();
    quoted(k);
    out_ += ':';
    comma_ = false;
    return *this;
  }
  CamJson& str(const char* k, const std::string& v) { return key(k).str(v); }
  CamJson& num(const char* k, long long v) { return key(k).num(v); }
  CamJson& flag(const char* k, bool v) { return key(k).flag(v); }
  CamJson& null(const char* k) { return key(k).null(); }
  CamJson& object(const char* k) { return key(k).object(); }
  CamJson& array(const char* k) { return key(k).array(); }
  // null when the value is not known.
  CamJson& num(const char* k, bool known, long long v) { return known ? num(k, v) : null(k); }
  CamJson& str(const char* k, bool known, const std::string& v) { return known ? str(k, v) : null(k); }

  CamJson& flag(const char* k, const char* v) = delete;  // a text would turn silently into true

  CamJson& end() {  // closes the innermost object or array
    if (!open_.empty()) {
      out_ += open_.back();
      open_.pop_back();
    }
    comma_ = true;
    return *this;
  }
  void finish() {  // closes whatever is still open and ends the reply with a line break
    while (!open_.empty()) end();
    out_ += '\n';
  }

 private:
  CamJson& open(char opening, char closing) {
    comma();
    out_ += opening;
    open_ += closing;
    comma_ = false;
    return *this;
  }
  void comma() {
    if (comma_) out_ += ',';
    comma_ = true;
  }
  void quoted(const std::string& s) {
    out_ += '"';
    for (char ch : s) {
      unsigned char u = (unsigned char)ch;
      if (ch == '"' || ch == '\\') {
        out_ += '\\';
        out_ += ch;
      } else if (ch == '\n') {
        out_ += "\\n";
      } else if (ch == '\r') {
        out_ += "\\r";
      } else if (ch == '\t') {
        out_ += "\\t";
      } else if (u < 0x20) {
        char esc[8];
        snprintf(esc, sizeof(esc), "\\u%04x", u);
        out_ += esc;
      } else {
        out_ += ch;
      }
    }
    out_ += '"';
  }

  std::string& out_;
  std::string open_;  // the closing bracket of each open object or array, innermost last
  bool comma_ = false;
};

// The whole reply for one text, as for an error message.
inline std::string camJsonError(const std::string& message) {
  std::string out;
  CamJson(out).object().flag("ok", false).str("error", message).finish();
  return out;
}
