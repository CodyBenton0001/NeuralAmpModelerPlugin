#pragma once

// NAM Tone Gallery
//
// A visual browser for a local library of NAM models & IRs, with photos and
// metadata pulled from TONE3000 (see tools/Add-Tone.ps1).
//
// Library layout (default root: Documents/NAM Tones; override by writing a
// single-line path into ~/.nam_tone_gallery):
//
// NAM Tones/
// Some Tone Name/
// tone.json (optional metadata: name, author, description, gear_type,
// tags, image, model, ir)
// cover.jpg (optional photo)
// model.nam / cab.wav
//
// This file is header-only on purpose: including it from NeuralAmpModeler.cpp
// (after NeuralAmpModelerControls.h) plus attaching two controls is the only
// integration needed, so this fork stays easy to rebase on upstream.
//
// NOTE: nlohmann::json is already visible here via the NAM Core headers
// included from NeuralAmpModeler.h (same mechanism Unserialization.cpp uses).

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "IControls.h"

using namespace iplug;
using namespace igraphics;

// Control tags for the gallery/sidebar/favorites. Deliberately defined here
// (not in ECtrlTags in NeuralAmpModeler.h) with values far above kNumCtrlTags,
// so that this fork doesn't have to touch the upstream header at all.
const int kCtrlTagToneGallery = 1001;
const int kCtrlTagToneSidebar = 1002;
const int kCtrlTagFavoritesBar = 1003;
const int kCtrlTagToneDetail = 1004;
const int kCtrlTagRackView = 1005;
// 1006 = kCtrlTagTone3000 (NAMTone3000Browser.h)
const int kCtrlTagChainView = 1007; // NAMChainView.h
const int kCtrlTagChainBanner = 1008; // (retired - kept so the tag numbering stays stable)
const int kCtrlTagMorphCards = 1009; // NAMChainView.h (main-view A/B tone cards)

// Window layout: the always-visible tone list on the left and the favorites
// bar under the main UI. The main plugin UI keeps its stock 600x400 size;
// config.h grows PLUG_WIDTH/PLUG_HEIGHT by these amounts.
const float kSidebarWidth = 210.0f;
const float kFavoritesBarHeight = 56.0f;
const float kDetailPanelWidth = 240.0f;
const float kRackViewHeight = 140.0f;

namespace tonegallery
{

// UTF-8 <-> std::filesystem::path helpers that compile under both C++17 and
// C++20. (In C++20, path::u8string() returns std::u8string -- a different
// character type -- and std::filesystem::u8path is deprecated.)
inline std::string PathToUTF8(const std::filesystem::path& p)
{
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

inline std::filesystem::path UTF8ToPath(const std::string& s)
{
#if defined(__cpp_char8_t)
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
#else
  return std::filesystem::u8path(s);
#endif
}

enum class GearType
{
  Amp = 0,
  AmpCab,
  IR,
  Pedal,
  Other,
  NumGearTypes
};

// What the filter chips/tabs cycle through. kFilterAll shows everything.
enum EGalleryFilter
{
  kFilterAll = 0,
  kFilterAmpCab,
  kFilterAmp,
  kFilterIR,
  kFilterPedal,
  kFilterOther,
  kNumFilters
};

// Shared labels for the sidebar chips and the gallery tabs.
inline const char* FilterLabel(int filter)
{
  switch (filter)
  {
    case kFilterAmpCab: return "AMP+CAB";
    case kFilterAmp: return "AMPS";
    case kFilterIR: return "CABS/IR";
    case kFilterPedal: return "PEDALS";
    case kFilterOther: return "OTHER";
    case kFilterAll:
    default: return "ALL";
  }
}

struct ToneEntry
{
  std::string directory; // absolute path of the tone's folder
  std::string name;
  std::string author;
  std::string description;
  GearType gearType = GearType::Other;
  std::vector<std::string> tags;
  std::string imagePath; // absolute path, empty if none
  std::string modelPath; // default .nam (absolute), empty if none
  std::string irPath; // default .wav (absolute), empty if none
  std::vector<std::string> models; // ALL .nam files (absolute, sorted)
  std::vector<std::string> irs; // ALL .wav files (absolute, sorted)
  std::string url; // TONE3000 page, if known
};

// Runtime accent color (defined after GetToneLibraryRoot).
inline IColor AccentColor();

inline GearType GearTypeFromString(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  if (s == "amp" || s == "amp_head" || s == "head")
    return GearType::Amp;
  if (s == "amp_cab" || s == "amp+cab" || s == "ampcab" || s == "full_rig" || s == "rig")
    return GearType::AmpCab;
  if (s == "ir" || s == "cab" || s == "cabinet" || s == "impulse_response")
    return GearType::IR;
  if (s == "pedal" || s == "stomp" || s == "stompbox" || s == "drive" || s == "fuzz")
    return GearType::Pedal;
  return GearType::Other;
}

inline const char* GearTypeChipLabel(GearType t)
{
  switch (t)
  {
    case GearType::Amp: return "AMP";
    case GearType::AmpCab: return "AMP+CAB";
    case GearType::IR: return "IR";
    case GearType::Pedal: return "PEDAL";
    default: return "TONE";
  }
}

inline IColor GearTypeColor(GearType t)
{
  switch (t)
  {
    case GearType::Amp: return IColor(255, 219, 148, 43); // orange
    case GearType::AmpCab: return AccentColor();
    case GearType::IR: return IColor(255, 80, 133, 232); // blue
    case GearType::Pedal: return IColor(255, 186, 85, 211); // purple
    default: return IColor(255, 120, 120, 120);
  }
}

inline std::string Ellipsize(const std::string& s, size_t maxChars)
{
  if (s.length() <= maxChars)
    return s;
  return s.substr(0, maxChars - 3) + "...";
}

inline bool MatchesFilter(GearType t, int filter)
{
  switch (filter)
  {
    case kFilterAmpCab: return t == GearType::AmpCab;
    case kFilterAmp: return t == GearType::Amp;
    case kFilterIR: return t == GearType::IR;
    case kFilterPedal: return t == GearType::Pedal;
    case kFilterOther: return t == GearType::Other;
    case kFilterAll:
    default: return true;
  }
}

inline std::filesystem::path GetHomeDir()
{
#ifdef OS_WIN
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  return home ? UTF8ToPath(std::string(home)) : std::filesystem::path();
}

// Root of the tone library. Default: <home>/Documents/NAM Tones
// Override: a text file <home>/.nam_tone_gallery whose first line is a path.
inline std::filesystem::path GetToneLibraryRoot()
{
  namespace fs = std::filesystem;
  const fs::path home = GetHomeDir();
  if (home.empty())
    return fs::path();

  const fs::path overrideFile = home / ".nam_tone_gallery";
  try
  {
    if (fs::exists(overrideFile))
    {
      std::ifstream f(overrideFile);
      std::string line;
      if (std::getline(f, line))
      {
        // Trim whitespace / quotes
        auto NotSpace = [](unsigned char c) { return !std::isspace(c) && c != '"'; };
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), NotSpace));
        line.erase(std::find_if(line.rbegin(), line.rend(), NotSpace).base(), line.end());
        if (!line.empty())
          return UTF8ToPath(line);
      }
    }
  }
  catch (const std::exception&)
  {
    // fall through to default
  }
  return home / "Documents" / "NAM Tones";
}

// --- Accent color: runtime-changeable, persisted to theme.json in the
// library root. The default is the compiled-in theme color.
inline IColor& MutableAccent()
{
  static IColor sAccent = []() {
    IColor c = PluginColors::NAM_THEMECOLOR;
    try
    {
      const std::filesystem::path p = GetToneLibraryRoot() / "theme.json";
      if (std::filesystem::exists(p))
      {
        std::ifstream f(p);
        nlohmann::json j = nlohmann::json::parse(f, nullptr, true, true);
        if (j.contains("accent") && j["accent"].is_string())
        {
          const std::string code = j["accent"].get<std::string>();
          if (code.size() == 7 && code[0] == '#')
          {
            const long v = std::strtol(code.c_str() + 1, nullptr, 16);
            c = IColor(255, (int)((v >> 16) & 0xFF), (int)((v >> 8) & 0xFF), (int)(v & 0xFF));
          }
        }
      }
    }
    catch (const std::exception&)
    {
    }
    return c;
  }();
  return sAccent;
}

inline IColor AccentColor()
{
  return MutableAccent();
}

// Read-modify-write a single key in theme.json (so the accent and the UI
// scale don't clobber each other).
inline void SaveThemeKey(const char* key, const nlohmann::json& value)
{
  try
  {
    nlohmann::json j;
    const std::filesystem::path p = GetToneLibraryRoot() / "theme.json";
    if (std::filesystem::exists(p))
    {
      try
      {
        std::ifstream f(p);
        j = nlohmann::json::parse(f, nullptr, true, true);
      }
      catch (const std::exception&)
      {
        j = nlohmann::json::object();
      }
    }
    j[key] = value;
    std::filesystem::create_directories(GetToneLibraryRoot());
    std::ofstream f(p);
    f << j.dump(2);
  }
  catch (const std::exception&)
  {
  }
}

inline void SetAccentColor(const IColor& c)
{
  MutableAccent() = c;
  char code[10];
  snprintf(code, sizeof(code), "#%02X%02X%02X", c.R, c.G, c.B);
  SaveThemeKey("accent", std::string(code));
}

