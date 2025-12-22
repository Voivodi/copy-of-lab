#include "hamarc_core.h"
#include "archiver.h"
#include "hamming_options.h"
#include "parse_args.h"


#include <iostream>

namespace hamarc {

int RunFromOptions(const ParsedOptions& options) {
  switch (options.command) {
    case Command::kCreate:
      return RunCreate(options);
    case Command::kList:
      return RunList(options);
    case Command::kExtract:
      return RunExtract(options);
    case Command::kAppend:
      return RunAppend(options);
    case Command::kDelete:
      return RunDelete(options);
    case Command::kConcatenate:
      return RunConcatenate(options);
    case Command::kNone:
    default:
      std::cerr << "No command specified.\n";
      return 1;
  }
}

int RunCreate(const ParsedOptions& options) {
  HammingOptions hopt{options.hamming.data_bits, options.hamming.parity_bits};
  Archiver archiver(options.archive_path, hopt);
  bool success = archiver.Create(options.files);
  return success ? 0 : 1;
}

int RunList(const ParsedOptions& options) {
  HammingOptions hopt{options.hamming.data_bits, options.hamming.parity_bits};
  Archiver archiver(options.archive_path, hopt);
  bool success = archiver.List();
  return success ? 0 : 1;
}

int RunExtract(const ParsedOptions& options) {
  HammingOptions hopt{options.hamming.data_bits, options.hamming.parity_bits};
  Archiver archiver(options.archive_path, hopt);
  bool success = archiver.Extract(options.files);
  return success ? 0 : 1;
}

int RunAppend(const ParsedOptions& options) {
  HammingOptions hopt{options.hamming.data_bits, options.hamming.parity_bits};
  Archiver archiver(options.archive_path, hopt);
  bool success = archiver.Append(options.files);
  return success ? 0 : 1;
}

int RunDelete(const ParsedOptions& options) {
  HammingOptions hopt{options.hamming.data_bits, options.hamming.parity_bits};
  Archiver archiver(options.archive_path, hopt);
  bool success = archiver.Delete(options.files);
  return success ? 0 : 1;
}

int RunConcatenate(const ParsedOptions& options) {
  if (options.files.size() < 2) {
    std::cerr << "Concatenate requires at least two source archives.\n";
    return 1;
  }
  HammingOptions hopt{options.hamming.data_bits, options.hamming.parity_bits};
  Archiver archiver(options.archive_path, hopt);
  bool success = archiver.Concatenate(options.files);
  return success ? 0 : 1;
}

}  // namespace hamarc
