#pragma once

#include <string>
#include <vector>

namespace hamarc {

enum class Command {
  kNone = 0,
  kCreate,
  kList,
  kExtract,
  kAppend,
  kDelete,
  kConcatenate
};

struct HammingParameters {
  int data_bits = 8;
  int parity_bits = 4;
};

struct ParsedOptions {
  Command command = Command::kNone;

  std::string archive_path;
  std::vector<std::string> files;

  HammingParameters hamming;

  bool show_help = false;
};

bool ParseCommandLine(int argc, const char* const argv[], ParsedOptions& options);

}  // namespace hamarc
