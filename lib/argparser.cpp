#include "argparser.h"

#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace nargparse {

static void* PerformSafeMalloc(std::size_t bytes) {
  void* p = std::malloc(bytes);
  if (p == nullptr) {
    std::abort();
  }
  return p;
}

static void* PerformSafeRealloc(void* old_ptr, std::size_t bytes){
  void* p = std::realloc(old_ptr, bytes);
  if (p == nullptr){
    std::abort();
  }
  return p;
}

static char* CopyString(const char* string){
  std::size_t size = std::strlen(string);
  char* p = static_cast<char*>(PerformSafeMalloc(size+1));
  std::memcpy(p, string, size+1);
  return p;
}

struct IntArray{
  int* data;
  int size;
  int cap;
};

static void PushIntArray(IntArray* arr, int val){
  if(arr->size == arr->cap){
    if(arr->cap == 0){
      arr->cap = 4;
    } else {
      arr->cap = (arr->cap * 2);
    }
    arr->data = static_cast<int*>(PerformSafeRealloc(arr->data, arr->cap*sizeof(int)));
  }
  arr->data[arr->size]= val;
  arr->size+=1;
}

struct FloatArray{
  float* data;
  int size;
  int cap;
};

static void PushFloatArray(FloatArray* arr, float val){
  if(arr->size == arr->cap){
    if(arr->cap == 0){
      arr->cap = 4;
    } else {
      arr->cap = (arr->cap * 2);
    }
    arr->data = static_cast<float*>(PerformSafeRealloc(arr->data, arr->cap*sizeof(float)));
  }
  arr->data[arr->size]= val;
  arr->size+=1;
}

struct StrArray{
  char** data;
  int size;
  int cap;
};

static bool PushAndCheckStrArray(StrArray* arr, const char* str, std::size_t max_len){
  std::size_t length = std::strlen(str);
  if(length >= max_len){
    return false;
  }
  if(arr->size == arr->cap){
    if(arr->cap == 0){
      arr->cap = 4;
    } else {
      arr->cap = (arr->cap * 2);
    }
    arr->data = static_cast<char**>(PerformSafeRealloc(arr->data, arr->cap*sizeof(char*)));
  }
  char* copy = static_cast<char*>(PerformSafeMalloc(length+1));
  std::memcpy(copy, str, length+1);

  arr->data[arr->size] = copy;
  arr->size+=1;
  return true;
}

static bool IsShortOption(const char* token){
  return token != nullptr && token[0] == '-' && 
         token[1] != '\0' && token[1] != '-';
}

static bool IsLongOption(const char* token) {
  return token != nullptr && token[0] == '-' && 
         token[1] == '-' && token[2] != '\0';
}

enum ValueKind {
  kKindFlag,
  kKindInt,
  kKindFloat,
  kKindString
};

enum ArgForm {
  kFormPositional,
  kFormNamed
};

struct FlagDef{
  const char* short_name;
  const char* long_name;
  const char* description;
  bool* out_ptr;
  bool default_value;
  bool current_value;
};

struct ArgDef{
  ValueKind kind;
  ArgForm form;

  const char* short_name;
  const char* long_name;
  const char* logical_name;

  Nargs nargs;
  void* first_out;

  void* validator;
  const char* error_hint;

  IntArray ints;
  FloatArray floats;
  StrArray strings;

  int occurrences;
};

struct Parser {
  const char* program;
  std::size_t max_string_len;

  ArgDef* args;
  int args_size;
  int args_cap;

  FlagDef* flags;
  int flags_size;
  int flags_cap;

  int help_index;
  bool help_requested;
};

static void EnsureArgsCapacity(Parser* parser){
  if (parser->args_size==parser->args_cap){
    if(parser->args_cap == 0){
      parser->args_cap = 8;
    }else{
      parser->args_cap = (parser->args_cap)*2;
    }
    parser->args = static_cast<ArgDef*>(PerformSafeRealloc(parser->args, 
                  static_cast<std::size_t>(parser->args_cap) * sizeof(ArgDef)));
  }
}

