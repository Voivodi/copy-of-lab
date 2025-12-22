#include <gtest/gtest.h>

#include <random>
#include <optional>
#include <chrono> 
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdint>
#include <cstring>

namespace fs = std::filesystem;

static std::string QuotePath(const fs::path& p);

struct TempDir {
  fs::path root;
  explicit TempDir(const std::string& prefix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    root = fs::temp_directory_path() / (prefix + "_" + std::to_string(now));
    std::error_code ec;
    fs::create_directories(root, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(root, ec);
  }
};

struct ScopedCurrentPath {
  fs::path old;
  explicit ScopedCurrentPath(const fs::path& p) {
    old = fs::current_path();
    fs::current_path(p);
  }
  ~ScopedCurrentPath() { fs::current_path(old); }
};

static void WriteDeterministicFile(const fs::path& path, std::size_t size, std::uint32_t seed) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(out.good());
  std::uint32_t x = seed;
  for (std::size_t i = 0; i < size; ++i) {
    x = x * 1664525u + 1013904223u;  
    const unsigned char b = static_cast<unsigned char>((x >> 24) & 0xFF);
    out.write(reinterpret_cast<const char*>(&b), 1);
  }
  out.close();
  ASSERT_TRUE(fs::exists(path));
  ASSERT_EQ(fs::file_size(path), size);
}

static int RunHamArc(const std::vector<std::string>& args, const std::optional<fs::path>& cwd = std::nullopt) {
  const std::string hamarc = HAMARC_EXE_PATH;
  std::ostringstream cmd;
  cmd << QuotePath(fs::path(hamarc));
  for (const auto& a : args) cmd << " " << a;

  if (cwd.has_value()) {
    ScopedCurrentPath scoped(*cwd);
    return std::system(cmd.str().c_str());
  }
  return std::system(cmd.str().c_str());
}

static int RunHamArcCapture(const std::vector<std::string>& args,
                            const fs::path& stdout_path,
                            const fs::path& stderr_path,
                            const std::optional<fs::path>& cwd = std::nullopt) {
  const std::string hamarc = HAMARC_EXE_PATH;
  std::ostringstream cmd;
  cmd << QuotePath(fs::path(hamarc));
  for (const auto& a : args) cmd << " " << a;
  cmd << " > " << QuotePath(stdout_path) << " 2> " << QuotePath(stderr_path);

  if (cwd.has_value()) {
    ScopedCurrentPath scoped(*cwd);
    return std::system(cmd.str().c_str());
  }
  return std::system(cmd.str().c_str());
}

