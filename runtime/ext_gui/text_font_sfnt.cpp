/*
 * xbintsc GUI engine — sfnt metadata reader (see text_font_sfnt.h).
 */

#include "text_font_sfnt.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace xtgui {
namespace {

constexpr uint32_t tagOf(char a, char b, char c, char d) {
  return ((uint32_t)(unsigned char)a << 24) | ((uint32_t)(unsigned char)b << 16) |
         ((uint32_t)(unsigned char)c << 8) | (uint32_t)(unsigned char)d;
}

constexpr uint32_t kTagTtcf = tagOf('t', 't', 'c', 'f');
constexpr uint32_t kTagOtto = tagOf('O', 'T', 'T', 'O');
constexpr uint32_t kTagName = tagOf('n', 'a', 'm', 'e');
constexpr uint32_t kTagOs2 = tagOf('O', 'S', '/', '2');
constexpr uint32_t kTagHead = tagOf('h', 'e', 'a', 'd');
constexpr uint32_t kTagPost = tagOf('p', 'o', 's', 't');

class FileReader {
 public:
  FileReader() = default;
  ~FileReader() {
    if (file_ != nullptr) std::fclose(file_);
  }
  FileReader(const FileReader &) = delete;
  FileReader &operator=(const FileReader &) = delete;

  bool open(const std::string &path) {
    file_ = std::fopen(path.c_str(), "rb");
    if (file_ == nullptr) return false;
    if (std::fseek(file_, 0, SEEK_END) != 0) return false;
    const long length = std::ftell(file_);
    if (length <= 0) return false;
    size_ = (uint64_t)length;
    return true;
  }

  uint64_t size() const { return size_; }

  bool read(uint64_t offset, void *destination, size_t length) const {
    if (offset > size_ || length > size_ - offset) return false;
    if (std::fseek(file_, (long)offset, SEEK_SET) != 0) return false;
    return std::fread(destination, 1, length, file_) == length;
  }

  bool u16(uint64_t offset, uint16_t *out) const {
    unsigned char bytes[2];
    if (!read(offset, bytes, sizeof(bytes))) return false;
    *out = (uint16_t)((uint16_t)bytes[0] << 8 | bytes[1]);
    return true;
  }

  bool u32(uint64_t offset, uint32_t *out) const {
    unsigned char bytes[4];
    if (!read(offset, bytes, sizeof(bytes))) return false;
    *out = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
    return true;
  }

 private:
  FILE *file_ = nullptr;
  uint64_t size_ = 0;
};

struct TableRecord {
  uint32_t tag = 0;
  uint32_t offset = 0;
  uint32_t length = 0;
};

bool readTableRecords(const FileReader &reader, uint64_t face_offset, uint32_t *sfnt_tag,
                      std::vector<TableRecord> *tables) {
  if (!reader.u32(face_offset, sfnt_tag)) return false;
  uint16_t count = 0;
  if (!reader.u16(face_offset + 4, &count)) return false;
  if (count == 0 || count > 4096) return false;
  const uint64_t directory_size = (uint64_t)count * 16;
  if (face_offset > reader.size() || directory_size > reader.size() - face_offset ||
      12 + directory_size > reader.size() - face_offset) {
    return false;
  }
  std::vector<unsigned char> bytes((size_t)directory_size);
  if (!reader.read(face_offset + 12, bytes.data(), bytes.size())) return false;
  tables->clear();
  tables->reserve(count);
  for (uint16_t i = 0; i < count; i++) {
    const unsigned char *entry = bytes.data() + (size_t)i * 16;
    TableRecord record;
    record.tag = ((uint32_t)entry[0] << 24) | ((uint32_t)entry[1] << 16) |
                 ((uint32_t)entry[2] << 8) | (uint32_t)entry[3];
    record.offset = ((uint32_t)entry[8] << 24) | ((uint32_t)entry[9] << 16) |
                    ((uint32_t)entry[10] << 8) | (uint32_t)entry[11];
    record.length = ((uint32_t)entry[12] << 24) | ((uint32_t)entry[13] << 16) |
                    ((uint32_t)entry[14] << 8) | (uint32_t)entry[15];
    if (record.offset > reader.size() || record.length > reader.size() - record.offset) {
      continue;
    }
    tables->push_back(record);
  }
  return true;
}

const TableRecord *findTable(const std::vector<TableRecord> &tables, uint32_t tag) {
  for (const TableRecord &record : tables) {
    if (record.tag == tag && record.length > 0) return &record;
  }
  return nullptr;
}

void appendUtf8(std::string *out, uint32_t code_point) {
  if (code_point <= 0x7f) {
    out->push_back((char)code_point);
  } else if (code_point <= 0x7ff) {
    out->push_back((char)(0xc0 | (code_point >> 6)));
    out->push_back((char)(0x80 | (code_point & 0x3f)));
  } else if (code_point <= 0xffff) {
    out->push_back((char)(0xe0 | (code_point >> 12)));
    out->push_back((char)(0x80 | ((code_point >> 6) & 0x3f)));
    out->push_back((char)(0x80 | (code_point & 0x3f)));
  } else {
    out->push_back((char)(0xf0 | (code_point >> 18)));
    out->push_back((char)(0x80 | ((code_point >> 12) & 0x3f)));
    out->push_back((char)(0x80 | ((code_point >> 6) & 0x3f)));
    out->push_back((char)(0x80 | (code_point & 0x3f)));
  }
}

std::string decodeUtf16Be(const unsigned char *bytes, size_t length) {
  std::string out;
  out.reserve(length / 2);
  for (size_t i = 0; i + 1 < length; i += 2) {
    uint32_t unit = ((uint32_t)bytes[i] << 8) | bytes[i + 1];
    if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < length) {
      const uint32_t low = ((uint32_t)bytes[i + 2] << 8) | bytes[i + 3];
      if (low >= 0xdc00 && low <= 0xdfff) {
        appendUtf8(&out, 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
        i += 2;
        continue;
      }
    }
    if (unit == 0) break;
    appendUtf8(&out, unit >= 0xd800 && unit <= 0xdfff ? 0xfffd : unit);
  }
  return out;
}

std::string decodeNameBytes(const unsigned char *bytes, size_t length, uint16_t platform,
                            uint16_t encoding) {
  if (platform == 0 || platform == 3 || (platform == 2 && encoding == 1)) {
    return decodeUtf16Be(bytes, length);
  }
  /* Macintosh/legacy encodings are mostly ASCII for family names. Decode the
   * rest as Latin-1 so a name is still usable for exact matching. */
  std::string out;
  out.reserve(length);
  for (size_t i = 0; i < length; i++) {
    unsigned char c = bytes[i];
    if (c == 0) break;
    if (c < 0x80) {
      out.push_back((char)c);
    } else {
      appendUtf8(&out, c);
    }
  }
  return out;
}

std::string trim(const std::string &text) {
  size_t first = 0;
  size_t last = text.size();
  auto space = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
  };
  while (first < last && space((unsigned char)text[first])) first++;
  while (last > first && space((unsigned char)text[last - 1])) last--;
  return text.substr(first, last - first);
}

