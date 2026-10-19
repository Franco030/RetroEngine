#include "AssetCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace editor {

namespace {

namespace fs = std::filesystem;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void ScanDir(const fs::path& root, const char* sub, const std::vector<std::string>& extensions,
             std::vector<std::string>& out) {
    out.clear();
    std::error_code ec;
    const fs::path dir = root / sub;
    if (!fs::exists(dir, ec)) return;

    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = Lower(it->path().extension().string());
        if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end()) continue;
        out.push_back(fs::relative(it->path(), root, ec).generic_string());
    }
    std::sort(out.begin(), out.end());
}

} // namespace

void AssetCatalog::Scan(const std::string& root) {
    const fs::path r(root);
    ScanDir(r, "models", {".obj"}, models);
    ScanDir(r, "textures", {".png", ".jpg", ".jpeg", ".bmp", ".tga"}, textures);
    ScanDir(r, "audio", {".wav", ".ogg", ".mp3", ".flac"}, sounds);
}

} // namespace editor