static std::string ReadAllText(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static void FlipBitInFile(const fs::path& p, std::uint64_t byte_pos, int bit_pos) {
  std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
  ASSERT_TRUE(f.is_open());
  f.seekg(static_cast<std::streamoff>(byte_pos), std::ios::beg);
  char byte = 0;
  f.read(&byte, 1);
  ASSERT_EQ(f.gcount(), 1);
  byte ^= static_cast<char>(1 << bit_pos);
  f.seekp(static_cast<std::streamoff>(byte_pos), std::ios::beg);
  f.write(&byte, 1);
  f.flush();
  f.close();
}

static std::string FileFlag(const fs::path& archive) {
  return std::string("--file=") + QuotePath(archive);
}

static std::string QuotePath(const fs::path& p) {
	fs::path native_path = p;
	native_path.make_preferred();
	std::string s = native_path.string();
#ifdef _WIN32
	if (s.find(' ') != std::string::npos) {
		return "\"" + s + "\"";
	}
	return s;
#else
  return "\"" + s + "\"";
#endif
}

static bool FilesEqual(const fs::path& a, const fs::path& b) {
	if (!fs::exists(a) || !fs::exists(b)) return false;
	if (fs::file_size(a) != fs::file_size(b)) return false;

	std::ifstream fa(a, std::ios::binary);
	std::ifstream fb(b, std::ios::binary);
	if (!fa || !fb) return false;

	const std::size_t buffer_size = 1 << 20;
	std::vector<char> ba(buffer_size), bb(buffer_size);

	while (fa && fb) {
		fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
		fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
		const auto ca = fa.gcount();
		const auto cb = fb.gcount();
		if (ca != cb) return false;
		if (std::memcmp(ba.data(), bb.data(), static_cast<std::size_t>(ca)) != 0) return false;
	}
	return fa.eof() && fb.eof();
}

TEST(HamArcCLI, CreateAndExtractAndCompare) {
	const fs::path resources_dir = fs::path(RESOURCES_DIR);
	const fs::path file1 = resources_dir / "BjarneStroustrup.jpg";
	const fs::path file2 = resources_dir / "Book.pdf";

	ASSERT_TRUE(fs::exists(file1));
	ASSERT_TRUE(fs::exists(file2));

	const auto temp_root = fs::temp_directory_path();
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path work = temp_root / ("hamarc_test_" + std::to_string(now));
	const fs::path out_dir = work / "out";
	ASSERT_TRUE(fs::create_directories(out_dir));

	const fs::path archive = work / "archive.haf";
	const std::string hamarc = HAMARC_EXE_PATH;

	{
		std::ostringstream cmd;
		cmd << QuotePath(hamarc)
		    << " --create"
		    << " --file=" << QuotePath(archive)
		    << " " << QuotePath(file1)
		    << " " << QuotePath(file2);
		std::cout << "Create command: " << cmd.str() << std::endl;
		const int rc = std::system(cmd.str().c_str());
		std::cout << "Return code: " << rc << std::endl;
		ASSERT_EQ(rc, 0);
		ASSERT_TRUE(fs::exists(archive));
	}

	{
		const std::uintmax_t archive_size = fs::file_size(archive);
		std::fstream archive_file(archive, std::ios::in | std::ios::out | std::ios::binary);
		ASSERT_TRUE(archive_file.is_open());

		std::vector<std::pair<std::uintmax_t, int>> flipped_bits = {
			{100, 0},
			{archive_size / 2, 0},
			{archive_size - 1, 0}
		};

		for (const auto& [byte_pos, bit_pos] : flipped_bits) {
			archive_file.seekg(static_cast<std::streamoff>(byte_pos));
			char byte = 0;
			archive_file.read(&byte, 1);
			if (archive_file.gcount() != 1) continue;

			byte ^= (1 << bit_pos);

			archive_file.seekp(static_cast<std::streamoff>(byte_pos));
			archive_file.write(&byte, 1);
			archive_file.flush();
		}

		archive_file.close();
	}

	{
		const fs::path original_dir = fs::current_path();
		fs::current_path(out_dir);
		
		std::ostringstream cmd;
		cmd << QuotePath(hamarc)
		    << " --extract"
		    << " --file=" << QuotePath(fs::path("..") / archive.filename());
		const int rc = std::system(cmd.str().c_str());
		
		fs::current_path(original_dir);
		ASSERT_EQ(rc, 0);
	}

	const fs::path extr1 = out_dir / file1.filename();
	const fs::path extr2 = out_dir / file2.filename();
	ASSERT_TRUE(fs::exists(extr1));
	ASSERT_TRUE(fs::exists(extr2));

	EXPECT_TRUE(FilesEqual(file1, extr1));
	EXPECT_TRUE(FilesEqual(file2, extr2));
}

TEST(HamArcCLI, ListShowsNamesAndSizes) {
  TempDir td("hamarc_list");
  const fs::path in_dir = td.root / "in";
  const fs::path out_dir = td.root / "out";
  ASSERT_TRUE(fs::create_directories(in_dir));
  ASSERT_TRUE(fs::create_directories(out_dir));

  const fs::path f1 = in_dir / "alpha.bin";
  const fs::path f2 = in_dir / "beta.bin";
  WriteDeterministicFile(f1, 64 * 1024, 1);
  WriteDeterministicFile(f2, 96 * 1024, 2);

  const fs::path archive = td.root / "a.haf";
  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), QuotePath(f1), QuotePath(f2)}), 0);
  ASSERT_TRUE(fs::exists(archive));

  const fs::path list_out = td.root / "list.txt";
  const fs::path list_err = td.root / "list.err";
  ASSERT_EQ(RunHamArcCapture({"--list", FileFlag(archive)}, list_out, list_err), 0);

  const std::string text = ReadAllText(list_out);
  EXPECT_NE(text.find("alpha.bin"), std::string::npos);
  EXPECT_NE(text.find("beta.bin"), std::string::npos);
  EXPECT_NE(text.find("(" + std::to_string(fs::file_size(f1)) + " bytes)"), std::string::npos);
  EXPECT_NE(text.find("(" + std::to_string(fs::file_size(f2)) + " bytes)"), std::string::npos);
}

