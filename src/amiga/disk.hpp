// The game disk: an 880 KB ADF image. The game writes to it (stage 2 has
// a file write path), so changes go to a copy in the save directory.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace amiga {

constexpr uint32_t ADF_SIZE = 901120;  // 80 cylinders × 2 heads × 11 sectors × 512

class Disk {
public:
  // Load the image; if `save_path` exists it is used instead (it holds the
  // disk as the game last wrote it). Writes go to save_path.
  bool load(const std::string& adf_path, const std::string& save_path);
  bool read(uint32_t offset, uint32_t len, uint8_t* dst) const;
  bool write(uint32_t offset, uint32_t len, const uint8_t* src);
  // Write the image to save_path if it was modified.
  void flush();
  const std::vector<uint8_t>& image() const { return img_; }

  // ---- AmigaDOS (OFS) file system, read-only -----------------------------
  // True if the disk has a valid AmigaDOS root block.
  bool is_dos() const;
  // Header block of a file or directory ("s/startup-sequence", "df0:x",
  // case-insensitive); the root block for "" or ":"; -1 if not found.
  int find(const std::string& path) const;
  bool is_dir(int block) const;
  // Contents of the file whose header block is `block`.
  bool read_file(int block, std::vector<uint8_t>& out) const;
  std::string entry_name(int block) const;
  uint32_t file_size(int block) const;
  // Header blocks of the entries of a directory.
  std::vector<int> list_dir(int block) const;
  int parent(int block) const;

private:
  uint32_t l(int block, int index) const;  // big-endian long of a block
  std::vector<uint8_t> img_;
  std::string save_path_;
  bool dirty_ = false;
};

}  // namespace amiga
