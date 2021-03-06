//
// Copyright (c) 2014, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the University of Illinois/NCSA Open
// Source License found in the LICENSE file in the root directory of this
// source tree. An additional grant of patent rights can be found in the
// PATENTS file in the same directory.
//

#include "JSObjects.h"

#include <cassert>
#include <csetjmp>

//
// JSObject
//

JSObject const *JSObject::traverse(std::string const &path) const {
  char const *cpath;
  char const *bpath;
  JSObject const *obj = this;

  if (obj == nullptr)
    return nullptr;

  if (path.empty())
    return this;

  cpath = path.c_str();

  while (*cpath != '\0') {
    if (*cpath == '[') {
      if (JSArray const *a = JSCastTo<JSArray>(obj)) {
        char *end;
        unsigned long index = strtoul(cpath + 1, &end, 0);
        if (index >= a->count())
          return nullptr;
        if (*end != ']')
          return nullptr;

        obj = a->value(index);
        cpath = end + 1;
      } else {
        return nullptr;
      }
    } else if (obj == this || *cpath == '.') {
      if (JSDictionary const *d = JSCastTo<JSDictionary>(obj)) {
        if (*cpath == '.') {
          cpath++;
        }
        bpath = cpath;
        while (*cpath != '\0') {
          if (*cpath == '.' || *cpath == '[') {
            if (cpath == bpath || *(cpath - 1) != '\\')
              break;
          }

          cpath++;
        }

        obj = d->value(std::string(bpath, cpath - bpath));
        if (obj == nullptr)
          return nullptr;
      }
    } else {
      return nullptr;
    }
  }

  return obj;
}

void JSObject::dump(FILE *fp, size_t indent) const {
  if (fp != nullptr) {
    dump1(fp, indent, indent);
    fputc('\n', fp);
  }
}

inline void JSObject::Indent(FILE *fp, size_t n) {
  fprintf(fp, "%*s", static_cast<int>(n * 4), "");
}

inline std::string JSObject::QuoteString(std::string const &s) {
  std::string o;

  for (size_t n = 0; n < s.length(); n++) {
    if (static_cast<uint8_t>(s[n]) < 32) {
      o += '\\';
      o += "0123456789abcdef"[(s[n] >> 4) & 0xf];
      o += "0123456789abcdef"[(s[n] >> 0) & 0xf];
    } else {
      if (s[n] == '\"' || s[n] == '\\') {
        o += '\\';
      }
      o += s[n];
    }
  }

  return o;
}

//
// JSInteger
//

void JSInteger::dump1(FILE *fp, size_t indent, size_t) const {
  JSObject::Indent(fp, indent);
#ifdef _MSC_VER
  fprintf(fp, "%I64d", (__int64)value());
#else
  fprintf(fp, "%lld", (long long)value());
#endif
}

//
// JSReal
//

void JSReal::dump1(FILE *fp, size_t indent, size_t) const {
  JSObject::Indent(fp, indent);
  fprintf(fp, "%g", value());
}

//
// JSString
//

void JSString::dump1(FILE *fp, size_t indent, size_t) const {
  JSObject::Indent(fp, indent);
  fprintf(fp, "\"%s\"", JSObject::QuoteString(value()).c_str());
}

//
// JSBoolean
//

void JSBoolean::dump1(FILE *fp, size_t indent, size_t) const {
  JSObject::Indent(fp, indent);
  fprintf(fp, "%s", value() ? "true" : "false");
}

//
// JSNull
//

void JSNull::dump1(FILE *fp, size_t indent, size_t) const {
  JSObject::Indent(fp, indent);
  fprintf(fp, "null");
}

//
// JSArray
//

void JSArray::dump1(FILE *fp, size_t, size_t cindent) const {
  fputc('[', fp);
  fputc(empty() ? ' ' : '\n', fp);

  for (auto i = begin(); i != end(); ++i) {
    if (i != begin()) {
      fprintf(fp, ",\n");
    }
    auto t = i->get()->type();
    if (t == JSObject::kTypeArray || t == JSObject::kTypeDictionary)
      JSObject::Indent(fp, cindent + 1);
    i->get()->dump1(fp, cindent + 1, cindent + 1);
  }

  if (!empty()) {
    fputc('\n', fp);
    JSObject::Indent(fp, cindent);
  }

  fputc(']', fp);
}