static void EnsureFlagsCapacity(Parser* parser){
  if (parser->flags_size==parser->flags_cap){
    if(parser->flags_cap == 0){
      parser->flags_cap = 8;
    }else{
      parser->flags_cap = (parser->flags_cap)*2;
    }
    parser->flags = static_cast<FlagDef*>(PerformSafeRealloc(parser->flags,
                  static_cast<std::size_t>(parser->flags_cap) * sizeof(FlagDef)));
  }
}

static bool ParseInt(const char* text, int* out) {
  if(text == nullptr || *text == '\0') {
    return false;
  }
  char* end_ptr = nullptr;
  long value = std::strtol(text, &end_ptr, 10);
  if (end_ptr == nullptr || *end_ptr != '\0') {
    return false;
  }
  *out = static_cast<int>(value);
  return true;
}

static bool ParseFloat(const char* text, float* out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  char* end_ptr = nullptr;
  double value = std::strtod(text, &end_ptr);
  if (end_ptr == nullptr || *end_ptr != '\0') {
    return false; 
  }
  *out = static_cast<float>(value);
  return true;
}

static bool ValidateInt(ArgDef* a, int v){
  if (a->validator == nullptr){
    return true;
  }
  bool (*fn)(const int&) = reinterpret_cast<bool (*)(const int&)>(a->validator);
  return fn(v);
}

static bool ValidateFloat(ArgDef* a, float v) {
  if (a->validator == nullptr){
    return true;
  }
  bool (*fn)(const float&) = reinterpret_cast<bool (*)(const float&)>(a->validator);
  return fn(v);
}

static bool ValidateString(ArgDef* a, const char* s){
  if (a->validator == nullptr) {
    return true;
  }
  bool (*fn)(const char* const&) = reinterpret_cast<bool (*)(const char* const&)>(a->validator);
  return fn(s);
}

static void MirrorFirstInt(ArgDef* a, int value) {
  if(a->first_out != nullptr && a->ints.size == 1){
    *static_cast<int*>(a->first_out) = value;
  }
}

static void MirrorFirstFloat(ArgDef* a, float value) {
  if(a->first_out != nullptr && a->floats.size == 1){
    *static_cast<float*>(a->first_out) = value;
  }
}

static void MirrorFirstString(Parser* parser, ArgDef* a, const char* value){
  if (a->first_out == nullptr) {
    return;
  }
  if(a->strings.size == 1 ){
    char (*buffer)[] = static_cast<char (*)[]>(a->first_out);
    std::strncpy(*buffer, value, parser->max_string_len - 1);
    (*buffer)[parser->max_string_len - 1] = '\0';
 }
}

static bool StoreInt(ArgDef* a, const char* token){
  int v = 0;
  if(!ParseInt(token, &v)) {
    return false;
  }
  if(!ValidateInt(a, v)) {
    return false;
  }
  PushIntArray(&a->ints, v);
  MirrorFirstInt(a, v);
  return true;
}

static bool StoreFloat(ArgDef* a, const char* token){
  float v = 0.0;
  if(!ParseFloat(token, &v)) {
    return false;
  }
  if(!ValidateFloat(a, v)) {
    return false;
  }
  PushFloatArray(&a->floats, v);
  MirrorFirstFloat(a, v);
  return true;
}

static bool StoreString(Parser* parser, ArgDef* a, const char* token){
  if (std::strlen(token) >= parser->max_string_len) {
    return false;
  }
  if (!ValidateString(a, token)) {
    return false;
  }
  if (!PushAndCheckStrArray(&a->strings, token, parser->max_string_len)) {
    return false;
  }
  const char* stored = a->strings.data[a->strings.size - 1];
  MirrorFirstString(parser, a, stored);
  return true;
}

static int CountArgs(const ArgDef* a) {
  if (a->kind == kKindInt){
    return a->ints.size;
  } else if (a->kind == kKindFloat){
    return a->floats.size;
  } else if (a->kind == kKindString){
    return a->strings.size;
  }
  return 0;
}

