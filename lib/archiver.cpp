#include "archiver.h"
#include "hamming_codec.h"
#include "hamming_options.h"

#include <algorithm>
#include <cstdint> 
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string> 
#include <system_error>
#include <unordered_set>
#include <vector>

namespace fs=std::filesystem;

namespace hamarc {

struct FileEntry {
  std::string name;
  std::string source_path;
  std::uint64_t original_size = 0;
  std::uint64_t encoded_size = 0;
  std::uint64_t offset = 0;
};

namespace {

std::uint64_t CalculateEncodedSize(const HammingCodec& codec, std::uint64_t original_size) {
  const std::uint64_t original_bits = original_size * 8;

  const std::uint64_t data_bits = static_cast<std::uint64_t>(codec.DataBits());
  const std::uint64_t parity_bits = static_cast<std::uint64_t>(codec.ParityBits());
  const std::uint64_t codeword_bits = data_bits + parity_bits;

  const std::uint64_t codeword_count = (original_bits + data_bits - 1) / data_bits;
  const std::uint64_t total_code_bits = codeword_count * codeword_bits;

  return (total_code_bits + 7) / 8;
}

std::uint64_t CalculateHeaderSize(const std::vector<FileEntry>& entries) {
  std::uint64_t header_size = 3 + 4;
  for (const FileEntry& entry : entries) {
    header_size += 2;
    header_size +=entry.name.size();
    header_size +=8+8+8;
  }
  return header_size;
}

void AssignOffsets(std::vector<FileEntry>& entries, std::uint64_t header_size) {
  std::uint64_t current_offset = header_size;
  for (FileEntry& entry : entries) {
    entry.offset = current_offset;
    current_offset += entry.encoded_size;
  }
}

bool WriteArchiveHeader(std::ostream& out, const std::vector<FileEntry>& entries) {
  out.write("HAF", 3);
  const std::uint32_t file_count = static_cast<std::uint32_t>(entries.size());
  out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));

  for (const FileEntry& entry : entries) {
    const std::uint16_t name_length = static_cast<std::uint16_t>(entry.name.size());
    out.write(reinterpret_cast<const char*>(&name_length), sizeof(name_length));
    out.write(entry.name.c_str(), name_length);
    out.write(reinterpret_cast<const char*>(&entry.original_size), sizeof(entry.original_size));
    out.write(reinterpret_cast<const char*>(&entry.encoded_size), sizeof(entry.encoded_size));
    out.write(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
  }

  return out.good();
}

bool ReadArchiveHeader(std::ifstream& in,  const std::string& archive_path, std::vector<FileEntry>& entries) {
  char signature[3];
  if (!in.read(signature, 3) || std::strncmp(signature, "HAF", 3) != 0) {
    std::cerr << "Invalid or corrupt archive format: " << archive_path << "\n";
    return false;
  }
  std::uint32_t file_count = 0;
  if (!in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count))) {
    std::cerr << "Failed to read archive header.\n";
    return false;
  }

  entries.clear();
  entries.reserve(file_count);

  for (std::uint32_t index = 0; index < file_count; ++index) {
    FileEntry entry;
    std::uint16_t name_length = 0;

    if (!in.read(reinterpret_cast<char*>(&name_length), sizeof(name_length))) {
      std::cerr << "Failed to read file entry.\n";
      return false;
    }

    entry.name.resize(name_length);
    if (name_length > 0 &&
        !in.read(&entry.name[0], name_length)) {
      std::cerr << "Failed to read file name.\n";
      return false;
    }

    if (!in.read(reinterpret_cast<char*>(&entry.original_size), sizeof(entry.original_size)) ||
        !in.read(reinterpret_cast<char*>(&entry.encoded_size), sizeof(entry.encoded_size)) ||
        !in.read(reinterpret_cast<char*>(&entry.offset), sizeof(entry.offset))) {
      std::cerr << "Failed to read file metadata.\n";
      return false;
    }

    entries.push_back(entry);
  }

  return true;
}

bool CollectNewEntries(const std::vector<std::string>& input_files,
                       const HammingCodec& codec,
                       std::vector<FileEntry>& out_entries) {
  out_entries.clear();
  out_entries.reserve(input_files.size());

  for (const std::string& file_path : input_files) {
    fs::path path = fs::u8path(file_path);
    if (!fs::exists(path) || fs::is_directory(path)) {
      std::cerr << "Input file not found: " << file_path << "\n";
      return false;
    }

    const std::uint64_t original_size = fs::file_size(path);
    const std::uint64_t encoded_size =
        CalculateEncodedSize(codec, original_size);

    FileEntry entry;
    entry.name = path.filename().generic_string();
    entry.source_path = path.generic_string();
    entry.original_size = original_size;
    entry.encoded_size = encoded_size;
    entry.offset = 0;

    out_entries.push_back(entry);
  }

  return true;
}

