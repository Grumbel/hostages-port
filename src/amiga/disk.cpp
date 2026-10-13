#include "amiga/disk.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace amiga {

namespace {
bool read_file(const std::string& path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  out.assign(ADF_SIZE, 0);
  size_t n = fread(out.data(), 1, out.size(), f);
  fclose(f);
  return n == ADF_SIZE;
}
}  // namespace

bool Disk::load(const std::string& adf_path, const std::string& save_path) {
  save_path_ = save_path;
  if (!save_path.empty() && read_file(save_path, img_)) {
    fprintf(stderr, "using saved disk %s\n", save_path.c_str());
    return true;
  }
  if (!read_file(adf_path, img_)) {
    fprintf(stderr, "cannot read ADF %s\n", adf_path.c_str());
    return false;
  }
  return true;
}

bool Disk::read(uint32_t offset, uint32_t len, uint8_t* dst) const {
  if (uint64_t(offset) + len > img_.size()) return false;
  std::memcpy(dst, img_.data() + offset, len);
  return true;
}

bool Disk::write(uint32_t offset, uint32_t len, const uint8_t* src) {
  if (uint64_t(offset) + len > img_.size()) return false;
  if (std::memcmp(img_.data() + offset, src, len) == 0) return true;
  std::memcpy(img_.data() + offset, src, len);
  dirty_ = true;
  return true;
}

void Disk::flush() {
  if (!dirty_ || save_path_.empty()) return;
  std::filesystem::path p(save_path_);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
  std::string tmp = save_path_ + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return;
  bool ok = fwrite(img_.data(), 1, img_.size(), f) == img_.size();
  ok = fclose(f) == 0 && ok;
  if (ok) std::filesystem::rename(tmp, save_path_);
  dirty_ = false;
}

}  // namespace amiga