static ArgDef* FindByName(Parser* parser, const char* name) {
  for (int i = 0; i< parser->args_size; ++i){
    if(parser->args[i].logical_name != nullptr &&
       std::strcmp(parser->args[i].logical_name, name) == 0){
      return &parser->args[i];
    }
  }
  return nullptr;
} 

static FlagDef* FindFlag(Parser* parser, const char* token) {
  for (int i = 0; i < parser->flags_size; ++i) {
    FlagDef* f = &parser->flags[i];
    if ((f->short_name != nullptr && std::strcmp(f->short_name, token) == 0) ||
        (f->long_name  != nullptr && std::strcmp(f->long_name,  token) == 0)) {
      return f;
    }
  }
  return nullptr;
}

static ArgDef* FindNamedArg(Parser* parser, const char* token, const char** out_value){
  *out_value = nullptr;
  const char* equal_s = nullptr;
  if(IsLongOption(token)){
    equal_s = std::strchr(token, '=');
  }
  for(int i = 0; i < parser->args_size; ++i) {
    ArgDef* a = &parser->args[i];
    if(a->form != kFormNamed){
      continue;
    }
    if (a->short_name != nullptr && std::strcmp(a->short_name, token) == 0){
      return a;
    }
    if (a->long_name != nullptr){
      if(equal_s == nullptr) {
        if (std::strcmp(a->long_name, token) == 0){
          return a;
        }
      }else{
        std::size_t name_len = static_cast<std::size_t>(equal_s - token);
        if(std::strlen(a->long_name) == name_len &&
           std::strncmp(a->long_name, token, name_len) == 0){
          *out_value = equal_s + 1;
          return a;
        }
      }
    }
  }
  return nullptr;
}

static ArgDef* FindNextPositionalArg(Parser* parser) {
  for (int i =0; i < parser->args_size; ++i){
    ArgDef* a = &parser->args[i];
    if (a->form != kFormPositional){
      continue;
    }
    int used = CountArgs(a);
    if(a->nargs == kNargsOptional || a->nargs == kNargsRequired){
      if (used == 0) {
        return a;
      }
    }else{
      return a;
    }
  }
  return nullptr;
}

static void ResetFlags(Parser* parser) {
  for(int i = 0; i < parser->flags_size; ++i){
    parser->flags[i].current_value = parser->flags[i].default_value;
    if (parser->flags[i].out_ptr != nullptr) {
      *parser->flags[i].out_ptr = parser->flags[i].default_value;
    }
  }
}

static void FreeStringsInsideArg(ArgDef* a) {
  for (int j = 0; j < a->strings.size; ++j) {
    std::free(a->strings.data[j]);
  }
  a->strings.size = 0;
}

static void ResetArgs(Parser* parser) {
  for (int i = 0; i< parser->args_size; ++i){
    ArgDef* a = &parser->args[i];

    a->ints.size = 0;
    a->floats.size = 0;
    FreeStringsInsideArg(a);
    a->occurrences = 0;

    if (a->kind == kKindString && a->first_out != nullptr){
      char (*buf)[] = static_cast<char (*)[]>(a->first_out);
      (*buf)[0] = '\0';
    }
  }
}

static bool AreRequirementsMet(Parser* parser){
  for (int i = 0; i< parser->args_size; ++i){
    ArgDef* a = &parser->args[i];
    int count = CountArgs(a);

    if (a->nargs == kNargsRequired && count != 1){
      return false;
    }
    if (a->nargs == kNargsOneOrMore && count < 1){
      return false;
    }
  }
  return true;
}

static void InitializeArgs(ArgDef* a, ValueKind kind, ArgForm form,
                        const char* name, Nargs nargs, void* first_out, 
                        void* validator, const char* hint){
a->kind = kind;
a->form = form;
a->short_name = nullptr;
a->long_name = nullptr;
a->logical_name = name;
a->nargs = nargs;
a->first_out = first_out;
a->validator = validator;
a->error_hint = hint;

a->ints = {nullptr, 0, 0};
a->floats = {nullptr, 0, 0};
a->strings = {nullptr,  0 , 0};

a->occurrences = 0;
}

