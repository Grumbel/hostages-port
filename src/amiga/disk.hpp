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

private:
  std::vector<uint8_t> img_;
  std::string save_path_;
  bool dirty_ = false;
};

}  // namespace amiga