bool EncodeFileToArchive(const FileEntry& entry, HammingCodec& codec, std::ostream& archive_out) {
  std::ifstream in_file(fs::u8path(entry.source_path), std::ios::binary);
  if (!in_file) {
    std::cerr <<"Failed to open input file: "<<entry.source_path<<"\n";
    return false;
  }

  if (!codec.EncodeStream(in_file, archive_out)) {
    std::cerr << "Error encoding file: " << entry.source_path << "\n";
    return false;
  }

  return true;
}

bool CopyEntryData(std::ifstream& archive_in, const FileEntry& entry, std::ostream& archive_out) {
  std::vector<char> buffer(8192);

  archive_in.seekg(entry.offset, std::ios::beg);
  if (!archive_in.good()) {
    std::cerr << "Error seeking in archive.\n";
    return false;
  }

  std::uint64_t remaining = entry.encoded_size;
  while (remaining > 0) {
    const std::streamsize chunk_size =
        static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));

    if (!archive_in.read(buffer.data(), chunk_size)) {
      std::cerr << "Error reading archive data.\n";
      return false;
    }

    const std::streamsize bytes_read = archive_in.gcount();
    archive_out.write(buffer.data(), bytes_read);
    if (!archive_out.good()) {
      std::cerr << "Error writing to archive file.\n";
      return false;
    }

    remaining -=static_cast<std::uint64_t>(bytes_read);
  }

  return true;
}

bool EnsureParentDirectoryExists(const fs::path& path) {
  if (!path.has_parent_path()) {
    return true;
  }

  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec && !fs::exists(path.parent_path())) {
    std::cerr << "Failed to create directory: "
              << path.parent_path() << " (" << ec.message()<< ")\n";
    return false;
  }

  return true;
}

bool FindEntriesByNames(const std::vector<FileEntry>& all_entries,
                        const std::vector<std::string>& requested_names,
                        std::vector<FileEntry>& out_entries) {
  out_entries.clear();
  for (const std::string& name : requested_names) {
    bool found = false;
    for (const FileEntry& entry : all_entries) {
      if (entry.name == name) {
        out_entries.push_back(entry);
        found = true;
      }
    }
    if (!found) {
      std::cerr << "File not found in archive: " << name << "\n";
      return false;
    }
  }

  return true;
}

} // namespace

Archiver::Archiver(const std::string& archive_path, const HammingOptions& hamming)
    : archive_path_(archive_path), codec_(hamming) {}

bool Archiver::Create(const std::vector<std::string>& input_files) {
  fs::path out_path(archive_path_);
  std::error_code ec;

  
  if (out_path.has_parent_path()) {
    fs::create_directories(out_path.parent_path(), ec);
    if (ec && !fs::exists(out_path.parent_path())) {
      std::cerr << "Failed to create directory: " << out_path.parent_path()
                << " (" << ec.message() << ")\n";
      return false;
    }
  }

  std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "Failed to open archive for writing: " << archive_path_ << "\n";
    return false;
  }

  std::vector<FileEntry> entries;
  if (!CollectNewEntries(input_files, codec_, entries)) {
    out.close();
    fs::remove(out_path);
    return false;
  }

  const std::uint64_t header_size = CalculateHeaderSize(entries);
  AssignOffsets(entries, header_size);

  if (!WriteArchiveHeader(out, entries)) {
    std::cerr << "Failed to write archive header.\n";
    out.close();
    fs::remove(out_path);
    return false;
  }

  for (const FileEntry& entry : entries) {
    if (!EncodeFileToArchive(entry, codec_, out)) {
      out.close();
      fs::remove(out_path);
      return false;
    }
  }

  out.close();
  return true;
}

bool Archiver::List() {
  std::ifstream in(archive_path_, std::ios::binary);
  if (!in) {
    std::cerr << "Failed to open archive: " << archive_path_ << "\n";
    return false;
  }

  std::vector<FileEntry> entries;
  if (!ReadArchiveHeader(in, archive_path_, entries)) {
    return false;
  }

  for (const FileEntry& entry : entries) {
    std::cout << entry.name << " (" << entry.original_size << " bytes)" << std::endl;
  }

  return true;
}

