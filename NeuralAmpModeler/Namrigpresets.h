#pragma once

// Tone Gallery fork: rig presets.
//
// A rig preset is a .namrig JSON file capturing the ENTIRE 4-unit signal
// chain (see NeuralAmpModeler::CaptureRigPreset / ApplyRigPreset). Presets
// live under <library>/.presets/ and folders there ARE the categories --
// nest them as deep as you like; the preset bar's dropdown mirrors the tree.
//
// This header holds the filesystem helpers + the share/export bundler. The
// preset bar UI itself lives in NAMChainView.h (top of the chain view).
//
// Include from NeuralAmpModeler.cpp AFTER NAMToneGalleryControl.h and
// BEFORE NAMChainView.h.

namespace namrig
{

inline std::filesystem::path PresetsRoot()
{
return tonegallery::GetToneLibraryRoot() / ".presets";
}

inline void EnsurePresetsRoot()
{
try
{
std::filesystem::create_directories(PresetsRoot());
}
catch (const std::exception&)
{
}
}

// Strip characters Windows won't allow in a file/folder name.
inline std::string SafeSegment(const std::string& name)
{
std::string out;
for (char c : name)
{
if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*'
|| (unsigned char)c < 32)
continue;
out += c;
}
while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
out.pop_back();
while (!out.empty() && out.front() == ' ')
out.erase(out.begin());
return out;
}

// "Metal/Djent/My Rig" -> creates .presets/Metal/Djent/, returns the full
// path of "My Rig.namrig" inside it (empty path on bad input).
inline std::filesystem::path ResolveSavePath(const std::string& typedName)
{
namespace fs = std::filesystem;
std::vector<std::string> segs;
std::string cur;
for (char c : typedName)
{
if (c == '/' || c == '\\')
{
segs.push_back(cur);
cur.clear();
}
else
cur += c;
}
segs.push_back(cur);
fs::path dir = PresetsRoot();
for (size_t i = 0; i + 1 < segs.size(); i++)
{
const std::string s = SafeSegment(segs[i]);
if (!s.empty())
dir /= tonegallery::UTF8ToPath(s);
}
const std::string leaf = SafeSegment(segs.back());
if (leaf.empty())
return fs::path();
try
{
fs::create_directories(dir);
}
catch (const std::exception&)
{
return fs::path();
}
return dir / tonegallery::UTF8ToPath(leaf + ".namrig");
}

// Library-relative path of a preset file (utf8, forward info for display).
inline std::string RelOfPreset(const std::filesystem::path& file)
{
try
{
const auto rel = file.lexically_relative(PresetsRoot());
const std::string r = tonegallery::PathToUTF8(rel);
if (!r.empty() && r.rfind("..", 0) != 0)
return r;
}
catch (const std::exception&)
{
}
return tonegallery::PathToUTF8(file.filename());
}

// Pretty display name of the currently loaded preset ("" -> "No preset").
inline std::string PresetDisplayName(const char* rel)
{
if (rel == nullptr || rel[0] == '\0')
return "No preset";
try
{
return tonegallery::PathToUTF8(tonegallery::UTF8ToPath(rel).stem());
}
catch (const std::exception&)
{
return rel;
}
}

// --- Share/export -----------------------------------------------------------
// Bundles the given rig (as captured JSON) into
// <library>/Exported Rigs/<name>/
// <name>.namrig (rewritten to library-relative paths)
// tones/<folder>... (copies of every tone folder the rig uses)
// README.txt
// The recipient copies tones/* into their own "NAM Tones" folder and the
// .namrig into "NAM Tones/.presets" -- done.
inline bool ExportRigBundle(nlohmann::json rig, const std::string& rawName, std::string& outMsg)
{
namespace fs = std::filesystem;
try
{
const fs::path lib = tonegallery::GetToneLibraryRoot();
std::string name = SafeSegment(rawName);
if (name.empty())
name = "My Rig";
const fs::path dest = lib / "Exported Rigs" / tonegallery::UTF8ToPath(name);
fs::create_directories(dest / "tones");

std::vector<std::string> copiedTop; // library top-level folders already copied
auto copyTopFolder = [&](const std::string& rel) {
// rel is library-relative; copy its first path component wholesale.
std::string top;
for (char c : rel)
{
if (c == '/' || c == '\\')
break;
top += c;
}
if (top.empty())
return;
for (const auto& d : copiedTop)
if (d == top)
return;
copiedTop.push_back(top);
try
{
const fs::path src = lib / tonegallery::UTF8ToPath(top);
if (fs::exists(src))
fs::copy(src, dest / "tones" / tonegallery::UTF8ToPath(top),
fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}
catch (const std::exception&)
{
}
};

// For each model/ir/tone reference: copy library folders in, and pull
// loose (non-library) files into tones/Imported Files/.
auto handlePart = [&](nlohmann::json& obj) {
static const char* kPairs[3][2] = {{"model", "model_rel"}, {"ir", "ir_rel"}, {"tone", "tone_rel"}};
for (int i = 0; i < 3; i++)
{
const char* absKey = kPairs[i][0];
const char* relKey = kPairs[i][1];
std::string rel = obj.value(relKey, "");
const std::string abs = obj.value(absKey, "");
if (!rel.empty())
{
copyTopFolder(rel);
}
else if (!abs.empty())
{
try
{
const fs::path src = tonegallery::UTF8ToPath(abs);
if (fs::exists(src) && fs::is_regular_file(src))
{
fs::create_directories(dest / "tones" / "Imported Files");
fs::copy_file(
src, dest / "tones" / "Imported Files" / src.filename(), fs::copy_options::overwrite_existing);
rel = std::string("Imported Files/") + tonegallery::PathToUTF8(src.filename());
obj[relKey] = rel;
}
}
catch (const std::exception&)
{
}
}
obj[absKey] = ""; // absolute paths are meaningless on another PC
}
};

if (rig.contains("main"))
handlePart(rig["main"]);
if (rig.contains("slots") && rig["slots"].is_array())
for (auto& s : rig["slots"])
handlePart(s);

{
std::ofstream f(dest / tonegallery::UTF8ToPath(name + ".namrig"));
f << rig.dump(2);
}
{
std::ofstream f(dest / "README.txt");
f << "NAM TONE GALLERY - SHARED RIG: " << name << "\r\n"
<< "==========================================\r\n\r\n"
<< "To install this rig:\r\n"
<< "1. Copy everything inside the 'tones' folder into your\r\n"
<< " Documents\\NAM Tones folder.\r\n"
<< "2. Copy '" << name << ".namrig' into Documents\\NAM Tones\\.presets\r\n"
<< " (create the .presets folder if it doesn't exist).\r\n"
<< "3. In the plugin, open the SIGNAL CHAIN view and pick the rig\r\n"
<< " from the preset dropdown at the top.\r\n";
}
outMsg = "Exported to NAM Tones\\Exported Rigs\\" + name;
return true;
}
catch (const std::exception&)
{
outMsg = "Export failed";
return false;
}
}

} // namespace namrig
