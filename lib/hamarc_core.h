#pragma once

#include "parse_args.h"

namespace hamarc {

int RunFromOptions(const ParsedOptions& options);

int RunCreate(const ParsedOptions& options);
int RunList(const ParsedOptions& options);
int RunExtract(const ParsedOptions& options);
int RunAppend(const ParsedOptions& options);
int RunDelete(const ParsedOptions& options);
int RunConcatenate(const ParsedOptions& options);

}  // namespace hamarc
