#include "ui/preferences.h"
#include "ui/layout.h"
#include "platform/asset_path.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace ui {

namespace {

constexpr const char* kFileName = "prefs.txt";

std::string PrefsPath() {
    const std::string& base = platform::AppSupportDir();
    if (base.empty()) return {};
    return base + kFileName;
}

float ClampScale(float v) {
    if (v < layout::kFontScaleMin) return layout::kFontScaleMin;
    if (v > layout::kFontScaleMax) return layout::kFontScaleMax;
    return v;
}

// Trim whitespace — atoi/atof reject values with trailing CR or surrounding spaces.
void Trim(std::string& s) {
    size_t b = 0;
    while (b < s.size() &&
           (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    size_t e = s.size();
    while (e > b &&
           (s[e - 1] == ' ' || s[e - 1] == '\t' ||
            s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    s = s.substr(b, e - b);
}

bool ParseBool(const std::string& v) {
    return (v == "1" || v == "true" || v == "True" || v == "TRUE");
}

}

void LoadPreferences(GuiState& s) {
    const std::string path = PrefsPath();
    if (path.empty()) return;

    std::ifstream in(path);
    if (!in) return;  // First launch — defaults stand.

    std::string line;
    while (std::getline(in, line)) {
        Trim(line);
        if (line.empty() || line[0] == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        Trim(key);
        Trim(val);
        if (key.empty()) continue;

        if (key == "chrome_scale") {
            s.chrome_scale = ClampScale((float)std::atof(val.c_str()));
        } else if (key == "font_scale") {
            s.font_scale = ClampScale((float)std::atof(val.c_str()));
        } else if (key == "palette") {
            int p = std::atoi(val.c_str());
            if (p < 0) p = 0;
            if (p >= (int)GuiState::PAL_COUNT) p = (int)GuiState::PAL_COUNT - 1;
            s.palette = (GuiState::Palette)p;
        } else if (key == "readonly_default") {
            s.readonly_default = ParseBool(val);
        } else if (key == "background_throttle") {
            s.background_throttle = ParseBool(val);
        }
        // Unknown keys tolerated — older binary shouldn't drop newer fields.
    }
}

void SavePreferences(const GuiState& s) {
    const std::string path = PrefsPath();
    if (path.empty()) return;

    // Temp-then-rename: POSIX rename is atomic on the same FS, so a mid-write crash can't truncate.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            std::fprintf(stderr,
                "[ui::SavePreferences] open failed for '%s'\n", tmp.c_str());
            return;
        }
        out << "# hxediter user preferences. Auto-generated; safe to edit.\n";
        out << "chrome_scale="        << s.chrome_scale          << '\n';
        out << "font_scale="          << s.font_scale            << '\n';
        out << "palette="             << (int)s.palette          << '\n';
        out << "readonly_default="    << (s.readonly_default ? 1 : 0)    << '\n';
        out << "background_throttle=" << (s.background_throttle ? 1 : 0) << '\n';
        if (!out) {
            std::fprintf(stderr,
                "[ui::SavePreferences] write failed for '%s'\n", tmp.c_str());
            return;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::fprintf(stderr,
            "[ui::SavePreferences] rename '%s' -> '%s' failed\n",
            tmp.c_str(), path.c_str());
    }
}

}
