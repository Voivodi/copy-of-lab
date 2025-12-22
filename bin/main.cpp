#include "hamarc_core.h"
#include "parse_args.h"

int main(int argc, const char* const argv[]) {
  hamarc::ParsedOptions options;
  if (!hamarc::ParseCommandLine(argc, argv, options)) {
    return options.show_help ? 0 : 1;
  }
  return hamarc::RunFromOptions(options);
}