TEST(HamArcCLI, ExtractSingleFileOnly) {
  TempDir td("hamarc_extract_one");
  const fs::path in_dir = td.root / "in";
  const fs::path out_dir = td.root / "out";
  ASSERT_TRUE(fs::create_directories(in_dir));
  ASSERT_TRUE(fs::create_directories(out_dir));

  const fs::path f1 = in_dir / "one.bin";
  const fs::path f2 = in_dir / "two.bin";
  WriteDeterministicFile(f1, 10 * 1024, 10);
  WriteDeterministicFile(f2, 12 * 1024, 20);

  const fs::path archive = td.root / "a.haf";
  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), QuotePath(f1), QuotePath(f2)}), 0);


  ASSERT_EQ(RunHamArc({"--extract", FileFlag(archive), "two.bin"}, out_dir), 0);

  EXPECT_FALSE(fs::exists(out_dir / "one.bin"));
  ASSERT_TRUE(fs::exists(out_dir / "two.bin"));
  EXPECT_TRUE(FilesEqual(f2, out_dir / "two.bin"));
}

TEST(HamArcCLI, AppendAddsFilesAndTheyExtractCorrectly) {
  TempDir td("hamarc_append");
  const fs::path in_dir = td.root / "in";
  const fs::path out_dir = td.root / "out";
  ASSERT_TRUE(fs::create_directories(in_dir));
  ASSERT_TRUE(fs::create_directories(out_dir));

  const fs::path f1 = in_dir / "base.bin";
  const fs::path f2 = in_dir / "added.bin";
  WriteDeterministicFile(f1, 32 * 1024, 111);
  WriteDeterministicFile(f2, 48 * 1024, 222);

  const fs::path archive = td.root / "a.haf";
  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), QuotePath(f1)}), 0);

  ASSERT_EQ(RunHamArc({"--append", FileFlag(archive), QuotePath(f2)}), 0);

  ASSERT_EQ(RunHamArc({"--extract", FileFlag(archive)}, out_dir), 0);
  EXPECT_TRUE(FilesEqual(f1, out_dir / "base.bin"));
  EXPECT_TRUE(FilesEqual(f2, out_dir / "added.bin"));
}

TEST(HamArcCLI, DeleteRemovesFileFromListAndExtraction) {
  TempDir td("hamarc_delete");
  const fs::path in_dir = td.root / "in";
  const fs::path out_dir = td.root / "out";
  ASSERT_TRUE(fs::create_directories(in_dir));
  ASSERT_TRUE(fs::create_directories(out_dir));

  const fs::path f1 = in_dir / "killme.bin";
  const fs::path f2 = in_dir / "keepme.bin";
  WriteDeterministicFile(f1, 20 * 1024, 1);
  WriteDeterministicFile(f2, 24 * 1024, 2);

  const fs::path archive = td.root / "a.haf";
  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), QuotePath(f1), QuotePath(f2)}), 0);

  ASSERT_EQ(RunHamArc({"--delete", FileFlag(archive), "killme.bin"}), 0);

  const fs::path list_out = td.root / "list.txt";
  const fs::path list_err = td.root / "list.err";
  ASSERT_EQ(RunHamArcCapture({"--list", FileFlag(archive)}, list_out, list_err), 0);
  const std::string text = ReadAllText(list_out);
  EXPECT_EQ(text.find("killme.bin"), std::string::npos);
  EXPECT_NE(text.find("keepme.bin"), std::string::npos);

  ASSERT_EQ(RunHamArc({"--extract", FileFlag(archive)}, out_dir), 0);
  EXPECT_FALSE(fs::exists(out_dir / "killme.bin"));
  EXPECT_TRUE(FilesEqual(f2, out_dir / "keepme.bin"));
}

TEST(HamArcCLI, DeleteNonExistingFileFailsAndArchiveStaysIntact) {
  TempDir td("hamarc_delete_missing");
  const fs::path in_dir = td.root / "in";
  ASSERT_TRUE(fs::create_directories(in_dir));

  const fs::path f1 = in_dir / "present.bin";
  WriteDeterministicFile(f1, 16 * 1024, 7);

  const fs::path archive = td.root / "a.haf";
  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), QuotePath(f1)}), 0);


  EXPECT_NE(RunHamArc({"--delete", FileFlag(archive), "absent.bin"}), 0);


  const fs::path list_out = td.root / "list.txt";
  const fs::path list_err = td.root / "list.err";
  ASSERT_EQ(RunHamArcCapture({"--list", FileFlag(archive)}, list_out, list_err), 0);
  const std::string text = ReadAllText(list_out);
  EXPECT_NE(text.find("present.bin"), std::string::npos);
}