bool Archiver::Extract(const std::vector<std::string>& requested_files) {
  std::ifstream in(archive_path_, std::ios::binary);
  if (!in) {
    std::cerr << "Failed to open archive: " << archive_path_ << "\n";
    return false;
  }

  std::vector<FileEntry> entries;
  if (!ReadArchiveHeader(in, archive_path_, entries)) {
    return false;
  }

  std::vector<FileEntry> entries_to_extract;
  if (requested_files.empty()) {
    entries_to_extract = entries;
  } else {
    if (!FindEntriesByNames(entries, requested_files, entries_to_extract)) {
      return false;
    }
  }

  for (const FileEntry& entry : entries_to_extract) {
    in.seekg(entry.offset, std::ios::beg);
    if (!in.good()) {
      std::cerr << "Failed to seek to file data: " << entry.name << "\n";
      return false;
    }

    fs::path out_path = fs::u8path(entry.name);
    if (!EnsureParentDirectoryExists(out_path)) {
      return false;
    }

    std::ofstream out_file(out_path, std::ios::binary | std::ios::trunc);
    if (!out_file) {
      std::cerr << "Failed to create output file: " << entry.name << "\n";
      return false;
    }

    if (!codec_.DecodeStream(in, out_file, entry.original_size, entry.encoded_size)) {
      std::cerr << "Failed to decode file: " << entry.name << "\n";
      return false;
    }
  }

  return true;
}

bool Archiver::Append(const std::vector<std::string>& input_files) {
  std::ifstream in(archive_path_, std::ios::binary);
  if (!in) {
    std::cerr << "Failed to open archive: " << archive_path_ << "\n";
    return false;
  }

  std::vector<FileEntry> old_entries;
  if (!ReadArchiveHeader(in, archive_path_, old_entries)) {
    return false;
  }

  std::vector<FileEntry> new_entries;
  if (!CollectNewEntries(input_files, codec_, new_entries)) {
    return false;
  }

  fs::path temp_path = fs::path(archive_path_).concat(".tmp");
  std::error_code ec;
  fs::remove(temp_path, ec);

  std::ofstream out(temp_path, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open temporary archive file.\n";
    return false;
  }

  std::vector<FileEntry> all_entries;
  all_entries.reserve(old_entries.size() + new_entries.size());
  all_entries.insert(all_entries.end(), old_entries.begin(), old_entries.end());
  all_entries.insert(all_entries.end(), new_entries.begin(), new_entries.end());

  const std::uint64_t header_size = CalculateHeaderSize(all_entries);
  AssignOffsets(all_entries, header_size);

  if (!WriteArchiveHeader(out, all_entries)) {
    std::cerr << "Failed to write archive header.\n";
    return false;
  }

  for (const FileEntry& entry : old_entries) {
    if (!CopyEntryData(in, entry, out)) {
      out.close();
      fs::remove(temp_path, ec);
      return false;
    }
  }

  for (const FileEntry& entry : new_entries) {
    if (!EncodeFileToArchive(entry, codec_, out)) {
      out.close();
      fs::remove(temp_path, ec);
      return false;
    }
  }

  out.close();
  in.close();

  fs::remove(archive_path_, ec);
  if (fs::rename(temp_path, archive_path_, ec); ec) {
    std::cerr << "Failed to replace archive: " << ec.message() << "\n";
    return false;
  }

  return true;
}

bool Archiver::Delete(const std::vector<std::string>& files_to_delete) {
  std::ifstream in(archive_path_, std::ios::binary);
  if (!in) {
    std::cerr << "Failed to open archive: " << archive_path_ << "\n";
    return false;
  }

  std::vector<FileEntry> old_entries;
  if (!ReadArchiveHeader(in, archive_path_, old_entries)) {
    return false;
  }

  for (const std::string& name_to_delete : files_to_delete) {
    bool found = false;
    for (const FileEntry& entry : old_entries) {
      if (entry.name == name_to_delete) {
        found = true;
        break;
      }
    }
    if (!found) {
      std::cerr << "File not found in archive: " << name_to_delete << "\n";
      return false;
    }
  }

  std::vector<FileEntry> keep_entries;
  keep_entries.reserve(old_entries.size());

  for (const FileEntry& entry : old_entries) {
    bool should_delete = false;
    for (const std::string& name_to_delete : files_to_delete) {
      if (entry.name == name_to_delete) {
        should_delete = true;
        break;
      }
    }
    if (!should_delete) {
      keep_entries.push_back(entry);  
    }
  }

  if (keep_entries.size() == old_entries.size()) {
    std::cerr << "No specified files were deleted (not found).\n";
    return false;
  }

  const std::vector<FileEntry> keep_entries_old_offsets = keep_entries;

  fs::path temp_path = fs::path(archive_path_).concat(".tmp");
  std::error_code ec;
  fs::remove(temp_path, ec);

  std::ofstream out(temp_path, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open temporary archive file.\n";
    return false;
  }

  const std::uint64_t header_size = CalculateHeaderSize(keep_entries);
  AssignOffsets(keep_entries, header_size);

  if (!WriteArchiveHeader(out, keep_entries)) {
    std::cerr << "Failed to write archive header.\n";
    out.close();
    fs::remove(temp_path, ec);
    return false;
  }

  for (const FileEntry& entry : keep_entries_old_offsets) {
    if (!CopyEntryData(in, entry, out)) {
      out.close();
      fs::remove(temp_path, ec);
      return false;
    }
  }

  out.close();
  in.close();

  fs::remove(archive_path_, ec);
  if (fs::rename(temp_path, archive_path_, ec); ec) {
    std::cerr << "Failed to replace archive: " << ec.message() << "\n";
    return false;
  }

  return true;
}

