#include "parse_args.h"

#include <cstdio>
#include <utility>
#include <string>
#include <vector>

#include "argparser.h"

namespace hamarc {
namespace {

using nargparse::ArgumentParser;

struct RawCliOptions {
  bool is_create_mode = false;
  bool is_list_mode = false;
  bool is_extract_mode = false;
  bool is_append_mode = false;
  bool is_delete_mode = false;
  bool is_concatenate_mode = false;

  bool is_help_requested = false;

  static constexpr int kMaxPathLength = 4096;
  char archive_path[kMaxPathLength];

  int hamming_data_bits = 8;
  int hamming_parity_bits = 4;

  RawCliOptions() {
    archive_path[0] = '\0';
  }
};

void CollectFiles(ArgumentParser parser, std::vector<std::string>& out) {
  const int count = nargparse::GetRepeatedCount(parser, "files");
  out.clear();
  out.reserve(count);

  for (int i = 0; i < count; ++i) {
    const char* value = nullptr;
    if (nargparse::GetRepeated(parser, "files", i, &value) &&
        value != nullptr) {
      out.emplace_back(value);
    }
  }
}

void PrintErrorAndHelp(ArgumentParser parser, const char* message) {
  if (message != nullptr && message[0] != '\0') {
    std::fprintf(stderr, "Error: %s\n\n", message);
  }
  nargparse::PrintHelp(parser);
}

bool ValidateHammingDataBits(const int& value) {
  return value > 0 && value <= 16;
}

bool ValidateHammingParityBits(const int& value) {
  return value > 0 && value <= 8;
}

void AddModeFlags(ArgumentParser parser, RawCliOptions& raw_options) {
  nargparse::AddFlag(parser, "-c", "--create", &raw_options.is_create_mode, "Create new archive");
  nargparse::AddFlag(parser, "-l", "--list", &raw_options.is_list_mode, "List files in archive");
  nargparse::AddFlag(parser, "-x", "--extract", &raw_options.is_extract_mode, "Extract files from archive");
  nargparse::AddFlag(parser, "-a", "--append", &raw_options.is_append_mode, "Append files to archive");
  nargparse::AddFlag(parser, "-d", "--delete", &raw_options.is_delete_mode, "Delete files from archive");
  nargparse::AddFlag(parser, "-A", "--concatenate",  &raw_options.is_concatenate_mode,
                     "Concatenate archives");
}

void AddHelpFlag(ArgumentParser parser, RawCliOptions& raw_options) {
  nargparse::AddFlag(parser, "-h", "--help", &raw_options.is_help_requested,
                     "Show this help and exit");
}

void AddArchiveArgument(ArgumentParser parser, RawCliOptions& raw_options) {
  nargparse::AddArgument(parser, "-f",
                         "--file", &raw_options.archive_path,
                         "Archive file path", nargparse::kNargsRequired);
}

void AddHammingArguments(ArgumentParser parser, RawCliOptions& raw_options) {
  nargparse::AddArgument(parser, "-D",
                         "--hamming-data-bits",  &raw_options.hamming_data_bits,
                         "Hamming data bits (k)", nargparse::kNargsOptional,
                         &ValidateHammingDataBits,  "must be > 0 and <= 16");

  nargparse::AddArgument(parser, "-P",
                         "--hamming-parity-bits", &raw_options.hamming_parity_bits,
                         "Hamming parity bits (r)", nargparse::kNargsOptional,
                         &ValidateHammingParityBits,  "must be > 0 and <= 8");
}

void AddFilesArgument(ArgumentParser parser) {
  nargparse::AddArgument(parser, static_cast<char (*)[]>(nullptr),
                         "files", nargparse::kNargsZeroOrMore);
}

ArgumentParser CreateParserWithAllOptions(RawCliOptions& raw_options) {
  ArgumentParser parser =
      nargparse::CreateParser("hamarc", RawCliOptions::kMaxPathLength);

  AddModeFlags(parser, raw_options);
  AddHelpFlag(parser, raw_options);
  AddArchiveArgument(parser, raw_options);
  AddHammingArguments(parser, raw_options);
  AddFilesArgument(parser);

  return parser;
}

int CountSelectedModes(const RawCliOptions& raw_options) {
  int count = 0;
  if (raw_options.is_create_mode) {
    ++count;
  }
  if (raw_options.is_list_mode) {
    ++count;
  }
  if (raw_options.is_extract_mode) {
    ++count;
  }
  if (raw_options.is_append_mode) {
    ++count;
  }
  if (raw_options.is_delete_mode) {
    ++count;
  }
  if (raw_options.is_concatenate_mode) {
    ++count;
  }
  return count;
}

bool ValidateModeSelection(int modes_count, ArgumentParser parser, ParsedOptions& options) {
  if (modes_count == 0) {
    options.show_help = true;
    PrintErrorAndHelp(parser, "you must specify exactly one mode: "
        "--create, --list, --extract, --append, --delete or --concatenate");
    return false;
  }

  if (modes_count > 1) {
    options.show_help = true;
    PrintErrorAndHelp(parser, "only one mode can be used at the same time");
    return false;
  }

  return true;
}

bool ParseArguments(ArgumentParser parser, int argc, const char* const argv[], 
                    RawCliOptions& raw_options, ParsedOptions& options) {
  if (!nargparse::Parse(parser, argc, argv)) {
    options.show_help = true;
    PrintErrorAndHelp(parser, "invalid command line arguments");
    return false;
  }

  if (raw_options.is_help_requested) {
    options.show_help = true;
    nargparse::PrintHelp(parser);
    return false;
  }
  const int modes_count = CountSelectedModes(raw_options);
  if (!ValidateModeSelection(modes_count, parser, options)) {
    return false;
  }

  return true;
}

Command DetectCommand(const RawCliOptions& raw_options) {
  if (raw_options.is_create_mode) {
    return Command::kCreate;
  }
  if (raw_options.is_list_mode) {
    return Command::kList;
  }
  if (raw_options.is_extract_mode) {
    return Command::kExtract;
  }
  if (raw_options.is_append_mode) {
    return Command::kAppend;
  }
  if (raw_options.is_delete_mode) {
    return Command::kDelete;
  }
  if (raw_options.is_concatenate_mode) {
    return Command::kConcatenate;
  }
  return Command::kNone;
}

void FillParsedOptionsFromRaw(const RawCliOptions& raw_options, ArgumentParser parser,
                              ParsedOptions& parsed) {
  parsed.command = DetectCommand(raw_options);
  parsed.archive_path = raw_options.archive_path;
  CollectFiles(parser, parsed.files);
  parsed.hamming.data_bits = raw_options.hamming_data_bits;
  parsed.hamming.parity_bits = raw_options.hamming_parity_bits;
}

bool ValidateOptionsByMode(const ParsedOptions& parsed, ArgumentParser parser,
                           ParsedOptions& options) {
  switch (parsed.command) {
    case Command::kCreate:
    case Command::kAppend:
    case Command::kDelete:
      if (parsed.files.empty()) {
        options.show_help = true;
        PrintErrorAndHelp(parser, "this mode requires at least one file name");
        return false;
      }
      break;

    case Command::kConcatenate:
      if (parsed.files.size() < 2) {
        options.show_help = true;
        PrintErrorAndHelp(parser, "concatenate mode requires at least two source archives");
        return false;
      }
      break;

    case Command::kList:
      break;

    case Command::kExtract:
      break;

    case Command::kNone:
      break;
  }

  return true;
}

}  // namespace

bool ParseCommandLine(int argc, const char* const argv[], ParsedOptions& options) {
  options = ParsedOptions{};

  RawCliOptions raw_options;
  ArgumentParser parser = CreateParserWithAllOptions(raw_options);

  ParsedOptions parsed;

  if (!ParseArguments(parser, argc, argv, raw_options, options)) {
    nargparse::FreeParser(parser);
    return false;
  }

  FillParsedOptionsFromRaw(raw_options, parser, parsed);

  if (!ValidateOptionsByMode(parsed, parser, options)) {
    nargparse::FreeParser(parser);
    return false;
  }

  nargparse::FreeParser(parser);

  options = std::move(parsed);
  options.show_help = false;
  return true;
}

}  // namespace hamarc
