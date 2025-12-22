#ifndef NARGPARSE_ARGPARSER_H
#define NARGPARSE_ARGPARSER_H

#include <cstddef>

namespace nargparse {

typedef void* ArgumentParser;


enum Nargs {
  kNargsOptional,
  kNargsRequired,
  kNargsZeroOrMore,
  kNargsOneOrMore
};

ArgumentParser CreateParser(const char* program_name);
ArgumentParser CreateParser(const char* program_name, std::size_t max_string_len);
void FreeParser(ArgumentParser parser);

void AddFlag(ArgumentParser parser, const char* short_name,
             const char* long_name, bool* out_value,
             const char* description,
             bool default_value = false);


void AddArgument(ArgumentParser parser, int* first_value_out,
                 const char* name, Nargs nargs = kNargsRequired,
                 bool (*validator)(const int&) = nullptr,
                 const char* error_hint = nullptr);

void AddArgument(ArgumentParser parser, float* first_value_out,
                 const char* name, Nargs nargs = kNargsRequired,
                 bool (*validator)(const float&) = nullptr,
                 const char* error_hint = nullptr);

void AddArgument(ArgumentParser parser, char (*first_value_out)[],
                 const char* name, Nargs nargs = kNargsRequired,
                 bool (*validator)(const char* const&) = nullptr,
                 const char* error_hint = nullptr);


void AddArgument(ArgumentParser parser, const char* short_name,
                 const char* long_name, int* first_value_out,
                 const char* description, Nargs nargs = kNargsRequired,
                 bool (*validator)(const int&) = nullptr, const char* error_hint = nullptr);

void AddArgument(ArgumentParser parser, const char* short_name,
                 const char* long_name, float* first_value_out,
                 const char* description, Nargs nargs = kNargsRequired,
                 bool (*validator)(const float&) = nullptr,
                 const char* error_hint = nullptr);

void AddArgument(ArgumentParser parser, const char* short_name,
                 const char* long_name, char (*first_value_out)[],
                 const char* description, Nargs nargs = kNargsRequired,
                 bool (*validator)(const char* const&) = nullptr,
                 const char* error_hint = nullptr);



void AddHelp(ArgumentParser parser);
void PrintHelp(ArgumentParser parser);

bool Parse(ArgumentParser parser, int argc, const char* const* argv);

int  GetRepeatedCount(ArgumentParser parser, const char* logical_name);

bool GetRepeated(ArgumentParser parser, const char* logical_name,
                 int index, int* out);

bool GetRepeated(ArgumentParser parser, const char* logical_name,
                 int index, float* out);

bool GetRepeated(ArgumentParser parser, const char* logical_name,
                 int index, const char** out);

} // namespace nargparse

#endif 