//
// JSDictionary
//

void JSDictionary::dump1(FILE *fp, size_t, size_t cindent) const {
  fputc('{', fp);
  fputc(empty() ? ' ' : '\n', fp);

  for (auto i = begin(); i != end(); ++i) {
    if (i != begin()) {
      fprintf(fp, ",\n");
    }
    JSObject::Indent(fp, cindent + 1);
    fprintf(fp, "\"%s\" : ", JSObject::QuoteString(*i).c_str());
    value(*i)->dump1(fp, 0, cindent + 1);
  }

  if (!empty()) {
    fputc('\n', fp);
    JSObject::Indent(fp, cindent);
  }

  fputc('}', fp);
}

//
// JSDictionary::Parser
//

static int _json_string(void *obj, char const *m, char const *s);
static int _json_int(void *obj, char const *m, int i);
static int _json_double(void *obj, char const *m, double d);
static int _json_bool(void *obj, char const *m, int b);
static int _json_null(void *obj, char const *m);
static int _json_new_obj(void *obj, char const *m, void **obj_new,
                         struct json_cb_t **cb);
static int _json_obj(void *obj, void *o);
static int _json_new_array(void *obj, char const *m, void **array_new,
                           struct json_cb_t **cb);
static int _json_array(void *obj, void *a);
static void *_json_delete(void *obj);

static struct json_cb_t _json_vb = {_json_string,    _json_int,   _json_double,
                                    _json_bool,      _json_null,

                                    _json_new_obj,   _json_obj,

                                    _json_new_array, _json_array,

                                    _json_delete};

static int _json_string(void *obj, char const *m, char const *s) {
  if (JSArray *a = JSCastTo<JSArray>(reinterpret_cast<JSObject *>(obj))) {
    a->append(JSString::New(s));
  } else if (JSDictionary *d =
                 JSCastTo<JSDictionary>(reinterpret_cast<JSObject *>(obj))) {
    assert(m != nullptr && "key is null");
    d->set(m, JSString::New(s));
  } else {
    assert(0 && "this shouldn't happen");
  }
  return 0;
}

static int _json_int(void *obj, char const *m, int i) {
  if (JSArray *a = JSCastTo<JSArray>(reinterpret_cast<JSObject *>(obj))) {
    a->append(JSInteger::New(i));
  } else if (JSDictionary *d =
                 JSCastTo<JSDictionary>(reinterpret_cast<JSObject *>(obj))) {
    assert(m != nullptr && "key is null");
    d->set(m, JSInteger::New(i));
  } else {
    assert(0 && "this shouldn't happen");
  }
  return 0;
}

static int _json_double(void *obj, char const *m, double r) {
  if (JSArray *a = JSCastTo<JSArray>(reinterpret_cast<JSObject *>(obj))) {
    a->append(JSReal::New(r));
  } else if (JSDictionary *d =
                 JSCastTo<JSDictionary>(reinterpret_cast<JSObject *>(obj))) {
    assert(m != nullptr && "key is null");
    d->set(m, JSReal::New(r));
  } else {
    assert(0 && "this shouldn't happen");
  }
  return 0;
}

static int _json_bool(void *obj, char const *m, int b) {
  if (JSArray *a = JSCastTo<JSArray>(reinterpret_cast<JSObject *>(obj))) {
    a->append(JSBoolean::New(b != 0));
  } else if (JSDictionary *d =
                 JSCastTo<JSDictionary>(reinterpret_cast<JSObject *>(obj))) {
    assert(m != nullptr && "key is null");
    d->set(m, JSBoolean::New(b != 0));
  } else {
    assert(0 && "this shouldn't happen");
  }
  return 0;
}