// --- UI scale: lets the window grow to (nearly) full screen, especially in
// the standalone app. Persisted next to the accent in theme.json.
inline double LoadSavedUIScale()
{
  try
  {
    const std::filesystem::path p = GetToneLibraryRoot() / "theme.json";
    if (std::filesystem::exists(p))
    {
      std::ifstream f(p);
      nlohmann::json j = nlohmann::json::parse(f, nullptr, true, true);
      if (j.contains("ui_scale") && j["ui_scale"].is_number())
        return std::min(2.5, std::max(0.75, j["ui_scale"].get<double>()));
    }
  }
  catch (const std::exception&)
  {
  }
  return 1.0;
}

inline void SaveUIScale(double scale)
{
  SaveThemeKey("ui_scale", scale);
}

inline bool HasExtension(const std::filesystem::path& p, const char* extLower)
{
  std::string e = PathToUTF8(p.extension());
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return e == extLower;
}

// Scan one tone folder into an entry. Returns false if the folder holds
// neither a model nor an IR.
inline bool ScanToneFolder(const std::filesystem::path& dir, ToneEntry& entry)
{
  namespace fs = std::filesystem;
  entry = ToneEntry();
  entry.directory = PathToUTF8(dir);
  entry.name = PathToUTF8(dir.filename());

  std::vector<fs::path> namFiles, wavFiles, imageFiles;
  try
  {
    for (const auto& item : fs::directory_iterator(dir))
    {
      if (!item.is_regular_file())
        continue;
      const fs::path& p = item.path();
      if (HasExtension(p, ".nam"))
        namFiles.push_back(p);
      else if (HasExtension(p, ".wav"))
        wavFiles.push_back(p);
      else if (HasExtension(p, ".jpg") || HasExtension(p, ".jpeg") || HasExtension(p, ".png"))
        imageFiles.push_back(p);
    }
  }
  catch (const std::exception&)
  {
    return false;
  }
  std::sort(namFiles.begin(), namFiles.end());
  std::sort(wavFiles.begin(), wavFiles.end());
  std::sort(imageFiles.begin(), imageFiles.end());

  bool haveGearType = false;

  // Metadata, if present
  const fs::path metaPath = dir / "tone.json";
  if (fs::exists(metaPath))
  {
    try
    {
      std::ifstream f(metaPath);
      nlohmann::json j = nlohmann::json::parse(f, nullptr, true, true); // allow comments
      auto GetStr = [&j](const char* key) -> std::string {
        if (j.contains(key) && j[key].is_string())
          return j[key].get<std::string>();
        return "";
      };
      if (!GetStr("name").empty())
        entry.name = GetStr("name");
      entry.author = GetStr("author");
      entry.description = GetStr("description");
      const std::string gearStr = GetStr("gear_type");
      if (!gearStr.empty())
      {
        entry.gearType = GearTypeFromString(gearStr);
        haveGearType = true;
      }
      if (j.contains("tags") && j["tags"].is_array())
        for (const auto& t : j["tags"])
          if (t.is_string())
            entry.tags.push_back(t.get<std::string>());
      const std::string image = GetStr("image");
      if (!image.empty() && fs::exists(dir / UTF8ToPath(image)))
        entry.imagePath = PathToUTF8(dir / UTF8ToPath(image));
      const std::string model = GetStr("model");
      if (!model.empty() && fs::exists(dir / UTF8ToPath(model)))
        entry.modelPath = PathToUTF8(dir / UTF8ToPath(model));
      const std::string ir = GetStr("ir");
      if (!ir.empty() && fs::exists(dir / UTF8ToPath(ir)))
        entry.irPath = PathToUTF8(dir / UTF8ToPath(ir));
      entry.url = GetStr("url");
    }
    catch (const std::exception&)
    {
      // Bad JSON: keep going with inferred values.
    }
  }

  // All variants (models & IRs), sorted.
  for (const auto& p : namFiles)
    entry.models.push_back(PathToUTF8(p));
  for (const auto& p : wavFiles)
    entry.irs.push_back(PathToUTF8(p));

  // Fill in anything the metadata didn't provide.
  if (entry.imagePath.empty() && !imageFiles.empty())
    entry.imagePath = PathToUTF8(imageFiles.front());
  if (entry.modelPath.empty() && !namFiles.empty())
    entry.modelPath = PathToUTF8(namFiles.front());
  if (entry.irPath.empty() && !wavFiles.empty())
    entry.irPath = PathToUTF8(wavFiles.front());
  if (!haveGearType)
  {
    if (!entry.modelPath.empty() && !entry.irPath.empty())
      entry.gearType = GearType::AmpCab;
    else if (!entry.modelPath.empty())
      entry.gearType = GearType::Amp;
    else if (!entry.irPath.empty())
      entry.gearType = GearType::IR;
    else
      entry.gearType = GearType::Other;
  }

  return !(entry.modelPath.empty() && entry.irPath.empty());
}

inline std::vector<ToneEntry> ScanToneLibrary(const std::filesystem::path& root)
{
  namespace fs = std::filesystem;
  std::vector<ToneEntry> entries;
  try
  {
    if (root.empty())
      return entries;
    if (!fs::exists(root))
      fs::create_directories(root); // so the user can find where to put tones
    for (const auto& item : fs::directory_iterator(root))
    {
      if (!item.is_directory())
        continue;
      ToneEntry entry;
      if (ScanToneFolder(item.path(), entry))
        entries.push_back(std::move(entry));
    }
  }
  catch (const std::exception&)
  {
    // Leave whatever we managed to collect.
  }
  std::sort(entries.begin(), entries.end(), [](const ToneEntry& a, const ToneEntry& b) {
    auto Lower = [](std::string s) {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
    };
    return Lower(a.name) < Lower(b.name);
  });
  return entries;
}

// --- Favorites persistence (favorites.json in the library root) ------------

inline std::filesystem::path FavoritesFilePath()
{
  return GetToneLibraryRoot() / "favorites.json";
}

inline std::vector<std::string> LoadFavorites()
{
  std::vector<std::string> slots(3, "");
  try
  {
    const std::filesystem::path p = FavoritesFilePath();
    if (std::filesystem::exists(p))
    {
      std::ifstream f(p);
      nlohmann::json j = nlohmann::json::parse(f, nullptr, true, true);
      if (j.contains("slots") && j["slots"].is_array())
      {
        for (size_t i = 0; i < slots.size() && i < j["slots"].size(); i++)
          if (j["slots"][i].is_string())
            slots[i] = j["slots"][i].get<std::string>();
      }
    }
  }
  catch (const std::exception&)
  {
    // Corrupt favorites file: start fresh.
  }
  return slots;
}

inline void SaveFavorites(const std::vector<std::string>& slots)
{
  try
  {
    nlohmann::json j;
    j["slots"] = slots;
    std::filesystem::create_directories(GetToneLibraryRoot());
    std::ofstream f(FavoritesFilePath());
    f << j.dump(2);
  }
  catch (const std::exception&)
  {
  }
}

// Stage a tone's files through the same completion handlers the stock file
// browser buttons use. Shared by the gallery grid, the sidebar and the
// favorites bar.
inline void LoadToneEntryFiles(const ToneEntry& entry, const IFileDialogCompletionHandlerFunc& loadModelFunc,
                               const IFileDialogCompletionHandlerFunc& loadIRFunc)
{
  if (!entry.modelPath.empty() && entry.gearType != GearType::IR && loadModelFunc)
  {
    WDL_String fileName(entry.modelPath.c_str());
    WDL_String path(entry.directory.c_str());
    loadModelFunc(fileName, path);
  }
  if (!entry.irPath.empty() && (entry.gearType == GearType::IR || entry.gearType == GearType::AmpCab) && loadIRFunc)
  {
    WDL_String fileName(entry.irPath.c_str());
    WDL_String path(entry.directory.c_str());
    loadIRFunc(fileName, path);
  }
}

// --- "Now playing" plumbing -----------------------------------------------
// Controls that show the currently-loaded tone implement this and are poked
// through their control tags whenever something loads a tone.
class INowPlayingListener
{
public:
  virtual ~INowPlayingListener() {}
  virtual void SetNowPlaying(const ToneEntry& entry, const std::string& modelPath, const std::string& irPath) = 0;
};

inline void NotifyNowPlaying(IGraphics* ui, const ToneEntry& entry, const std::string& modelPath,
                             const std::string& irPath, bool force = false)
{
  if (ui == nullptr)
    return;
  // Tone Gallery fork: while a chain unit other than the main one is being
  // edited, tone loads land in that CHAIN SLOT -- they are not the new main
  // tone, so the main "now playing" displays (unit-1 screen, sidebar glow,
  // favorites, detail LED, rack screen) must NOT update. `force` is used by
  // the plugin's own restore code, which always describes the real main tone.
  if (!force)
  {
    if (auto* pPlug = static_cast<PLUG_CLASS_NAME*>(ui->GetDelegate()))
    {
      // Tone Morph: loads targeting a unit's B side (mChainEditTargetB) must
      // also leave the main "now playing" displays alone -- even for unit 0.
      if (pPlug->mChainEditSlot >= 1 || pPlug->mChainEditTargetB)
      {
        // Still repaint the chain view so the edited unit's screen refreshes.
        if (IControl* pChain = ui->GetControlWithTag(kCtrlTagChainView))
          pChain->SetDirty(false);
        return;
      }
    }
  }
  const int tags[] = {kCtrlTagToneSidebar, kCtrlTagFavoritesBar, kCtrlTagToneDetail,
                      kCtrlTagRackView,    kCtrlTagChainView,    kCtrlTagMorphCards};
  for (int tag : tags)
  {
    if (IControl* pControl = ui->GetControlWithTag(tag))
    {
      if (auto* pListener = dynamic_cast<INowPlayingListener*>(pControl))
        pListener->SetNowPlaying(entry, modelPath, irPath);
    }
  }
}