struct NameSet {
  std::string values[18];
  int priorities[18];

  NameSet() {
    for (int &priority : priorities) priority = 100;
  }

  void add(uint16_t id, const std::string &value, int priority) {
    if (id >= 18 || value.empty()) return;
    if (priority >= priorities[id]) return;
    values[id] = value;
    priorities[id] = priority;
  }
};

int namePriority(uint16_t platform, uint16_t encoding, uint16_t language) {
  if (platform == 3) {
    if (language == 0x0409) return 0;
    if (language == 0x0809 || language == 0x0c09 || language == 0x1009 || language == 0x1409) {
      return 1;
    }
    if (encoding == 1 || encoding == 10) return 4;
    return 6;
  }
  if (platform == 0) return 2;
  if (platform == 1 && language == 0) return 3;
  return 5;
}

bool readNames(const FileReader &reader, const TableRecord &table, NameSet *names) {
  uint16_t format = 0;
  uint16_t count = 0;
  uint16_t string_offset = 0;
  if (!reader.u16(table.offset, &format) || !reader.u16(table.offset + 2, &count) ||
      !reader.u16(table.offset + 4, &string_offset)) {
    return false;
  }
  if ((format != 0 && format != 1) || count == 0 || count > 10000) return false;
  if ((uint64_t)count * 12 > table.length) return false;
  std::vector<unsigned char> records((size_t)count * 12);
  if (!reader.read(table.offset + 6, records.data(), records.size())) return false;
  if (string_offset > table.length) return false;
  for (uint16_t i = 0; i < count; i++) {
    const unsigned char *entry = records.data() + (size_t)i * 12;
    const uint16_t platform = (uint16_t)((uint16_t)entry[0] << 8 | entry[1]);
    const uint16_t encoding = (uint16_t)((uint16_t)entry[2] << 8 | entry[3]);
    const uint16_t language = (uint16_t)((uint16_t)entry[4] << 8 | entry[5]);
    const uint16_t name_id = (uint16_t)((uint16_t)entry[6] << 8 | entry[7]);
    const uint16_t length = (uint16_t)((uint16_t)entry[8] << 8 | entry[9]);
    const uint16_t offset = (uint16_t)((uint16_t)entry[10] << 8 | entry[11]);
    if (name_id >= 18 || length == 0) continue;
    const uint64_t start = (uint64_t)table.offset + string_offset + offset;
    if (start > reader.size() || length > reader.size() - start) continue;
    std::vector<unsigned char> bytes(length);
    if (!reader.read(start, bytes.data(), bytes.size())) continue;
    names->add(name_id, trim(decodeNameBytes(bytes.data(), bytes.size(), platform, encoding)),
               namePriority(platform, encoding, language));
  }
  return true;
}

std::string lowerAscii(const std::string &text) {
  std::string out = text;
  for (char &c : out) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  }
  return out;
}