static int _json_null(void *obj, char const *m) {
  if (JSArray *a = JSCastTo<JSArray>(reinterpret_cast<JSObject *>(obj))) {
    a->append(JSNull::New());
  } else if (JSDictionary *d =
                 JSCastTo<JSDictionary>(reinterpret_cast<JSObject *>(obj))) {
    assert(m != nullptr && "key is null");
    d->set(m, JSNull::New());
  } else {
    assert(0 && "this shouldn't happen");
  }
  return 0;
}

static int _json_new_obj(void *obj, char const *m, void **obj_new,
                         struct json_cb_t **cb) {
  JSDictionary *dict = JSDictionary::New();
  if (m == nullptr) {
    if (*reinterpret_cast<JSObject **>(obj) == nullptr) {
      *reinterpret_cast<JSObject **>(obj) = dict;
    } else if (JSArray *a =
                   JSCastTo<JSArray>(reinterpret_cast<JSObject *>(obj))) {
      a->append(dict);
    } else {
      assert(0 && "this shouldn't happen");
    }
  } else if (JSDictionary *d =
                 JSCastTo<JSDictionary>(reinterpret_cast<JSObject *>(obj))) {
    assert(m != nullptr && "key is null");
    d->set(m, dict);
  } else {
    assert(0 && "this shouldn't happen");
  }
  *obj_new = dict;
  *cb = &_json_vb;
  return 0;
}

static int _json_obj(void *obj, void *o) { return 0; }

static int _json_new_array(void *obj, char const *m, void **array_new,
                           struct json_cb_t **cb) {
  JSArray *array = JSArray::New();
  if (JSArray *a = JSCastTo<JSArray>(reinterpret_cast<JSObject *>(obj))) {
    a->append(array);
  } else if (JSDictionary *d =
                 JSCastTo<JSDictionary>(reinterpret_cast<JSObject *>(obj))) {
    assert(m != nullptr && "key is null");
    d->set(m, array);
  } else {
    assert(0 && "this shouldn't happen");
  }
  *array_new = array;
  *cb = &_json_vb;
  return 0;
}

static int _json_array(void *obj, void *a) { return 0; }

static void *_json_delete(void *obj) { return 0; }

typedef std::function<bool(unsigned, unsigned, std::string const &)>
    ErrorFunction;

struct ErrorContext {
  ErrorFunction const &ef;
  jmp_buf jb;

  ErrorContext(ErrorFunction const &func) : ef(func) {}
};

static int _json_err(void *err_data, unsigned int line, unsigned int column,
                     char const *error) {
  ErrorContext *ctx = reinterpret_cast<ErrorContext *>(err_data);

  if (!ctx->ef(line, column, error)) {
    longjmp(ctx->jb, 1);
  }

  return 0;
}

JSDictionary *JSDictionary::Parse(
    std::string const &path,
    std::function<bool(unsigned, unsigned, std::string const &)> const &error) {
  if (path.empty())
    return nullptr;

  FILE *fp = fopen(path.c_str(), "rt");
  if (fp == nullptr)
    return nullptr;

  JSDictionary *root = Parse(fp, error);
  fclose(fp);

  return root;
}

JSDictionary *JSDictionary::Parse(std::string const &path) {
  return Parse(path, [](unsigned, unsigned,
                        std::string const &) -> bool { return false; });
}

JSDictionary *JSDictionary::Parse(
    FILE *fp,
    std::function<bool(unsigned, unsigned, std::string const &)> const &error) {
  if (fp == nullptr)
    return nullptr;

  ErrorContext ectx(error);
  JSDictionary *root = nullptr;

  if (setjmp(ectx.jb) == 1) {
    if (root != nullptr) {
      delete root;
    }
    return nullptr;
  }

  json_fparse(fp, &_json_vb, &root, _json_err, &ectx);
  return root;
}

JSDictionary *JSDictionary::Parse(FILE *fp) {
  return Parse(fp, [](unsigned, unsigned,
                      std::string const &) -> bool { return false; });
}