void AddFlag(ArgumentParser handle, const char* short_name,
             const char* long_name, bool* out_value,
             const char* description, bool default_value) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureFlagsCapacity(parser);

  FlagDef* f = &parser->flags[parser->flags_size];
  parser->flags_size += 1;

  f->short_name = short_name;
  f->long_name = long_name;
  f->description = description;
  f->out_ptr = out_value;
  f->default_value = default_value;
  f->current_value = default_value;

  if (out_value != nullptr) {
    *out_value = default_value;
  }
}

void AddHelp(ArgumentParser handle){
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureFlagsCapacity(parser);

  FlagDef* f = &parser->flags[parser->flags_size];
  parser->flags_size += 1;

  f->short_name = "-h";
  f->long_name = "--help";
  f->description = "Show help";
  f->out_ptr = nullptr;
  f->default_value = false;
  f->current_value = false;

  parser->help_index = parser->flags_size - 1;
}

void AddArgument(ArgumentParser handle, int* first_value_out,
                 const char* name, Nargs nargs,
                 bool (*validator)(const int&), const char* hint) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureArgsCapacity(parser);
  ArgDef* a = &parser->args[parser->args_size++];
  InitializeArgs(a, kKindInt, kFormPositional, name, nargs,
                 first_value_out, reinterpret_cast<void*>(validator), hint);
}

void AddArgument(ArgumentParser handle, float* first_value_out,
                 const char* name, Nargs nargs,
                 bool (*validator)(const float&), const char* hint) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureArgsCapacity(parser);
  ArgDef* a = &parser->args[parser->args_size++];
  InitializeArgs(a, kKindFloat, kFormPositional, name, nargs,
                 first_value_out, reinterpret_cast<void*>(validator), hint);
}

void AddArgument(ArgumentParser handle, char (*first_value_out)[],
                 const char* name, Nargs nargs,
                 bool (*validator)(const char* const&), const char* hint) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureArgsCapacity(parser);
  ArgDef* a = &parser->args[parser->args_size++];
  InitializeArgs(a, kKindString, kFormPositional, name, nargs,
                 reinterpret_cast<void*>(first_value_out),
                 reinterpret_cast<void*>(validator), hint);
  if (first_value_out) { 
    (*first_value_out)[0] = '\0'; 
  }
}

void AddArgument(ArgumentParser handle, const char* short_name,
                 const char* long_name, int* first_value_out,
                 const char* description, Nargs nargs,
                 bool (*validator)(const int&), const char* hint) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureArgsCapacity(parser);

  ArgDef* a = &parser->args[parser->args_size];
  parser->args_size += 1;

  
  InitializeArgs(a, kKindInt, kFormNamed, description, 
                 nargs, first_value_out,
                 reinterpret_cast<void*>(validator), hint);

  a->short_name = short_name;
  a->long_name  = long_name;
}

void AddArgument(ArgumentParser handle, const char* short_name,
                 const char* long_name, float* first_value_out,
                 const char* description, Nargs nargs,
                 bool (*validator)(const float&), const char* hint) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureArgsCapacity(parser);

  ArgDef* a = &parser->args[parser->args_size];
  parser->args_size += 1;
  
  InitializeArgs(a, kKindFloat, kFormNamed, description, 
                 nargs, first_value_out,
                 reinterpret_cast<void*>(validator), hint);

  a->short_name = short_name;
  a->long_name  = long_name;
}

void AddArgument(ArgumentParser handle, const char* short_name,
                 const char* long_name, char (*first_value_out)[],
                 const char* description, Nargs nargs,
                 bool (*validator)(const char* const&), const char* hint) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  EnsureArgsCapacity(parser);

  ArgDef* a = &parser->args[parser->args_size];
  parser->args_size += 1;

  
  InitializeArgs(a, kKindString, kFormNamed, description, 
                 nargs, reinterpret_cast<void*>(first_value_out),
                 reinterpret_cast<void*>(validator), hint);

  a->short_name = short_name;
  a->long_name  = long_name;
  
  if (first_value_out != nullptr) {
    (*first_value_out)[0] = '\0';
  }
}



