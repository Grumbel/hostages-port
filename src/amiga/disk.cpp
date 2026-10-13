#include "amiga/disk.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace amiga {

namespace {
bool read_host_file(const std::string& path, std::vector<uint8_t>& out) {
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
  if (!save_path.empty() && read_host_file(save_path, img_)) {
    fprintf(stderr, "using saved disk %s\n", save_path.c_str());
    return true;
  }
  if (!read_host_file(adf_path, img_)) {
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

// ---- OFS ----------------------------------------------------------------------

namespace {
constexpr int ROOT_BLOCK = 880;
constexpr int BS = 512;
constexpr int T_HEADER = 2, T_DATA = 8, ST_ROOT = 1, ST_USERDIR = 2, ST_FILE = -3;
constexpr int HT_SIZE = 72;
}  // namespace

uint32_t Disk::l(int block, int index) const {
  size_t o = size_t(block) * BS + size_t(index) * 4;
  if (block < 0 || o + 4 > img_.size()) return 0;
  return uint32_t(img_[o]) << 24 | uint32_t(img_[o + 1]) << 16 | uint32_t(img_[o + 2]) << 8 |
         img_[o + 3];
}

bool Disk::is_dos() const {
  return std::memcmp(img_.data(), "DOS", 3) == 0 && int32_t(l(ROOT_BLOCK, 0)) == T_HEADER &&
         int32_t(l(ROOT_BLOCK, BS / 4 - 1)) == ST_ROOT;
}

std::string Disk::entry_name(int block) const {
  size_t o = size_t(block) * BS + BS - 80;
  int n = std::min<int>(img_[o], 30);
  return std::string(reinterpret_cast<const char*>(&img_[o + 1]), size_t(n));
}

bool Disk::is_dir(int block) const {
  int32_t st = int32_t(l(block, BS / 4 - 1));
  return st == ST_ROOT || st == ST_USERDIR;
}

uint32_t Disk::file_size(int block) const { return l(block, BS / 4 - 47); }

int Disk::parent(int block) const {
  if (block == ROOT_BLOCK) return ROOT_BLOCK;
  return int(l(block, BS / 4 - 3));
}

std::vector<int> Disk::list_dir(int block) const {
  std::vector<int> out;
  for (int i = 0; i < HT_SIZE; ++i)
    for (int b = int(l(block, 6 + i)); b > 0 && b < int(img_.size() / BS);
         b = int(l(b, BS / 4 - 4)))
      out.push_back(b);
  return out;
}

int Disk::find(const std::string& path) const {
  std::string p = path;
  size_t colon = p.find(':');
  if (colon != std::string::npos) p = p.substr(colon + 1);  // device/volume name
  int dir = ROOT_BLOCK;
  size_t start = 0;
  while (start <= p.size()) {
    size_t slash = p.find('/', start);
    std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    start = slash == std::string::npos ? p.size() + 1 : slash + 1;
    if (part.empty()) continue;
    if (!is_dir(dir)) return -1;
    // Hash as AmigaDOS does: case-insensitive.
    uint32_t h = uint32_t(part.size());
    for (char c : part) h = (h * 13 + uint32_t(std::toupper(uint8_t(c)))) & 0x7FF;
    int found = -1;
    for (int b = int(l(dir, 6 + int(h % HT_SIZE))); b > 0; b = int(l(b, BS / 4 - 4))) {
      std::string n = entry_name(b);
      if (n.size() == part.size() &&
          std::equal(n.begin(), n.end(), part.begin(), [](char a, char c) {
            return std::toupper(uint8_t(a)) == std::toupper(uint8_t(c));
          })) {
        found = b;
        break;
      }
    }
    if (found < 0) return -1;
    dir = found;
  }
  return dir;
}

bool Disk::read_file(int block, std::vector<uint8_t>& out) const {
  if (int32_t(l(block, 0)) != T_HEADER || int32_t(l(block, BS / 4 - 1)) != ST_FILE) return false;
  uint32_t size = file_size(block);
  out.clear();
  out.reserve(size);
  // OFS data blocks form a chain: +12 data size, +16 next data block.
  for (int b = int(l(block, 4)); b > 0 && out.size() < size; b = int(l(b, 4))) {
    if (int32_t(l(b, 0)) != T_DATA) return false;
    uint32_t n = std::min<uint32_t>(l(b, 3), 488);
    size_t o = size_t(b) * BS + 24;
    out.insert(out.end(), img_.begin() + long(o), img_.begin() + long(o + n));
  }
  out.resize(std::min<size_t>(out.size(), size));
  return out.size() == size;
}

}  // namespace amiga