TEST(HamArcCLI, ConcatenateRenamesDuplicatesAndExtractsBoth) {
  TempDir td("hamarc_concat");
  const fs::path d1 = td.root / "d1";
  const fs::path d2 = td.root / "d2";
  const fs::path out_dir = td.root / "out";
  ASSERT_TRUE(fs::create_directories(d1));
  ASSERT_TRUE(fs::create_directories(d2));
  ASSERT_TRUE(fs::create_directories(out_dir));


  const fs::path f1 = d1 / "dup.bin";
  const fs::path f2 = d2 / "dup.bin";
  WriteDeterministicFile(f1, 12 * 1024, 100);
  WriteDeterministicFile(f2, 12 * 1024, 200);

  const fs::path a1 = td.root / "a1.haf";
  const fs::path a2 = td.root / "a2.haf";
  const fs::path a3 = td.root / "merged.haf";

  ASSERT_EQ(RunHamArc({"--create", FileFlag(a1), QuotePath(f1)}), 0);
  ASSERT_EQ(RunHamArc({"--create", FileFlag(a2), QuotePath(f2)}), 0);

  ASSERT_EQ(RunHamArc({"--concatenate", FileFlag(a3), QuotePath(a1), QuotePath(a2)}), 0);


  const fs::path list_out = td.root / "list.txt";
  const fs::path list_err = td.root / "list.err";
  ASSERT_EQ(RunHamArcCapture({"--list", FileFlag(a3)}, list_out, list_err), 0);
  const std::string text = ReadAllText(list_out);
  EXPECT_NE(text.find("dup.bin"), std::string::npos);
  EXPECT_NE(text.find("dup.bin(2)"), std::string::npos);

  ASSERT_EQ(RunHamArc({"--extract", FileFlag(a3)}, out_dir), 0);
  ASSERT_TRUE(fs::exists(out_dir / "dup.bin"));
  ASSERT_TRUE(fs::exists(out_dir / "dup.bin(2)"));


  EXPECT_TRUE(FilesEqual(f1, out_dir / "dup.bin"));
  EXPECT_TRUE(FilesEqual(f2, out_dir / "dup.bin(2)"));
}

TEST(HamArcCLI, CorruptedSignatureMakesListFail) {
  TempDir td("hamarc_bad_sig");
  const fs::path in_dir = td.root / "in";
  ASSERT_TRUE(fs::create_directories(in_dir));

  const fs::path f1 = in_dir / "x.bin";
  WriteDeterministicFile(f1, 8 * 1024, 123);

  const fs::path archive = td.root / "a.haf";
  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), QuotePath(f1)}), 0);


  FlipBitInFile(archive, /*byte_pos=*/0, /*bit_pos=*/0);

  EXPECT_NE(RunHamArc({"--list", FileFlag(archive)}), 0);
}

TEST(HamArcCLI, WorksWithCustomHammingParamsAndCorrectsSingleBitDamage) {
  TempDir td("hamarc_custom_hamming");
  const fs::path in_dir = td.root / "in";
  const fs::path out_dir = td.root / "out";
  ASSERT_TRUE(fs::create_directories(in_dir));
  ASSERT_TRUE(fs::create_directories(out_dir));

  const fs::path f1 = in_dir / "space name.bin"; 
  WriteDeterministicFile(f1, 40 * 1024, 999);

  const fs::path archive = td.root / "a.haf";

  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), "-D", "4", "-P", "3", QuotePath(f1)}), 0);

  const auto sz = fs::file_size(archive);
  ASSERT_GT(sz, 128u);
  FlipBitInFile(archive, /*byte_pos=*/64, /*bit_pos=*/1);

  ASSERT_EQ(RunHamArc({"--extract", FileFlag(archive), "-D", "4", "-P", "3"}, out_dir), 0);
  EXPECT_TRUE(FilesEqual(f1, out_dir / f1.filename()));
}

TEST(HamArcCLI, ExtractMissingFileFails) {
  TempDir td("hamarc_extract_missing");
  const fs::path in_dir = td.root / "in";
  const fs::path out_dir = td.root / "out";
  ASSERT_TRUE(fs::create_directories(in_dir));
  ASSERT_TRUE(fs::create_directories(out_dir));

  const fs::path f1 = in_dir / "present.bin";
  WriteDeterministicFile(f1, 8 * 1024, 5);

  const fs::path archive = td.root / "a.haf";
  ASSERT_EQ(RunHamArc({"--create", FileFlag(archive), QuotePath(f1)}), 0);

  EXPECT_NE(RunHamArc({"--extract", FileFlag(archive), "absent.bin"}, out_dir), 0);
  EXPECT_FALSE(fs::exists(out_dir / "present.bin"));
}