int GetRepeatedCount(ArgumentParser handle, const char* name) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  ArgDef* a = FindByName(parser, name);
  if (a == nullptr){
    return 0;
  }
  return CountArgs(a);
}

bool GetRepeated(ArgumentParser handle, const char* name, int index, int* out) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  ArgDef* a = FindByName(parser, name);
  if (a == nullptr || a->kind != kKindInt) {
    return false;
  }
  if (index < 0 || index >= a->ints.size) {
    return false;
  }
  if (out != nullptr) {
    *out = a->ints.data[index];
  }
  return true;
}

bool GetRepeated(ArgumentParser handle, const char* logical_name, int index, float* out) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  ArgDef* a = FindByName(parser, logical_name);
  if (a == nullptr || a->kind != kKindFloat) {
    return false;
  }
  if (index < 0 || index >= a->floats.size) {
    return false;
  }
  if (out != nullptr) {
    *out = a->floats.data[index];
  }
  return true;
}

bool GetRepeated(ArgumentParser handle, const char* logical_name, int index, const char** out) {
  Parser* parser = reinterpret_cast<Parser*>(handle);
  ArgDef* a = FindByName(parser, logical_name);
  if (a == nullptr || a->kind != kKindString) {
    return false;
  }
  if (index < 0 || index >= a->strings.size) {
    return false;
  }
  if (out != nullptr) {
    *out = a->strings.data[index];
  }
  return true;
}

static bool HandleHelp(Parser* parser, const char* token) {
  if (parser->help_index < 0) {
    return false;
  }
  if (std::strcmp(token, "-h") == 0 || std::strcmp(token, "--help") == 0) {
    parser->help_requested = true;
    return true;
  }
  return false;
}

static bool HandleFlag(Parser* parser, const char* token) {
  FlagDef* f = FindFlag(parser, token);
  if (f == nullptr) {
    return false;
  }
  f->current_value = true;
  if (f->out_ptr != nullptr) {
    *f->out_ptr = true;
  }
  return true;
}

static bool TakeNamedValue(Parser* parser, ArgDef* a,
                          const char* value_from_equal, int argc,
                          const char* const* argv, int i, int* taken){
  const char* value_token = value_from_equal;
  if (value_token == nullptr) {
    if (i + 1 >= argc) {
      return false;
    }
    value_token = argv[i + 1];
    *taken = 2; 
  } else {
    *taken = 1; 
  }

  if (a->kind == kKindInt) {
    return StoreInt(a, value_token);
  } else if (a->kind == kKindFloat) {
    return StoreFloat(a, value_token);
  } else {
    return StoreString(parser, a, value_token);
  }
}

static int HandleNamedOption(Parser* parser, const char* token, int argc, const char* const* argv, int i) {
  const char* value_from_equal = nullptr;
  ArgDef* a = FindNamedArg(parser, token, &value_from_equal);
  if (a == nullptr) {
    return -1; 
  }
  if (a->nargs == kNargsOptional || a->nargs == kNargsRequired) {
    if (a->occurrences >= 1) {
      return -1;
    }
  }
  a->occurrences += 1;

  int taken = 0;
  if (!TakeNamedValue(parser, a, value_from_equal, argc, argv, i, &taken)) {
    return -1;
  }
  return taken;
}

static bool HandlePositional(Parser* parser, const char* token) {
  ArgDef* a = FindNextPositionalArg(parser);
  if (a == nullptr) {
    return false;
  }

  if (a->kind == kKindInt) {
    return StoreInt(a, token);
  } else if (a->kind == kKindFloat) {
    return StoreFloat(a, token);
  } else {
    return StoreString(parser, a, token);
  }
}