int inferWeight(const std::string &style) {
  const std::string text = lowerAscii(style);
  if (text.find("thin") != std::string::npos) return 100;
  if (text.find("extralight") != std::string::npos ||
      text.find("ultralight") != std::string::npos) {
    return 200;
  }
  if (text.find("semilight") != std::string::npos ||
      text.find("demilight") != std::string::npos) {
    return 350;
  }
  if (text.find("light") != std::string::npos) return 300;
  if (text.find("extrabold") != std::string::npos ||
      text.find("ultrabold") != std::string::npos) {
    return 800;
  }
  if (text.find("semibold") != std::string::npos ||
      text.find("demibold") != std::string::npos) {
    return 600;
  }
  if (text.find("black") != std::string::npos || text.find("heavy") != std::string::npos ||
      text.find("ultra") != std::string::npos) {
    return 900;
  }
  if (text.find("bold") != std::string::npos) return 700;
  if (text.find("medium") != std::string::npos) return 500;
  return 400;
}

bool looksMonospace(const std::string &family, const std::string &style) {
  const std::string text = lowerAscii(family + " " + style);
  return text.find("mono") != std::string::npos || text.find("courier") != std::string::npos ||
         text.find("consol") != std::string::npos || text.find("menlo") != std::string::npos ||
         text.find("monaco") != std::string::npos || text.find("code") != std::string::npos ||
         text.find("fixed") != std::string::npos || text.find("terminal") != std::string::npos;
}

std::vector<std::string> uniqueNames(const std::vector<std::string> &names) {
  std::vector<std::string> out;
  for (const std::string &name : names) {
    const std::string trimmed = trim(name);
    if (trimmed.empty()) continue;
    if (std::find(out.begin(), out.end(), trimmed) == out.end()) out.push_back(trimmed);
  }
  return out;
}

bool parseFace(const FileReader &reader, uint64_t face_offset, int face_index, SfntFace *face) {
  uint32_t sfnt_tag = 0;
  std::vector<TableRecord> tables;
  if (!readTableRecords(reader, face_offset, &sfnt_tag, &tables)) return false;

  NameSet names;
  if (const TableRecord *name_table = findTable(tables, kTagName)) {
    readNames(reader, *name_table, &names);
  }

  const std::string legacy_family = names.values[1];
  const std::string legacy_style = names.values[2];
  const std::string full_name = names.values[4];
  const std::string typographic_family = names.values[16];
  const std::string typographic_style = names.values[17];

  std::vector<std::string> candidates;
  if (!typographic_family.empty()) candidates.push_back(typographic_family);
  if (!legacy_family.empty()) candidates.push_back(legacy_family);
  if (candidates.empty() && !full_name.empty()) candidates.push_back(full_name);
  face->families = uniqueNames(candidates);
  if (!face->families.empty()) face->primary_family = face->families.front();
  face->index = face_index;
  face->style = typographic_family.empty() ? legacy_style : typographic_style;
  if (face->style.empty()) face->style = legacy_style;
  face->truetype = sfnt_tag != kTagOtto;

  face->weight = inferWeight(face->style);
  if (const TableRecord *os2 = findTable(tables, kTagOs2)) {
    uint16_t weight = 0;
    if (os2->length >= 6 && reader.u16(os2->offset + 4, &weight) && weight >= 1 && weight <= 1000) {
      face->weight = weight;
    }
  }

  bool italic = false;
  if (const TableRecord *os2 = findTable(tables, kTagOs2)) {
    uint16_t selection = 0;
    if (os2->length >= 64 && reader.u16(os2->offset + 62, &selection)) {
      italic = (selection & 0x0001) != 0;
    }
  }
  if (const TableRecord *head = findTable(tables, kTagHead)) {
    uint16_t mac_style = 0;
    if (head->length >= 46 && reader.u16(head->offset + 44, &mac_style)) {
      italic = italic || (mac_style & 0x0002) != 0;
    }
  }
  const std::string style_lower = lowerAscii(face->style);
  face->italic = italic || style_lower.find("italic") != std::string::npos ||
                 style_lower.find("oblique") != std::string::npos;

  bool fixed_pitch = false;
  if (const TableRecord *post = findTable(tables, kTagPost)) {
    uint32_t value = 0;
    if (post->length >= 16 && reader.u32(post->offset + 12, &value)) fixed_pitch = value != 0;
  }
  face->monospace = fixed_pitch || looksMonospace(face->primary_family, face->style);
  return true;
}

}  // namespace

bool xt_sfnt_read_faces(const std::string &path, std::vector<SfntFace> &out) {
  FileReader reader;
  if (!reader.open(path)) return false;
  uint32_t first_tag = 0;
  if (!reader.u32(0, &first_tag)) return false;
  if (first_tag != kTagTtcf) {
    SfntFace face;
    if (!parseFace(reader, 0, 0, &face)) return false;
    out.push_back(std::move(face));
    return true;
  }

  uint32_t count = 0;
  if (!reader.u32(8, &count) || count == 0 || count > 4096) return false;
  if (12 + (uint64_t)count * 4 > reader.size()) return false;
  bool any = false;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t offset = 0;
    if (!reader.u32(12 + (uint64_t)i * 4, &offset)) continue;
    SfntFace face;
    if (!parseFace(reader, offset, (int)i, &face)) continue;
    out.push_back(std::move(face));
    any = true;
  }
  return any;
}

}  // namespace xtgui
