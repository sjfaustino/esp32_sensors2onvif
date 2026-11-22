#include "cam_node_images.h"
#include "cam_number.h"
#include <stdlib.h>

static const char UPLOAD[] = "nodes/upload.bin";

static std::string imagePath(uint16_t chip) { return "nodes/" + std::to_string(chip) + ".bin"; }
static std::string describePath(uint16_t chip) { return "nodes/" + std::to_string(chip) + ".txt"; }

void CamNodeImages::load() {
  images_.clear();
  std::vector<CamFsEntry> files;
  if (!storage_.list("nodes", files)) return;
  for (const CamFsEntry& e : files) {
    if (e.dir || e.name.size() < 5 || e.name.compare(e.name.size() - 4, 4, ".txt") != 0) continue;
    std::string text;
    if (!storage_.readFile("nodes/" + e.name, text, 256)) continue;
    CamNodeImage image;
    bool numbersOk = true;
    for (size_t at = 0; at < text.size();) {
      size_t end = text.find('\n', at);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(at, end - at);
      at = end + 1;
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = line.substr(0, eq), value = line.substr(eq + 1);
      if (key == "chip")
        numbersOk = numbersOk && camNumber(value, image.chip);
      else if (key == "version")
        image.version = value;
      else if (key == "number")
        numbersOk = numbersOk && camNumber(value, image.number);
      else if (key == "size")
        numbersOk = numbersOk && camNumber(value, image.size);
    }
    if (!numbersOk || image.version.empty() || !image.number || storage_.fileSize(imagePath(image.chip)) != image.size || forChip(image.chip))
      continue;
    images_.push_back(image);
  }
}

const CamNodeImage* CamNodeImages::forChip(uint16_t chip) const {
  for (const CamNodeImage& i : images_)
    if (i.chip == chip) return &i;
  return nullptr;
}

bool CamNodeImages::read(uint16_t chip, uint32_t offset, uint8_t* out, size_t len) const {
  return storage_.readAt(imagePath(chip), offset, (char*)out, len);
}

bool CamNodeImages::remove(uint16_t chip) {
  bool ok = storage_.removeTree(describePath(chip)) && storage_.removeTree(imagePath(chip));
  load();
  return ok;
}

void CamNodeImages::json(CamJson& j) const {
  j.array("nodeFirmware");
  for (const CamNodeImage& i : images_)
    j.object().num("chip", i.chip).str("name", camChipName(i.chip)).str("version", i.version).num("size", i.size).end();
  j.end();
}

bool CamNodeImages::begin() {
  abandon();
  upload_ = storage_.openWrite(UPLOAD);
  return upload_ != nullptr;
}

bool CamNodeImages::write(const char* data, size_t n) { return upload_ && fwrite(data, 1, n, upload_) == n; }

void CamNodeImages::abandon() {
  if (upload_) (void)fclose(upload_);  // being thrown away
  upload_ = nullptr;
  storage_.removeTree(UPLOAD);
}

// Checked whole once it is on storage: it must be a node's firmware, which says its version, and not the hub's own.
CamNodeImages::Result CamNodeImages::finish(bool complete, CamNodeImage& image) {
  bool closed = upload_ && fclose(upload_) == 0;
  upload_ = nullptr;
  if (!complete || !closed) {
    storage_.removeTree(UPLOAD);
    return NOT_SAVED;
  }
  const CamStorage* storage = &storage_;
  auto reader = [storage](size_t offset, char* out, size_t len) { return storage->readAt(UPLOAD, offset, out, len); };
  if (!camCheckNodeImage(reader, storage_.fileSize(UPLOAD), image)) {
    storage_.removeTree(UPLOAD);
    return NOT_AN_IMAGE;
  }
  std::string text = "chip=" + std::to_string(image.chip) + "\nversion=" + image.version + "\nnumber=" + std::to_string(image.number) +
                     "\nsize=" + std::to_string(image.size) + "\n";
  // The old description is removed before the file is replaced, and the new one written after: a power cut between
  // leaves a file with no description, which is ignored, never a file described as another.
  storage_.removeTree(describePath(image.chip));
  bool saved = storage_.renameFile(UPLOAD, imagePath(image.chip)) && storage_.writeFile(describePath(image.chip), text.data(), text.size());
  load();
  return saved ? SAVED : NOT_SAVED;
}