bool Parse(ArgumentParser handle, int argc, const char* const* argv){
  Parser* parser = reinterpret_cast<Parser*>(handle);

  ResetFlags(parser);
  ResetArgs(parser);
  parser->help_requested = false;
  int i = 1;
  while (i < argc){
    const char* token = argv[i];
    if(IsShortOption(token) || IsLongOption(token)) {
      if(HandleHelp(parser, token)){
        i++;
        continue;
      }
      if(HandleFlag(parser, token)){
        i++;
        continue;
      }
      int taken = HandleNamedOption(parser, token, argc, argv, i);
      if (taken < 0) {
        return false;
      }
      i += taken;
      continue;
    }
    if(!HandlePositional(parser, token)) {
      return false;
    }
    i+=1;
  }
  if(!AreRequirementsMet(parser)){
    return false;
  }
return true;
}

ArgumentParser CreateParser(const char* program_name) {
  return CreateParser(program_name, 128);
}

ArgumentParser CreateParser(const char* program_name, std::size_t max_string_len) {
  Parser* parser = static_cast<Parser*>(PerformSafeMalloc(sizeof(Parser)));
  if(program_name != nullptr){
    parser->program = CopyString(program_name);
  } else {
    parser->program = CopyString("program");
  }
  if(max_string_len != 0) {
    parser->max_string_len = max_string_len;
  } else {
    parser->max_string_len = 128;
  }
  parser->args = nullptr;
  parser->args_size = 0;
  parser->args_cap = 0;

  parser->flags = nullptr;
  parser->flags_size = 0;
  parser->flags_cap = 0;

  parser->help_index = -1;
  parser->help_requested = false;

  return reinterpret_cast<ArgumentParser>(parser);
}

void FreeParser(ArgumentParser handle) {
  if (handle == nullptr) {
    return;
  }
  Parser* parser = reinterpret_cast<Parser*>(handle);

  for(int i = 0; i<parser->args_size; ++i) {
    ArgDef* a = &parser->args[i];
    for (int j = 0; j < a->strings.size; ++j) {
      std::free(a->strings.data[j]);
    }
    std::free(a->strings.data);

    std::free(a->ints.data);
    std::free(a->floats.data);
  }
  std::free(parser->args);
  std::free(parser->flags);

  std::free(const_cast<char*>(parser->program));
  std::free(parser);
}

static const char* Safe(const char* s) { 
  return s ? s : ""; 
}

static const char* ArgTypeName(const ArgDef* a) {
  switch (a->kind) {
    case kKindInt:   return "int";
    case kKindFloat: return "float";
    default:         return "string";
  }
}

static void PrintUsageHeader(const Parser* parser) {
  std::printf("Usage: %s [options] [args]\n", parser->program);
  std::puts("Options and arguments:");
}

static void PrintSingleFlag(const FlagDef* f) {
  std::printf("  %s %s\t%s (default: %s)\n",
              Safe(f->short_name),
              Safe(f->long_name),
              Safe(f->description),
              f->default_value ? "true" : "false");
}

static void PrintFlagsSection(const Parser* parser) {
  for (int i = 0; i < parser->flags_size; ++i) {
    PrintSingleFlag(&parser->flags[i]);
  }
}

static void PrintSingleNamedArg(const ArgDef* a) {
  std::printf("  %s %s\t%s (%s)\n",
              Safe(a->short_name),
              Safe(a->long_name),
              Safe(a->logical_name),
              ArgTypeName(a));
}

static void PrintSinglePositionalArg(const ArgDef* a) {
  const char* name = a->logical_name ? a->logical_name : "<positional>";
  std::printf("  %s\t(%s %s)\n", name, "positional", ArgTypeName(a));
}

static void PrintArgsSection(const Parser* parser) {
  for (int i = 0; i < parser->args_size; ++i) {
    const ArgDef* a = &parser->args[i];
    if (a->form == kFormNamed) {
      PrintSingleNamedArg(a);
    } else {
      PrintSinglePositionalArg(a);
    }
  }
}

void PrintHelp(ArgumentParser handle) {
  const Parser* parser = reinterpret_cast<const Parser*>(handle);

  PrintUsageHeader(parser);
  PrintFlagsSection(parser);
  PrintArgsSection(parser);
}


} // namespace nargparse