// Greedy word-wrap into at most maxLines lines of ~maxChars characters.
inline std::vector<std::string> WrapLines(const std::string& s, size_t maxChars, size_t maxLines)
{
  std::vector<std::string> lines;
  std::string remaining = s;
  while (!remaining.empty() && lines.size() < maxLines)
  {
    if (remaining.length() <= maxChars || lines.size() == maxLines - 1)
    {
      lines.push_back(lines.size() == maxLines - 1 ? Ellipsize(remaining, maxChars) : remaining);
      break;
    }
    size_t breakAt = remaining.rfind(' ', maxChars);
    if (breakAt == std::string::npos || breakAt < maxChars / 2)
      breakAt = maxChars;
    lines.push_back(remaining.substr(0, breakAt));
    remaining = remaining.substr(breakAt == maxChars ? breakAt : breakAt + 1);
  }
  return lines;
}

} // namespace tonegallery

// ===========================================================================
// Controls
// ===========================================================================

// The top-left button that opens the gallery: draws a little 2x2 grid icon.
class NAMGalleryButtonControl : public IControl
{
public:
  NAMGalleryButtonControl(const IRECT& bounds, IActionFunction af)
  : IControl(bounds, af)
  {
    SetTooltip("Tone Gallery");
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillEllipse(PluginColors::MOUSEOVER, mRECT);

    const IRECT icon = mRECT.GetCentredInside(16.0f, 16.0f);
    const float cellSize = 7.0f;
    const float gap = 2.0f;
    const IColor color = PluginColors::NAM_THEMEFONTCOLOR;
    for (int row = 0; row < 2; row++)
    {
      for (int col = 0; col < 2; col++)
      {
        const float x = icon.L + col * (cellSize + gap);
        const float y = icon.T + row * (cellSize + gap);
        g.FillRoundRect(color, IRECT(x, y, x + cellSize, y + cellSize), 1.5f);
      }
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    SetDirty(true); // fires the action function
  }
};

// Segmented filter tabs: All | Amps | Amp+Cab | Cabs & IRs
class NAMGalleryFilterControl : public IControl
{
public:
  NAMGalleryFilterControl(const IRECT& bounds, std::function<void(int)> onChanged)
  : IControl(bounds)
  , mOnChanged(onChanged)
  {
  }

  int GetFilter() const { return mFilter; }

