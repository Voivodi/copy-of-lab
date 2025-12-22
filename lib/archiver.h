#ifndef HAMARC_ARCHIVER_H_
#define HAMARC_ARCHIVER_H_

#include <string>
#include <vector>
#include "hamming_codec.h"
#include "hamming_options.h"

namespace hamarc {

class Archiver {
 public:
  Archiver(const std::string& archive_path, const HammingOptions& hamming);

  Archiver(const Archiver&) = delete;
  Archiver& operator=(const Archiver&) = delete;

  bool Create(const std::vector<std::string>& input_files);
  bool List();
  bool Extract(const std::vector<std::string>& requested_files);
  bool Append(const std::vector<std::string>& input_files);
  bool Delete(const std::vector<std::string>& files_to_delete);
  bool Concatenate(const std::vector<std::string>& source_archives);

 private:
  std::string archive_path_;
  HammingCodec codec_;
};

}  // namespace hamarc

#endif  // HAMARC_ARCHIVER_H_