bool Archiver::Concatenate(const std::vector<std::string>& source_archives) {
  fs::path target_path(archive_path_);

  if (target_path.has_parent_path()) {
    std::error_code ec_dir;
    fs::create_directories(target_path.parent_path(), ec_dir);
  }

  fs::path temp_path = target_path;
  temp_path += ".tmp";

  std::error_code ec;
  fs::remove(temp_path, ec);

  std::ofstream out(temp_path, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open output archive: " << archive_path_ << "\n";
    return false;
  }

  std::vector<FileEntry> combined_entries;
  std::unordered_set<std::string> used_names;

  struct SourceInfo {
    std::string path;
    std::uint64_t data_start = 0;
    std::uint64_t data_length = 0;
  };
  std::vector<SourceInfo> sources;

  for (const std::string& src_path : source_archives) {
    std::ifstream src_in(src_path, std::ios::binary);
    if (!src_in) {
      std::cerr << "Failed to open source archive: " << src_path << "\n";
      out.close();
      fs::remove(temp_path);
      return false;
    }

    std::vector<FileEntry> src_entries;
    if (!ReadArchiveHeader(src_in, src_path, src_entries)) {
      src_in.close();
      out.close();
      fs::remove(temp_path);
      return false;
    }

    for (FileEntry entry : src_entries) {
      const std::string original_name = entry.name;
      if (used_names.count(original_name) != 0U) {
        std::string new_name = original_name;
        int suffix = 2;
        while (used_names.count(new_name) != 0U) {
          new_name = original_name +"(" + std::to_string(suffix) + ")";
          ++suffix;
        }
        entry.name = new_name;
      }
      used_names.insert(entry.name);
      combined_entries.push_back(entry);
    }

    const std::uint64_t data_start = static_cast<std::uint64_t>(src_in.tellg());
    const std::uint64_t file_size = fs::file_size(src_path);
    const std::uint64_t data_length = (file_size > data_start) ? (file_size - data_start) : 0;

    sources.push_back({src_path, data_start, data_length});
  }

  const std::uint64_t header_size = CalculateHeaderSize(combined_entries);
  AssignOffsets(combined_entries, header_size);

  if (!WriteArchiveHeader(out, combined_entries)) {
    std::cerr << "Failed to write archive header.\n";
    out.close();
    fs::remove(temp_path);
    return false;
  }

  std::vector<char> buffer(8192);
  for (const SourceInfo& source : sources) {
    std::ifstream src_in(source.path, std::ios::binary);
    if (!src_in) {
      std::cerr << "Failed to open source archive: " << source.path << "\n";
      out.close();
      fs::remove(temp_path);
      return false;
    }

    src_in.seekg(static_cast<std::streamoff>(source.data_start), std::ios::beg);
    std::uint64_t remaining = source.data_length;

    while (remaining > 0) {
      const std::streamsize chunk_size =
          static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));

      if (!src_in.read(buffer.data(), chunk_size)) {
        std::cerr << "Error reading source archive data.\n";
        src_in.close();
        out.close();
        fs::remove(temp_path);
        return false;
      }

      const std::streamsize bytes_read = src_in.gcount();
      out.write(buffer.data(), bytes_read);
      if (!out.good()) {
        std::cerr << "Error writing to output archive.\n";
        src_in.close();
        out.close();
        fs::remove(temp_path);
        return false;
      }

      remaining -= static_cast<std::uint64_t>(bytes_read);
    }
  }

  out.close();

  fs::remove(archive_path_, ec);
  if (fs::rename(temp_path, archive_path_, ec); ec) {
    std::cerr << "Failed to create archive: "<< ec.message() << "\n";
    return false;
  }

  return true;
}

}  // namespace hamarc