  void Draw(IGraphics& g) override
  {
    const IColor active = tonegallery::AccentColor();
    const IColor inactive = tonegallery::AccentColor().WithOpacity(0.15f);
    const IText activeText(11.0f, COLOR_BLACK, "Inter-Regular", EAlign::Center, EVAlign::Middle);
    const IText inactiveText(11.0f, PluginColors::NAM_THEMEFONTCOLOR, "Inter-Regular", EAlign::Center, EVAlign::Middle);

    for (int i = 0; i < tonegallery::kNumFilters; i++)
    {
      const IRECT seg = SegmentRect(i);
      const bool isActive = (i == mFilter);
      const bool isOver = (i == mMouseOverIdx);
      g.FillRoundRect(isActive ? active : inactive, seg, 4.0f);
      if (isOver && !isActive)
        g.FillRoundRect(PluginColors::MOUSEOVER, seg, 4.0f);
      g.DrawText(isActive ? activeText : inactiveText, tonegallery::FilterLabel(i), seg);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    for (int i = 0; i < tonegallery::kNumFilters; i++)
    {
      if (SegmentRect(i).Contains(x, y))
      {
        if (mFilter != i)
        {
          mFilter = i;
          if (mOnChanged)
            mOnChanged(mFilter);
          SetDirty(false);
        }
        return;
      }
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    int over = -1;
    for (int i = 0; i < tonegallery::kNumFilters; i++)
      if (SegmentRect(i).Contains(x, y))
        over = i;
    if (over != mMouseOverIdx)
    {
      mMouseOverIdx = over;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mMouseOverIdx = -1;
    SetDirty(false);
  }

private:
  IRECT SegmentRect(int i) const
  {
    const float gap = 6.0f;
    const float w = (mRECT.W() - gap * (tonegallery::kNumFilters - 1)) / tonegallery::kNumFilters;
    const float x = mRECT.L + i * (w + gap);
    return IRECT(x, mRECT.T, x + w, mRECT.B);
  }

  int mFilter = tonegallery::kFilterAll;
  int mMouseOverIdx = -1;
  std::function<void(int)> mOnChanged;
};

// The scrollable grid of tone tiles.
class NAMToneGridControl : public IControl
{
public:
  static constexpr int kNumCols = 3;
  static constexpr float kTileGap = 10.0f;
  static constexpr float kTileHeight = 150.0f;
  static constexpr float kImageHeight = 92.0f;

  NAMToneGridControl(const IRECT& bounds, std::function<void(const tonegallery::ToneEntry&)> onSelect)
  : IControl(bounds)
  , mOnSelect(onSelect)
  {
  }

  void SetEntries(std::vector<tonegallery::ToneEntry> entries, const std::string& libraryRoot)
  {
    mEntries = std::move(entries);
    mLibraryRoot = libraryRoot;
    ApplyFilter();
  }

  void SetFilter(int filter)
  {
    mFilter = filter;
    ApplyFilter();
  }

  void Draw(IGraphics& g) override
  {
    g.PathClipRegion(mRECT);

    if (mFiltered.empty())
    {
      const IText msgText(15.0f, PluginColors::HELP_TEXT, "Inter-Regular", EAlign::Center, EVAlign::Middle);
      std::string msg;
      if (mEntries.empty())
        msg = std::string("No tones found.\nPut tone folders in: ") + mLibraryRoot
              + "\n(Use the Add-Tone helper to grab photos & info from TONE3000.)";
      else
        msg = "No tones match this filter.";
      // Draw line by line; DrawText doesn't handle newlines.
      std::stringstream ss(msg);
      std::string line;
      int i = 0;
      while (std::getline(ss, line, '\n'))
      {
        g.DrawText(msgText, line.c_str(), mRECT.GetMidVPadded(40.0f).SubRectVertical(3, i));
        i++;
      }
    }
    else
    {
      for (int i = 0; i < (int)mFiltered.size(); i++)
      {
        const IRECT tile = TileRect(i);
        if (tile.B < mRECT.T || tile.T > mRECT.B)
          continue;
        DrawTile(g, mEntries[mFiltered[i]], tile, i == mMouseOverTile);
      }

      // Scrollbar
      const float contentH = ContentHeight();
      if (contentH > mRECT.H())
      {
        const float frac = mRECT.H() / contentH;
        const float barH = std::max(20.0f, frac * mRECT.H());
        const float travel = mRECT.H() - barH;
        const float pos = (mScroll / (contentH - mRECT.H())) * travel;
        const IRECT bar(mRECT.R - 4.0f, mRECT.T + pos, mRECT.R - 1.0f, mRECT.T + pos + barH);
        g.FillRoundRect(tonegallery::AccentColor().WithOpacity(0.5f), bar, 1.5f);
      }
    }

    g.PathClipRegion(); // reset clip
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int idx = TileIndexAt(x, y);
    if (idx >= 0 && mOnSelect)
      mOnSelect(mEntries[mFiltered[idx]]);
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override { ScrollBy(-d * 40.0f); }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    const int idx = TileIndexAt(x, y);
    if (idx != mMouseOverTile)
    {
      mMouseOverTile = idx;
      if (idx >= 0)
      {
        const tonegallery::ToneEntry& entry = mEntries[mFiltered[idx]];
        SetTooltip(entry.description.empty() ? entry.name.c_str() : entry.description.c_str());
      }
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mMouseOverTile = -1;
    SetDirty(false);
  }

private:
  float ContentHeight() const
  {
    const int numRows = ((int)mFiltered.size() + kNumCols - 1) / kNumCols;
    return numRows * (kTileHeight + kTileGap);
  }

  void ScrollBy(float amount)
  {
    const float maxScroll = std::max(0.0f, ContentHeight() - mRECT.H());
    const float newScroll = std::min(maxScroll, std::max(0.0f, mScroll + amount));
    if (newScroll != mScroll)
    {
      mScroll = newScroll;
      SetDirty(false);
    }
  }

  void ApplyFilter()
  {
    mFiltered.clear();
    for (int i = 0; i < (int)mEntries.size(); i++)
      if (tonegallery::MatchesFilter(mEntries[i].gearType, mFilter))
        mFiltered.push_back(i);
    mScroll = 0.0f;
    mMouseOverTile = -1;
    SetDirty(false);
  }

  IRECT TileRect(int filteredIdx) const
  {
    const int col = filteredIdx % kNumCols;
    const int row = filteredIdx / kNumCols;
    const float tileW = (mRECT.W() - (kNumCols - 1) * kTileGap - 8.0f /*scrollbar*/) / kNumCols;
    const float x = mRECT.L + col * (tileW + kTileGap);
    const float y = mRECT.T + row * (kTileHeight + kTileGap) - mScroll;
    return IRECT(x, y, x + tileW, y + kTileHeight);
  }

  int TileIndexAt(float x, float y) const
  {
    for (int i = 0; i < (int)mFiltered.size(); i++)
    {
      const IRECT tile = TileRect(i);
      if (tile.B < mRECT.T || tile.T > mRECT.B)
        continue;
      if (tile.Contains(x, y))
        return i;
    }
    return -1;
  }

  static std::string Ellipsize(const std::string& s, size_t maxChars)
  {
    if (s.length() <= maxChars)
      return s;
    return s.substr(0, maxChars - 3) + "...";
  }

  IBitmap* GetImage(const std::string& path)
  {
    if (path.empty())
      return nullptr;
    auto found = mImageCache.find(path);
    if (found != mImageCache.end())
      return found->second.GetAPIBitmap() ? &found->second : nullptr;
    IBitmap bitmap;
    try
    {
      if (std::filesystem::exists(tonegallery::UTF8ToPath(path)))
        bitmap = GetUI()->LoadBitmap(path.c_str());
    }
    catch (const std::exception&)
    {
      // keep an invalid bitmap in the cache so we don't retry every frame
    }
    auto inserted = mImageCache.insert({path, bitmap});
    return inserted.first->second.GetAPIBitmap() ? &inserted.first->second : nullptr;
  }

  void DrawTile(IGraphics& g, const tonegallery::ToneEntry& entry, const IRECT& tile, bool mouseOver)
  {
    // Panel
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.55f), tile, 6.0f);

    // Image
    const IRECT imageArea = tile.GetFromTop(kImageHeight).GetPadded(-4.0f);
    IBitmap* pBitmap = GetImage(entry.imagePath);
    if (pBitmap != nullptr && pBitmap->W() > 0 && pBitmap->H() > 0)
    {
      // Aspect-fit inside imageArea (DrawFittedBitmap would stretch).
      const float bmpAspect = (float)pBitmap->W() / (float)pBitmap->H();
      const float areaAspect = imageArea.W() / imageArea.H();
      IRECT fit = imageArea;
      if (bmpAspect > areaAspect)
        fit = imageArea.GetMidVPadded(0.5f * imageArea.W() / bmpAspect);
      else
        fit = imageArea.GetMidHPadded(0.5f * imageArea.H() * bmpAspect);
      g.DrawFittedBitmap(*pBitmap, fit);
    }
    else
    {
      g.FillRoundRect(tonegallery::AccentColor().WithOpacity(0.08f), imageArea, 4.0f);
      const IText placeholderText(
        24.0f, tonegallery::AccentColor().WithOpacity(0.6f), "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(placeholderText, "NAM", imageArea);
    }

    // Name
    const IRECT nameArea = tile.GetReducedFromTop(kImageHeight).GetFromTop(20.0f).GetHPadded(-6.0f);
    const IText nameText(14.0f, COLOR_WHITE, "Inter-Regular", EAlign::Near, EVAlign::Middle);
    g.DrawText(nameText, Ellipsize(entry.name, 24).c_str(), nameArea);

    // Author + gear chip row
    const IRECT infoArea = tile.GetFromBottom(24.0f).GetPadded(-6.0f);
    const IText authorText(11.0f, PluginColors::HELP_TEXT, "Inter-Regular", EAlign::Near, EVAlign::Middle);
    if (!entry.author.empty())
      g.DrawText(authorText, Ellipsize("by " + entry.author, 18).c_str(), infoArea.GetReducedFromRight(64.0f));

    // Chip
    const IRECT chipArea = infoArea.GetFromRight(60.0f);
    const IColor chipColor = tonegallery::GearTypeColor(entry.gearType);
    g.FillRoundRect(chipColor.WithOpacity(0.25f), chipArea, 6.0f);
    g.DrawRoundRect(chipColor, chipArea, 6.0f);
    const IText chipText(10.0f, chipColor.WithContrast(0.3f), "Inter-Regular", EAlign::Center, EVAlign::Middle);
    g.DrawText(chipText, tonegallery::GearTypeChipLabel(entry.gearType), chipArea);

    // Hover outline
    if (mouseOver)
      g.DrawRoundRect(tonegallery::AccentColor(), tile, 6.0f, nullptr, 2.0f);
  }

  std::vector<tonegallery::ToneEntry> mEntries;
  std::vector<int> mFiltered;
  std::string mLibraryRoot;
  int mFilter = tonegallery::kFilterAll;
  float mScroll = 0.0f;
  int mMouseOverTile = -1;
  std::map<std::string, IBitmap> mImageCache;
  std::function<void(const tonegallery::ToneEntry&)> mOnSelect;
};

// ===========================================================================
// Always-visible panels: favorites bar & tone sidebar
// ===========================================================================

// Three quick-access favorite slots under the main UI. Left-click loads the
// tone; right-click clears the slot. Slots are assigned by right-clicking a
// tone in the sidebar. Stored in favorites.json in the library root.
class NAMFavoritesBarControl : public IControl, public tonegallery::INowPlayingListener
{
public:
  static constexpr int kNumSlots = 3;

  NAMFavoritesBarControl(const IRECT& bounds, IFileDialogCompletionHandlerFunc loadModelFunc,
                         IFileDialogCompletionHandlerFunc loadIRFunc)
  : IControl(bounds)
  , mLoadModelFunc(loadModelFunc)
  , mLoadIRFunc(loadIRFunc)
  {
  }

  void OnAttached() override { Reload(); }

  void Reload()
  {
    mSlotNames = tonegallery::LoadFavorites();
    for (int i = 0; i < kNumSlots; i++)
    {
      mSlotEntries[i] = tonegallery::ToneEntry();
      mSlotValid[i] = false;
      if (!mSlotNames[i].empty())
      {
        try
        {
          const std::filesystem::path dir = tonegallery::GetToneLibraryRoot() / tonegallery::UTF8ToPath(mSlotNames[i]);
          if (std::filesystem::exists(dir))
            mSlotValid[i] = tonegallery::ScanToneFolder(dir, mSlotEntries[i]);
        }
        catch (const std::exception&)
        {
        }
      }
    }
    SetDirty(false);
  }

  void SetNowPlaying(const tonegallery::ToneEntry& entry, const std::string& modelPath,
                     const std::string& irPath) override
  {
    mNowPlayingDir = entry.directory;
    SetDirty(false);
  }

  void AssignSlot(int slotIdx, const std::string& folderName)
  {
    if (slotIdx < 0 || slotIdx >= kNumSlots)
      return;
    auto slots = tonegallery::LoadFavorites();
    slots[slotIdx] = folderName;
    tonegallery::SaveFavorites(slots);
    Reload();
  }

  void Draw(IGraphics& g) override
  {
    for (int i = 0; i < kNumSlots; i++)
    {
      const IRECT slot = SlotRect(i);
      const bool valid = mSlotValid[i];
      const bool over = (i == mMouseOverSlot);
      const bool active = valid && !mNowPlayingDir.empty() && mSlotEntries[i].directory == mNowPlayingDir;
      const IColor accent = valid ? tonegallery::GearTypeColor(mSlotEntries[i].gearType) : tonegallery::AccentColor();

      g.FillRoundRect(active ? accent.WithOpacity(0.15f) : IColor(255, 30, 30, 34), slot, 8.0f);
      if (over)
        g.FillRoundRect(PluginColors::MOUSEOVER, slot, 8.0f);
      if (active)
        g.DrawRoundRect(accent.WithOpacity(0.3f), slot.GetPadded(1.5f), 9.0f, nullptr, 3.0f);
      g.DrawRoundRect(valid ? accent.WithOpacity(active ? 1.0f : 0.7f) : accent.WithOpacity(0.15f), slot, 8.0f);

      // Numbered badge
      const IRECT badge = slot.GetFromLeft(slot.H()).GetCentredInside(18.0f);
      g.FillEllipse(valid ? accent : accent.WithOpacity(0.25f), badge);
      const IText badgeText(11.0f, COLOR_BLACK, "Inter-Regular", EAlign::Center, EVAlign::Middle);
      const char num[2] = {(char)('1' + i), 0};
      g.DrawText(badgeText, num, badge);

      // Label
      const IRECT labelArea = slot.GetReducedFromLeft(slot.H() * 0.85f).GetReducedFromRight(8.0f);
      if (valid)
      {
        const IText nameText(12.0f, COLOR_WHITE, "Inter-Regular", EAlign::Near, EVAlign::Middle);
        g.DrawText(nameText, tonegallery::Ellipsize(mSlotEntries[i].name, 22).c_str(), labelArea);
      }
      else
      {
        const IText emptyText(10.0f, PluginColors::HELP_TEXT, "Inter-Regular", EAlign::Near, EVAlign::Middle);
        g.DrawText(emptyText, "Empty - right-click a tone", labelArea);
      }
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    const int idx = SlotAt(x, y);
    if (idx < 0)
      return;
    if (mod.R)
    {
      if (mSlotValid[idx])
      {
        mMenuSlotIdx = idx;
        mMenu.Clear();
        mMenu.AddItem("Clear favorite");
        GetUI()->CreatePopupMenu(*this, mMenu, IRECT(x, y, x, y));
      }
      return;
    }
    if (mSlotValid[idx])
    {
      tonegallery::LoadToneEntryFiles(mSlotEntries[idx], mLoadModelFunc, mLoadIRFunc);
      tonegallery::NotifyNowPlaying(GetUI(), mSlotEntries[idx], mSlotEntries[idx].modelPath, mSlotEntries[idx].irPath);
    }
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu != nullptr && pSelectedMenu->GetChosenItem() != nullptr && mMenuSlotIdx >= 0)
      AssignSlot(mMenuSlotIdx, "");
    mMenuSlotIdx = -1;
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    const int idx = SlotAt(x, y);
    if (idx != mMouseOverSlot)
    {
      mMouseOverSlot = idx;
      if (idx >= 0 && mSlotValid[idx])
        SetTooltip(mSlotEntries[idx].name.c_str());
      else
        SetTooltip("Right-click a tone in the list to assign a favorite");
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mMouseOverSlot = -1;
    SetDirty(false);
  }

private:
  IRECT SlotRect(int i) const
  {
    const IRECT area = mRECT.GetPadded(-8.0f).GetHPadded(-12.0f);
    const float gap = 10.0f;
    const float w = (area.W() - gap * (kNumSlots - 1)) / kNumSlots;
    const float x = area.L + i * (w + gap);
    return IRECT(x, area.T, x + w, area.B);
  }

  int SlotAt(float x, float y) const
  {
    for (int i = 0; i < kNumSlots; i++)
      if (SlotRect(i).Contains(x, y))
        return i;
    return -1;
  }

  std::vector<std::string> mSlotNames = std::vector<std::string>(kNumSlots, "");
  std::string mNowPlayingDir;
  tonegallery::ToneEntry mSlotEntries[kNumSlots];
  bool mSlotValid[kNumSlots] = {false, false, false};
  int mMouseOverSlot = -1;
  int mMenuSlotIdx = -1;
  IPopupMenu mMenu;
  IFileDialogCompletionHandlerFunc mLoadModelFunc;
  IFileDialogCompletionHandlerFunc mLoadIRFunc;
};

// Slide-in tone detail panel: opens to the right of the sidebar when a tone
// card is clicked. Shows the full photo, name, author, description, tags and
// every model/IR variant in the tone folder. Clicking a variant loads it.
class NAMToneDetailControl : public IControl, public tonegallery::INowPlayingListener
{
public:
  static constexpr float kPhotoHeight = 130.0f;
  static constexpr float kRowHeight = 24.0f;

  NAMToneDetailControl(const IRECT& bounds, IFileDialogCompletionHandlerFunc loadModelFunc,
                       IFileDialogCompletionHandlerFunc loadIRFunc)
  : IControl(bounds)
  , mLoadModelFunc(loadModelFunc)
  , mLoadIRFunc(loadIRFunc)
  {
  }

  void ShowTone(const tonegallery::ToneEntry& entry)
  {
    mEntry = entry;
    mHasEntry = true;
    mScroll = 0.0f;
    Hide(false);
    SetDirty(false);
  }

  void SetNowPlaying(const tonegallery::ToneEntry& entry, const std::string& modelPath,
                     const std::string& irPath) override
  {
    if (!modelPath.empty())
      mNowPlayingModel = modelPath;
    if (!irPath.empty())
      mNowPlayingIR = irPath;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    const IColor accent = tonegallery::AccentColor();
    g.FillRect(IColor(255, 20, 21, 25), mRECT);
    g.FillRect(accent.WithOpacity(0.3f), mRECT.GetFromRight(1.0f));
    if (!mHasEntry)
      return;

    // Photo (cover crop)
    const IRECT photo = mRECT.GetFromTop(kPhotoHeight);
    IBitmap* pBitmap = GetImage(mEntry.imagePath);
    if (pBitmap != nullptr && pBitmap->W() > 0 && pBitmap->H() > 0)
    {
      const float bmpAspect = (float)pBitmap->W() / (float)pBitmap->H();
      const float areaAspect = photo.W() / photo.H();
      IRECT cover = photo;
      if (bmpAspect > areaAspect)
        cover = photo.GetMidHPadded(0.5f * photo.H() * bmpAspect);
      else
        cover = photo.GetMidVPadded(0.5f * photo.W() / bmpAspect);
      g.PathClipRegion(photo);
      g.DrawFittedBitmap(*pBitmap, cover);
      g.PathClipRegion();
      // Dark gradient over the top of the photo (darkest at the very top)
      g.PathRect(photo.GetFromTop(64.0f));
      g.PathFill(IPattern::CreateLinearGradient(photo.L, photo.T, photo.L, photo.T + 64.0f,
                                                {{COLOR_BLACK.WithOpacity(0.6f), 0.0f}, {COLOR_TRANSPARENT, 1.0f}}));
      // Fade the bottom of the photo into the panel
      g.PathRect(photo.GetFromBottom(40.0f));
      g.PathFill(IPattern::CreateLinearGradient(
        photo.L, photo.B - 40.0f, photo.L, photo.B, {{IColor(0, 20, 21, 25), 0.0f}, {IColor(255, 20, 21, 25), 1.0f}}));
    }
    else
    {
      const IColor gearColor = tonegallery::GearTypeColor(mEntry.gearType);
      g.FillRect(gearColor.WithOpacity(0.12f), photo);
      const IText bigText(28.0f, gearColor, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      std::string initials;
      initials += mEntry.name.empty() ? '?' : (char)std::toupper((unsigned char)mEntry.name[0]);
      g.DrawText(bigText, initials.c_str(), photo);
    }

    // Close button
    const IRECT close = CloseRect();
    g.FillEllipse(IColor(180, 20, 21, 25), close);
    const IColor xColor = mMouseOverClose ? COLOR_WHITE : IColor(255, 170, 173, 182);
    const IRECT xr = close.GetCentredInside(8.0f);
    g.DrawLine(xColor, xr.L, xr.T, xr.R, xr.B, nullptr, 1.6f);
    g.DrawLine(xColor, xr.L, xr.B, xr.R, xr.T, nullptr, 1.6f);

    // Name / author / chips
    const IRECT body = mRECT.GetReducedFromTop(kPhotoHeight - 14.0f).GetHPadded(-10.0f);
    float y = body.T;
    const IText nameText(13.0f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Top);
    for (const auto& line : tonegallery::WrapLines(mEntry.name, 26, 2))
    {
      g.DrawText(nameText, line.c_str(), IRECT(body.L, y, body.R, y + 15.0f));
      y += 15.0f;
    }
    if (!mEntry.author.empty())
    {
      const IText authorText(9.5f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Near, EVAlign::Top);
      g.DrawText(authorText, ("by " + mEntry.author).c_str(), IRECT(body.L, y + 1.0f, body.R, y + 12.0f));
      y += 14.0f;
    }
    // Gear chip + tags
    {
      float x = body.L;
      const IColor gearColor = tonegallery::GearTypeColor(mEntry.gearType);
      const char* gearLabel = tonegallery::GearTypeChipLabel(mEntry.gearType);
      const float gw = 12.0f + 5.0f * (float)strlen(gearLabel);
      const IRECT gearChip(x, y + 2.0f, x + gw, y + 15.0f);
      g.FillRoundRect(gearColor, gearChip, 4.0f);
      const IText gearText(7.5f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(gearText, gearLabel, gearChip);
      x += gw + 4.0f;
      const IText tagText(7.5f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Center, EVAlign::Middle);
      for (const auto& tag : mEntry.tags)
      {
        const std::string t = tonegallery::Ellipsize(tag, 14);
        const float tw = 12.0f + 4.6f * (float)t.length();
        if (x + tw > body.R)
          break;
        const IRECT chip(x, y + 2.0f, x + tw, y + 15.0f);
        g.FillRoundRect(IColor(18, 255, 255, 255), chip, 4.0f);
        g.DrawText(tagText, t.c_str(), chip);
        x += tw + 4.0f;
      }
      y += 21.0f;
    }

    // LOAD button
    mLoadButtonRect = IRECT(body.L, y, body.R, y + 24.0f);
    const bool overLoad = mMouseOverLoad;
    g.FillRoundRect(overLoad ? accent : accent.WithOpacity(0.85f), mLoadButtonRect, 12.0f);
    const IText loadText(11.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    g.DrawText(loadText, "LOAD TONE", mLoadButtonRect);
    y += 32.0f;

    // Description
    if (!mEntry.description.empty())
    {
      const IText descText(9.0f, IColor(255, 150, 153, 162), "Inter-Regular", EAlign::Near, EVAlign::Top);
      for (const auto& line : tonegallery::WrapLines(mEntry.description, 40, 6))
      {
        g.DrawText(descText, line.c_str(), IRECT(body.L, y, body.R, y + 11.0f));
        y += 11.5f;
      }
      y += 6.0f;
    }

    // Variants
    const int numVariants = (int)(mEntry.models.size() + mEntry.irs.size());
    std::stringstream header;
    header << "VARIANTS (" << numVariants << ")";
    const IText headerText(9.0f, IColor(255, 139, 142, 152), "Inter-Bold", EAlign::Near, EVAlign::Top);
    g.DrawText(headerText, header.str().c_str(), IRECT(body.L, y, body.R, y + 12.0f));
    y += 15.0f;

    mListTop = y;
    const IRECT list(body.L, y, body.R, mRECT.B - 8.0f);
    g.PathClipRegion(list);
    for (int i = 0; i < numVariants; i++)
    {
      const IRECT row = RowRect(i);
      if (row.B < list.T || row.T > list.B)
        continue;
      const bool isIR = i >= (int)mEntry.models.size();
      const std::string& path = isIR ? mEntry.irs[i - mEntry.models.size()] : mEntry.models[i];
      const bool nowPlaying = (!isIR && path == mNowPlayingModel) || (isIR && path == mNowPlayingIR);
      if (i == mMouseOverRow)
        g.FillRoundRect(accent.WithOpacity(0.12f), row, 5.0f);
      // Type chip
      const IColor typeColor = isIR ? IColor(255, 110, 168, 255) : accent;
      const IRECT typeChip = row.GetFromLeft(24.0f).GetCentredInside(20.0f, 13.0f);
      g.FillRoundRect(typeColor.WithOpacity(0.2f), typeChip, 3.0f);
      const IText typeText(7.0f, typeColor, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(typeText, isIR ? "IR" : "M", typeChip);
      // Name (filename without extension)
      const std::string stem = tonegallery::PathToUTF8(tonegallery::UTF8ToPath(path).stem());
      const IText rowText(
        9.5f, nowPlaying ? COLOR_WHITE : IColor(255, 190, 193, 202), "Inter-Regular", EAlign::Near, EVAlign::Middle);
      g.DrawText(
        rowText, tonegallery::Ellipsize(stem, 30).c_str(), row.GetReducedFromLeft(28.0f).GetReducedFromRight(12.0f));
      // Now-playing LED
      if (nowPlaying)
      {
        const IRECT led = row.GetFromRight(14.0f).GetCentredInside(6.0f);
        g.FillEllipse(accent, led);
      }
    }
    // Scrollbar for variants
    const float contentH = numVariants * kRowHeight;
    if (contentH > list.H())
    {
      const float frac = list.H() / contentH;
      const float barH = std::max(16.0f, frac * list.H());
      const float travel = list.H() - barH;
      const float pos = (mScroll / (contentH - list.H())) * travel;
      g.FillRoundRect(
        accent.WithOpacity(0.5f), IRECT(mRECT.R - 4.0f, list.T + pos, mRECT.R - 2.0f, list.T + pos + barH), 1.0f);
    }
    g.PathClipRegion();
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (!mHasEntry)
      return;
    if (CloseRect().Contains(x, y))
    {
      Hide(true);
      return;
    }
    if (mLoadButtonRect.Contains(x, y))
    {
      tonegallery::LoadToneEntryFiles(mEntry, mLoadModelFunc, mLoadIRFunc);
      tonegallery::NotifyNowPlaying(GetUI(), mEntry, mEntry.modelPath, mEntry.irPath);
      return;
    }
    const int idx = RowAt(x, y);
    if (idx >= 0)
    {
      const bool isIR = idx >= (int)mEntry.models.size();
      const std::string& path = isIR ? mEntry.irs[idx - mEntry.models.size()] : mEntry.models[idx];
      WDL_String fileName(path.c_str());
      WDL_String dir(mEntry.directory.c_str());
      if (isIR)
      {
        if (mLoadIRFunc)
          mLoadIRFunc(fileName, dir);
        tonegallery::NotifyNowPlaying(GetUI(), mEntry, "", path);
      }
      else
      {
        if (mLoadModelFunc)
          mLoadModelFunc(fileName, dir);
        tonegallery::NotifyNowPlaying(GetUI(), mEntry, path, "");
      }
    }
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    const int numVariants = (int)(mEntry.models.size() + mEntry.irs.size());
    const float listH = mRECT.B - 8.0f - mListTop;
    const float maxScroll = std::max(0.0f, numVariants * kRowHeight - listH);
    const float next = std::min(maxScroll, std::max(0.0f, mScroll - d * 30.0f));
    if (next != mScroll)
    {
      mScroll = next;
      SetDirty(false);
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    const bool overClose = CloseRect().Contains(x, y);
    const bool overLoad = mLoadButtonRect.Contains(x, y);
    const int row = RowAt(x, y);
    if (overClose != mMouseOverClose || overLoad != mMouseOverLoad || row != mMouseOverRow)
    {
      mMouseOverClose = overClose;
      mMouseOverLoad = overLoad;
      mMouseOverRow = row;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mMouseOverClose = false;
    mMouseOverLoad = false;
    mMouseOverRow = -1;
    SetDirty(false);
  }

private:
  IRECT CloseRect() const { return mRECT.GetFromTRHC(30.0f, 30.0f).GetCentredInside(20.0f); }

  IRECT RowRect(int i) const
  {
    const float y = mListTop + i * kRowHeight - mScroll;
    return IRECT(mRECT.L + 10.0f, y, mRECT.R - 10.0f, y + kRowHeight);
  }

  int RowAt(float x, float y) const
  {
    if (y < mListTop || y > mRECT.B - 8.0f)
      return -1;
    const int numVariants = (int)(mEntry.models.size() + mEntry.irs.size());
    const int idx = (int)((y - mListTop + mScroll) / kRowHeight);
    return (idx >= 0 && idx < numVariants) ? idx : -1;
  }

  IBitmap* GetImage(const std::string& path)
  {
    if (path.empty())
      return nullptr;
    auto found = mImageCache.find(path);
    if (found != mImageCache.end())
      return found->second.GetAPIBitmap() ? &found->second : nullptr;
    IBitmap bitmap;
    try
    {
      if (std::filesystem::exists(tonegallery::UTF8ToPath(path)))
        bitmap = GetUI()->LoadBitmap(path.c_str());
    }
    catch (const std::exception&)
    {
    }
    auto inserted = mImageCache.insert({path, bitmap});
    return inserted.first->second.GetAPIBitmap() ? &inserted.first->second : nullptr;
  }

  tonegallery::ToneEntry mEntry;
  bool mHasEntry = false;
  std::string mNowPlayingModel;
  std::string mNowPlayingIR;
  float mScroll = 0.0f;
  float mListTop = 0.0f;
  IRECT mLoadButtonRect;
  bool mMouseOverClose = false;
  bool mMouseOverLoad = false;
  int mMouseOverRow = -1;
  std::map<std::string, IBitmap> mImageCache;
  IFileDialogCompletionHandlerFunc mLoadModelFunc;
  IFileDialogCompletionHandlerFunc mLoadIRFunc;
};

// The always-visible tone list on the left of the main UI. Click to load,
// right-click to add to a favorite slot, mouse wheel to scroll. The circular
// arrow at the top rescans the library folder.
// The always-visible tone browser on the left: category chips up top, then a
// two-column grid of tone cards (photo, name, description, tags). Click to
// load, right-click to assign a favorite slot, mouse wheel to scroll.
class NAMToneSidebarControl : public IControl, public tonegallery::INowPlayingListener
{
public:
  static constexpr float kHeaderHeight = 46.0f;
  static constexpr float kFilterAreaHeight = 50.0f;
  static constexpr float kChipHeight = 18.0f;
  static constexpr int kNumCols = 2;
  static constexpr float kCardGap = 6.0f;
  static constexpr float kCardHeight = 124.0f;
  static constexpr float kPhotoHeight = 54.0f;

  NAMToneSidebarControl(const IRECT& bounds, IFileDialogCompletionHandlerFunc loadModelFunc,
                        IFileDialogCompletionHandlerFunc loadIRFunc)
  : IControl(bounds)
  , mLoadModelFunc(loadModelFunc)
  , mLoadIRFunc(loadIRFunc)
  {
  }

  void OnAttached() override { Refresh(); }

  void Refresh()
  {
    mEntries = tonegallery::ScanToneLibrary(tonegallery::GetToneLibraryRoot());
    ApplyFilter();
  }

  void ApplyFilter()
  {
    mFiltered.clear();
    for (int i = 0; i < (int)mEntries.size(); i++)
      if (tonegallery::MatchesFilter(mEntries[i].gearType, mFilter))
        mFiltered.push_back(i);
    if (mScroll > MaxScroll())
      mScroll = MaxScroll();
    mMouseOverCard = -1;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    // Panel
    g.FillRect(IColor(255, 23, 24, 28), mRECT);
    g.FillRect(IColor(18, 255, 255, 255), mRECT.GetFromRight(1.0f));

    // Header
    const IRECT header = mRECT.GetFromTop(kHeaderHeight);
    const IText titleText(11.0f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(titleText, "TONE LIBRARY", header.GetReducedFromLeft(12.0f).GetFromTop(28.0f));
    std::stringstream count;
    if (mFilter == tonegallery::kFilterAll)
      count << mEntries.size() << (mEntries.size() == 1 ? " tone" : " tones");
    else
      count << mFiltered.size() << " of " << mEntries.size() << (mEntries.size() == 1 ? " tone" : " tones");
    const IText countText(9.0f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Near, EVAlign::Middle);
    g.DrawText(countText, count.str().c_str(), header.GetReducedFromLeft(12.0f).GetFromBottom(16.0f));

    // Refresh icon (circular arrow)
    const IRECT refresh = RefreshRect();
    const IColor iconColor =
      mMouseOverRefresh ? tonegallery::AccentColor() : tonegallery::AccentColor().WithOpacity(0.6f);
    const float cx = refresh.MW(), cy = refresh.MH(), r = 6.0f;
    g.DrawArc(iconColor, cx, cy, r, 30.0f, 330.0f, nullptr, 1.6f);
    g.FillTriangle(iconColor, cx + 1.5f, cy - r - 3.0f, cx + 7.5f, cy - r + 0.5f, cx + 1.5f, cy - r + 4.0f);

    // Category chips
    const IText chipTextActive(9.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    const IText chipTextInactive(9.0f, PluginColors::NAM_THEMEFONTCOLOR, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    for (int i = 0; i < tonegallery::kNumFilters; i++)
    {
      const IRECT chip = ChipRect(i);
      const bool isActive = (i == mFilter);
      const bool isOver = (i == mMouseOverChip);
      g.FillRoundRect(isActive ? tonegallery::AccentColor() : IColor(13, 255, 255, 255), chip, chip.H() * 0.5f);
      if (isOver && !isActive)
        g.FillRoundRect(PluginColors::MOUSEOVER, chip, chip.H() * 0.5f);
      g.DrawText(isActive ? chipTextActive : chipTextInactive, tonegallery::FilterLabel(i), chip);
    }
    g.FillRect(IColor(18, 255, 255, 255), mRECT.GetFromTop(kHeaderHeight + kFilterAreaHeight).GetFromBottom(1.0f));

    // Cards
    const IRECT grid = GridArea();
    g.PathClipRegion(grid);
    if (mFiltered.empty())
    {
      const IText msgText(10.0f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Center, EVAlign::Middle);
      if (mEntries.empty())
      {
        g.DrawText(msgText, "No tones yet.", grid.GetFromTop(60.0f).GetVShifted(10.0f));
        g.DrawText(msgText, "Use the Add-Tone helper,", grid.GetFromTop(60.0f).GetVShifted(26.0f));
        g.DrawText(msgText, "then click the arrow above.", grid.GetFromTop(60.0f).GetVShifted(42.0f));
      }
      else
      {
        g.DrawText(msgText, "No tones in this category.", grid.GetFromTop(40.0f).GetVShifted(10.0f));
      }
    }
    for (int i = 0; i < (int)mFiltered.size(); i++)
    {
      const IRECT card = CardRect(i);
      if (card.B < grid.T || card.T > grid.B)
        continue;
      DrawCard(g, mEntries[mFiltered[i]], card, i == mMouseOverCard, grid);
    }
    // Scrollbar
    const float contentH = ContentHeight();
    if (contentH > grid.H())
    {
      const float frac = grid.H() / contentH;
      const float barH = std::max(20.0f, frac * grid.H());
      const float travel = grid.H() - barH;
      const float pos = (mScroll / (contentH - grid.H())) * travel;
      const IRECT bar(mRECT.R - 4.0f, grid.T + pos, mRECT.R - 2.0f, grid.T + pos + barH);
      g.FillRoundRect(tonegallery::AccentColor().WithOpacity(0.5f), bar, 1.0f);
    }
    g.PathClipRegion();
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (RefreshRect().Contains(x, y))
    {
      Refresh();
      return;
    }
    for (int i = 0; i < tonegallery::kNumFilters; i++)
    {
      if (ChipRect(i).Contains(x, y))
      {
        if (mFilter != i)
        {
          mFilter = i;
          mScroll = 0.0f;
          ApplyFilter();
        }
        return;
      }
    }
    const int idx = CardAt(x, y);
    if (idx < 0)
      return;
    if (mod.R)
    {
      mMenuEntryIdx = mFiltered[idx];
      mMenu.Clear();
      mMenu.AddItem("Add to Favorite 1");
      mMenu.AddItem("Add to Favorite 2");
      mMenu.AddItem("Add to Favorite 3");
      GetUI()->CreatePopupMenu(*this, mMenu, IRECT(x, y, x, y));
      return;
    }
    // Left-click opens the tone detail panel (with the variant list).
    if (IControl* pDetail = GetUI()->GetControlWithTag(kCtrlTagToneDetail))
      pDetail->As<NAMToneDetailControl>()->ShowTone(mEntries[mFiltered[idx]]);
  }

  void SetNowPlaying(const tonegallery::ToneEntry& entry, const std::string& modelPath,
                     const std::string& irPath) override
  {
    mNowPlayingDir = entry.directory;
    SetDirty(false);
  }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu == nullptr || pSelectedMenu->GetChosenItem() == nullptr)
      return;
    const int slotIdx = pSelectedMenu->GetChosenItemIdx();
    if (mMenuEntryIdx >= 0 && mMenuEntryIdx < (int)mEntries.size() && slotIdx >= 0)
    {
      const std::string folderName =
        tonegallery::PathToUTF8(tonegallery::UTF8ToPath(mEntries[mMenuEntryIdx].directory).filename());
      if (auto* pFav = GetUI()->GetControlWithTag(kCtrlTagFavoritesBar))
        pFav->As<NAMFavoritesBarControl>()->AssignSlot(slotIdx, folderName);
    }
    mMenuEntryIdx = -1;
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    const float maxScroll = MaxScroll();
    const float next = std::min(maxScroll, std::max(0.0f, mScroll - d * 44.0f));
    if (next != mScroll)
    {
      mScroll = next;
      SetDirty(false);
    }
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    const bool overRefresh = RefreshRect().Contains(x, y);
    int overChip = -1;
    for (int i = 0; i < tonegallery::kNumFilters; i++)
      if (ChipRect(i).Contains(x, y))
        overChip = i;
    const int idx = CardAt(x, y);
    if (idx != mMouseOverCard || overRefresh != mMouseOverRefresh || overChip != mMouseOverChip)
    {
      mMouseOverCard = idx;
      mMouseOverRefresh = overRefresh;
      mMouseOverChip = overChip;
      if (overRefresh)
        SetTooltip("Rescan the tone folder");
      else if (idx >= 0)
      {
        const tonegallery::ToneEntry& e = mEntries[mFiltered[idx]];
        SetTooltip(e.description.empty() ? e.name.c_str() : e.description.c_str());
      }
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mMouseOverCard = -1;
    mMouseOverRefresh = false;
    mMouseOverChip = -1;
    SetDirty(false);
  }

private:
  IRECT GridArea() const
  {
    return mRECT.GetReducedFromTop(kHeaderHeight + kFilterAreaHeight).GetPadded(-8.0f).GetReducedFromRight(2.0f);
  }
  IRECT RefreshRect() const { return mRECT.GetFromTop(kHeaderHeight).GetFromRight(30.0f).GetCentredInside(18.0f); }

  float ContentHeight() const
  {
    const int numRows = ((int)mFiltered.size() + kNumCols - 1) / kNumCols;
    return numRows * (kCardHeight + kCardGap);
  }

  float MaxScroll() const { return std::max(0.0f, ContentHeight() - GridArea().H()); }

  // Pill-shaped category chips, flowing over two rows under the header.
  IRECT ChipRect(int filter) const
  {
    const IRECT area = mRECT.GetReducedFromTop(kHeaderHeight).GetFromTop(kFilterAreaHeight).GetPadded(-6.0f);
    const float gap = 5.0f;
    float x = area.L, y = area.T;
    for (int i = 0; i < tonegallery::kNumFilters; i++)
    {
      const float w = 14.0f + 5.4f * (float)strlen(tonegallery::FilterLabel(i));
      if (x + w > area.R)
      {
        x = area.L;
        y += kChipHeight + gap;
      }
      if (i == filter)
        return IRECT(x, y, x + w, y + kChipHeight);
      x += w + gap;
    }
    return IRECT();
  }

  IRECT CardRect(int filteredIdx) const
  {
    const IRECT grid = GridArea();
    const int col = filteredIdx % kNumCols;
    const int row = filteredIdx / kNumCols;
    const float w = (grid.W() - kCardGap) / (float)kNumCols;
    const float x = grid.L + col * (w + kCardGap);
    const float y = grid.T + row * (kCardHeight + kCardGap) - mScroll;
    return IRECT(x, y, x + w, y + kCardHeight);
  }

  int CardAt(float x, float y) const
  {
    if (!GridArea().Contains(x, y))
      return -1;
    for (int i = 0; i < (int)mFiltered.size(); i++)
      if (CardRect(i).Contains(x, y))
        return i;
    return -1;
  }

  IBitmap* GetImage(const std::string& path)
  {
    if (path.empty())
      return nullptr;
    auto found = mImageCache.find(path);
    if (found != mImageCache.end())
      return found->second.GetAPIBitmap() ? &found->second : nullptr;
    IBitmap bitmap;
    try
    {
      if (std::filesystem::exists(tonegallery::UTF8ToPath(path)))
        bitmap = GetUI()->LoadBitmap(path.c_str());
    }
    catch (const std::exception&)
    {
    }
    auto inserted = mImageCache.insert({path, bitmap});
    return inserted.first->second.GetAPIBitmap() ? &inserted.first->second : nullptr;
  }

  static void TwoLines(const std::string& s, size_t maxChars, std::string& line1, std::string& line2)
  {
    if (s.length() <= maxChars)
    {
      line1 = s;
      line2 = "";
      return;
    }
    // Prefer breaking at a space near the limit.
    size_t breakAt = s.rfind(' ', maxChars);
    if (breakAt == std::string::npos || breakAt < maxChars / 2)
      breakAt = maxChars;
    line1 = s.substr(0, breakAt);
    line2 = tonegallery::Ellipsize(s.substr(breakAt == maxChars ? breakAt : breakAt + 1), maxChars);
  }

  void DrawCard(IGraphics& g, const tonegallery::ToneEntry& entry, const IRECT& card, bool mouseOver, const IRECT& grid)
  {
    const IColor gearColor = tonegallery::GearTypeColor(entry.gearType);

    g.FillRoundRect(IColor(255, 32, 33, 41), card, 8.0f);

    // Photo (cover-cropped into the top of the card)
    const IRECT photo = card.GetFromTop(kPhotoHeight).GetPadded(-1.5f);
    IBitmap* pBitmap = GetImage(entry.imagePath);
    // NOTE: photo.Intersect(grid) is EMPTY when the photo is scrolled fully
    // out of view, and an empty clip rect means "no clipping" -- which would
    // splatter the full-size cover image over the whole UI. Only draw when
    // the photo is actually (partly) visible.
    const IRECT photoVis = photo.Intersect(grid);
    if (pBitmap != nullptr && pBitmap->W() > 0 && pBitmap->H() > 0 && photoVis.W() > 0.5f && photoVis.H() > 0.5f)
    {
      const float bmpAspect = (float)pBitmap->W() / (float)pBitmap->H();
      const float areaAspect = photo.W() / photo.H();
      IRECT cover = photo;
      if (bmpAspect > areaAspect)
        cover = photo.GetMidHPadded(0.5f * photo.H() * bmpAspect); // wider: overflow left/right
      else
        cover = photo.GetMidVPadded(0.5f * photo.W() / bmpAspect); // taller: overflow top/bottom
      g.PathClipRegion(photoVis);
      g.DrawFittedBitmap(*pBitmap, cover);
      g.PathClipRegion(grid);
    }
    else
    {
      g.FillRoundRect(gearColor.WithOpacity(0.15f), photo, 6.0f);
      const IText initialText(15.0f, gearColor, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      std::string initials;
      initials += entry.name.empty() ? '?' : (char)std::toupper((unsigned char)entry.name[0]);
      g.DrawText(initialText, initials.c_str(), photo);
    }

    // Name (up to two lines)
    const IRECT body = card.GetReducedFromTop(kPhotoHeight).GetPadded(-6.0f);
    const IText nameText(8.5f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Top);
    std::string n1, n2;
    TwoLines(entry.name, 18, n1, n2);
    g.DrawText(nameText, n1.c_str(), body.GetFromTop(10.0f));
    if (!n2.empty())
      g.DrawText(nameText, n2.c_str(), body.GetReducedFromTop(10.0f).GetFromTop(10.0f));

    // Description (up to two lines)
    const IText descText(7.5f, IColor(255, 120, 123, 132), "Inter-Regular", EAlign::Near, EVAlign::Top);
    std::string d1, d2;
    TwoLines(entry.description, 21, d1, d2);
    const IRECT descArea = body.GetReducedFromTop(21.0f);
    g.DrawText(descText, d1.c_str(), descArea.GetFromTop(9.0f));
    if (!d2.empty())
      g.DrawText(descText, d2.c_str(), descArea.GetReducedFromTop(9.0f).GetFromTop(9.0f));

    // Gear chip + first tag
    const IRECT tagRow = body.GetFromBottom(11.0f);
    const char* gearLabel = tonegallery::GearTypeChipLabel(entry.gearType);
    const float gearW = 10.0f + 4.6f * (float)strlen(gearLabel);
    const IRECT gearChip = tagRow.GetFromLeft(gearW);
    g.FillRoundRect(gearColor, gearChip, 4.0f);
    const IText gearText(6.5f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    g.DrawText(gearText, gearLabel, gearChip);
    if (!entry.tags.empty())
    {
      const std::string tag = tonegallery::Ellipsize(entry.tags.front(), 12);
      const float tagW = 10.0f + 4.2f * (float)tag.length();
      IRECT tagChip = tagRow.GetReducedFromLeft(gearW + 4.0f).GetFromLeft(tagW);
      if (tagChip.R <= card.R - 4.0f)
      {
        g.FillRoundRect(IColor(18, 255, 255, 255), tagChip, 4.0f);
        const IText tagText(6.5f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Center, EVAlign::Middle);
        g.DrawText(tagText, tag.c_str(), tagChip);
      }
    }

    // Border / hover glow / now-playing glow
    const bool nowPlaying = !mNowPlayingDir.empty() && entry.directory == mNowPlayingDir;
    if (mouseOver || nowPlaying)
    {
      g.DrawRoundRect(tonegallery::AccentColor().WithOpacity(0.25f), card.GetPadded(1.0f), 9.0f, nullptr, 3.0f);
      g.DrawRoundRect(tonegallery::AccentColor(), card, 8.0f, nullptr, nowPlaying ? 1.6f : 1.2f);
      if (nowPlaying)
      {
        const IRECT led = card.GetFromTop(kPhotoHeight).GetFromTRHC(16.0f, 16.0f).GetCentredInside(7.0f);
        g.FillEllipse(tonegallery::AccentColor(), led);
        g.DrawEllipse(COLOR_BLACK.WithOpacity(0.4f), led);
      }
    }
    else
    {
      g.DrawRoundRect(IColor(18, 255, 255, 255), card, 8.0f);
    }
  }

  std::vector<tonegallery::ToneEntry> mEntries;
  std::vector<int> mFiltered;
  std::string mNowPlayingDir;
  int mFilter = tonegallery::kFilterAll;
  float mScroll = 0.0f;
  int mMouseOverCard = -1;
  bool mMouseOverRefresh = false;
  int mMouseOverChip = -1;
  int mMenuEntryIdx = -1;
  IPopupMenu mMenu;
  std::map<std::string, IBitmap> mImageCache;
  IFileDialogCompletionHandlerFunc mLoadModelFunc;
  IFileDialogCompletionHandlerFunc mLoadIRFunc;
};

// The full-window overlay page, modeled on NAMSettingsPageControl.
class NAMToneGalleryPageControl : public IContainerBaseWithNamedChildren
{
public:
  NAMToneGalleryPageControl(const IRECT& bounds, const IBitmap& bitmap, ISVG closeSVG, const IVStyle& style,
                            IFileDialogCompletionHandlerFunc loadModelFunc, IFileDialogCompletionHandlerFunc loadIRFunc)
  : IContainerBaseWithNamedChildren(bounds)
  , mBitmap(bitmap)
  , mCloseSVG(closeSVG)
  , mStyle(style)
  , mLoadModelFunc(loadModelFunc)
  , mLoadIRFunc(loadIRFunc)
  {
    mIgnoreMouse = false;
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      HideAnimated(true);
      return true;
    }
    return false;
  }

  void ShowAnimated()
  {
    Refresh();
    HideAnimated(false);
  }

  void HideAnimated(bool hide)
  {
    mWillHide = hide;
    if (hide == false)
    {
      mHide = false;
    }
    else // hide subcontrols immediately
    {
      ForAllChildrenFunc([hide](int childIdx, IControl* pChild) { pChild->Hide(hide); });
    }

    SetAnimation(
      [&](IControl* pCaller) {
        auto progress = static_cast<float>(pCaller->GetAnimationProgress());

        if (mWillHide)
          SetBlend(IBlend(EBlend::Default, 1.0f - progress));
        else
          SetBlend(IBlend(EBlend::Default, progress));

        if (progress > 1.0f)
        {
          pCaller->OnEndAnimation();
          IContainerBase::Hide(mWillHide);
          GetUI()->SetAllControlsDirty();
          return;
        }
      },
      mAnimationTime);

    SetDirty(true);
  }

  void Refresh()
  {
    const std::filesystem::path root = tonegallery::GetToneLibraryRoot();
    auto entries = tonegallery::ScanToneLibrary(root);
    if (auto* pGrid = GetGrid())
      pGrid->SetEntries(std::move(entries), tonegallery::PathToUTF8(root));
    // Keep the always-visible panels in sync too.
    if (auto* pSidebar = GetUI()->GetControlWithTag(kCtrlTagToneSidebar))
      pSidebar->As<NAMToneSidebarControl>()->Refresh();
    if (auto* pFav = GetUI()->GetControlWithTag(kCtrlTagFavoritesBar))
      pFav->As<NAMFavoritesBarControl>()->Reload();
  }

  void OnAttached() override
  {
    const float pad = 20.0f;
    const IVStyle titleStyle =
      DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Inter-Bold")).WithDrawFrame(false).WithShadowOffset(2.f);

    AddNamedChildControl(new IPanelControl(GetRECT(), IColor(255, 14, 14, 17)), mControlNames.bitmap)
      ->SetIgnoreMouse(true);

    const auto titleArea = GetRECT().GetPadded(-(pad + 10.0f)).GetFromTop(50.0f);
    AddNamedChildControl(new IVLabelControl(titleArea, "TONE GALLERY", titleStyle), mControlNames.title);

    // Filter tabs
    const auto contentArea = GetRECT().GetPadded(-(pad + 10.0f));
    const auto filterArea = contentArea.GetReducedFromTop(52.0f).GetFromTop(26.0f).GetMidHPadded(250.0f);
    auto onFilterChanged = [&](int filter) {
      if (auto* pGrid = GetGrid())
        pGrid->SetFilter(filter);
    };
    AddNamedChildControl(new NAMGalleryFilterControl(filterArea, onFilterChanged), mControlNames.filters);

    // The grid
    const auto gridArea = contentArea.GetReducedFromTop(88.0f).GetReducedFromBottom(6.0f);
    auto onSelect = [&](const tonegallery::ToneEntry& entry) { LoadTone(entry); };
    AddNamedChildControl(new NAMToneGridControl(gridArea, onSelect), mControlNames.grid);

    // Open-library-folder button (bottom-left corner)
    const auto folderButtonArea = GetRECT().GetPadded(-pad).GetFromBLHC(120.0f, 20.0f);
    const IText linkText(13.0f, PluginColors::HELP_TEXT, "Inter-Regular", EAlign::Near, EVAlign::Middle);
    auto openFolderAction = [](IControl* pCaller) {
      const std::string root = tonegallery::PathToUTF8(tonegallery::GetToneLibraryRoot());
      if (!root.empty())
        pCaller->GetUI()->OpenURL(root.c_str());
    };
    auto* pFolderButton = new IVButtonControl(folderButtonArea, DefaultClickActionFunc, "Open tone folder",
                                              mStyle.WithDrawFrame(false).WithValueText(linkText));
    pFolderButton->SetAnimationEndActionFunction(openFolderAction);
    AddNamedChildControl(pFolderButton, mControlNames.folder);

    // Close button
    auto closeAction = [&](IControl* pCaller) { HideAnimated(true); };
    AddNamedChildControl(
      new NAMSquareButtonControl(CornerButtonArea(GetRECT()), closeAction, mCloseSVG), mControlNames.close);

    OnResize();
  }

private:
  NAMToneGridControl* GetGrid() { return static_cast<NAMToneGridControl*>(GetNamedChild(mControlNames.grid)); }

  void LoadTone(const tonegallery::ToneEntry& entry)
  {
    tonegallery::LoadToneEntryFiles(entry, mLoadModelFunc, mLoadIRFunc);
    tonegallery::NotifyNowPlaying(GetUI(), entry, entry.modelPath, entry.irPath);
    HideAnimated(true);
  }

  IBitmap mBitmap;
  ISVG mCloseSVG;
  IVStyle mStyle;
  IFileDialogCompletionHandlerFunc mLoadModelFunc;
  IFileDialogCompletionHandlerFunc mLoadIRFunc;
  int mAnimationTime = 200;
  bool mWillHide = false;

  struct ControlNames
  {
    const std::string bitmap = "Bitmap";
    const std::string close = "Close";
    const std::string filters = "Filters";
    const std::string folder = "Folder";
    const std::string grid = "Grid";
    const std::string title = "Title";
  } mControlNames;
};
