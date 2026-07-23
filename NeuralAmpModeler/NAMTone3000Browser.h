#pragma once

// TONE3000 in-plugin browser
//
// Live search of tone3000.com from inside the plugin, with one-click download
// into the local tone library (folder + tone.json + cover photo + model
// files), which then shows up in the sidebar/gallery immediately.
//
// Networking is Windows-only (WinHTTP) and runs on a background worker
// thread; the UI polls job state while animating. The Supabase anonymous key
// the website itself ships to every visitor is extracted at runtime from the
// site's JavaScript and cached in the tone library (t3k.json) -- nothing is
// hardcoded, so it keeps working if the site rotates it.
//
// Include from NeuralAmpModeler.cpp AFTER NAMToneGalleryControl.h.

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#ifdef OS_WIN
  #include <winhttp.h>
  #pragma comment(lib, "winhttp.lib")
#endif

const int kCtrlTagTone3000 = 1006;

namespace t3k
{

// ---------------------------------------------------------------------------
// HTTP (WinHTTP, Windows only)
// ---------------------------------------------------------------------------

inline std::wstring Widen(const std::string& s)
{
  return std::wstring(s.begin(), s.end());
}

inline bool SplitUrl(const std::string& url, std::wstring& host, std::wstring& path)
{
  const std::string https = "https://";
  if (url.rfind(https, 0) != 0)
    return false;
  const std::string rest = url.substr(https.size());
  const size_t slash = rest.find('/');
  host = Widen(slash == std::string::npos ? rest : rest.substr(0, slash));
  path = Widen(slash == std::string::npos ? std::string("/") : rest.substr(slash));
  return !host.empty();
}

inline bool HttpRequest(const std::wstring& host, const std::wstring& path, const wchar_t* method,
                        const std::string& body, const std::string& contentType, const std::string& apiKey,
                        std::string& outBody, int* outStatus = nullptr)
{
#ifdef OS_WIN
  bool ok = false;
  HINTERNET hSession = WinHttpOpen(
    L"NAMToneGallery/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (hSession == nullptr)
    return false;
  WinHttpSetTimeouts(hSession, 8000, 8000, 20000, 30000);
  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  HINTERNET hRequest = hConnect != nullptr
                         ? WinHttpOpenRequest(hConnect, method, path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                         : nullptr;
  if (hRequest != nullptr)
  {
    std::wstring headers;
    if (!contentType.empty())
      headers += L"Content-Type: " + Widen(contentType) + L"\r\n";
    if (!apiKey.empty())
    {
      headers += L"apikey: " + Widen(apiKey) + L"\r\n";
      headers += L"Authorization: Bearer " + Widen(apiKey) + L"\r\n";
    }
    if (WinHttpSendRequest(hRequest, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                           headers.empty() ? 0 : (DWORD)headers.size(),
                           body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
                           (DWORD)body.size(), 0)
        && WinHttpReceiveResponse(hRequest, nullptr))
    {
      DWORD status = 0;
      DWORD statusSize = sizeof(status);
      WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                          &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
      if (outStatus != nullptr)
        *outStatus = (int)status;
      DWORD avail = 0;
      do
      {
        avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
          break;
        std::vector<char> buf(avail);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buf.data(), avail, &bytesRead))
          break;
        outBody.append(buf.data(), bytesRead);
      } while (avail > 0);
      ok = true;
    }
  }
  if (hRequest != nullptr)
    WinHttpCloseHandle(hRequest);
  if (hConnect != nullptr)
    WinHttpCloseHandle(hConnect);
  if (hSession != nullptr)
    WinHttpCloseHandle(hSession);
  return ok;
#else
  return false;
#endif
}

inline bool HttpGetUrl(const std::string& url, const std::string& apiKey, std::string& outBody,
                       int* outStatus = nullptr)
{
  std::wstring host, path;
  if (!SplitUrl(url, host, path))
    return false;
  return HttpRequest(host, path, L"GET", "", "", apiKey, outBody, outStatus);
}

inline bool DownloadUrlToFile(const std::string& url, const std::filesystem::path& dest)
{
  std::string data;
  int status = 0;
  if (!HttpGetUrl(url, "", data, &status) || status != 200 || data.empty())
    return false;
  try
  {
    std::ofstream f(dest, std::ios::binary);
    f.write(data.data(), (std::streamsize)data.size());
    return f.good();
  }
  catch (const std::exception&)
  {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Anonymous API key: cached in <library>/t3k.json, extracted at runtime from
// the site's JavaScript when missing or rejected.
// ---------------------------------------------------------------------------

inline std::string B64UrlDecode(const std::string& in)
{
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '-' || c == '+')
      return 62;
    if (c == '_' || c == '/')
      return 63;
    return -1;
  };
  std::string out;
  int bits = 0, acc = 0;
  for (char c : in)
  {
    const int v = val(c);
    if (v < 0)
      continue;
    acc = (acc << 6) | v;
    bits += 6;
    if (bits >= 8)
    {
      bits -= 8;
      out += (char)((acc >> bits) & 0xFF);
    }
  }
  return out;
}

inline std::string FindAnonKeyInText(const std::string& text)
{
  size_t i = text.find("eyJ");
  while (i != std::string::npos)
  {
    size_t j = i;
    while (j < text.size() && (isalnum((unsigned char)text[j]) || text[j] == '_' || text[j] == '-' || text[j] == '.'))
      j++;
    const std::string token = text.substr(i, j - i);
    const size_t d1 = token.find('.');
    const size_t d2 = d1 == std::string::npos ? std::string::npos : token.find('.', d1 + 1);
    if (d1 != std::string::npos && d2 != std::string::npos && token.find('.', d2 + 1) == std::string::npos)
    {
      const std::string payload = B64UrlDecode(token.substr(d1 + 1, d2 - d1 - 1));
      if (payload.find("\"role\":\"anon\"") != std::string::npos)
        return token;
    }
    i = text.find("eyJ", j);
  }
  return "";
}

inline std::filesystem::path KeyFilePath()
{
  return tonegallery::GetToneLibraryRoot() / "t3k.json";
}

inline std::string LoadKeyFromDisk()
{
  try
  {
    if (std::filesystem::exists(KeyFilePath()))
    {
      std::ifstream f(KeyFilePath());
      nlohmann::json j = nlohmann::json::parse(f, nullptr, true, true);
      if (j.contains("anon_key") && j["anon_key"].is_string())
        return j["anon_key"].get<std::string>();
    }
  }
  catch (const std::exception&)
  {
  }
  return "";
}

inline void SaveKeyToDisk(const std::string& key)
{
  try
  {
    nlohmann::json j;
    j["anon_key"] = key;
    std::filesystem::create_directories(tonegallery::GetToneLibraryRoot());
    std::ofstream f(KeyFilePath());
    f << j.dump(2);
  }
  catch (const std::exception&)
  {
  }
}

// Fetch the site's search page, then its JS chunks, hunting for the public
// anonymous key the site ships to every browser.
inline std::string ExtractKeyFromSite()
{
  std::string html;
  if (!HttpGetUrl("https://www.tone3000.com/search", "", html))
    return "";
  std::string key = FindAnonKeyInText(html);
  if (!key.empty())
    return key;

  // Collect ALL chunk URLs referenced by the page. The key sits in one
  // specific chunk whose position in the list changes between site builds
  // (observed at index 18 of ~32), so no small cap works -- scan them all,
  // stopping as soon as the key turns up.
  std::vector<std::string> chunks;
  size_t i = html.find("/_next/static/");
  while (i != std::string::npos && chunks.size() < 64)
  {
    const size_t end = html.find('"', i);
    if (end == std::string::npos)
      break;
    std::string rel = html.substr(i, end - i);
    while (!rel.empty() && rel.back() == '\\') // JSON-escaped quote in inline scripts
      rel.pop_back();
    if (rel.size() > 3 && rel.find(".js") != std::string::npos)
    {
      const std::string full = "https://www.tone3000.com" + rel;
      bool seen = false;
      for (const auto& c : chunks)
        if (c == full)
          seen = true;
      if (!seen)
        chunks.push_back(full);
    }
    i = html.find("/_next/static/", end);
  }
  for (const auto& url : chunks)
  {
    std::string js;
    if (HttpGetUrl(url, "", js))
    {
      key = FindAnonKeyInText(js);
      if (!key.empty())
        return key;
    }
  }
  return "";
}

inline std::mutex& KeyMutex()
{
  static std::mutex m;
  return m;
}

inline std::string EnsureKey(bool forceRefresh = false)
{
  std::lock_guard<std::mutex> lock(KeyMutex());
  static std::string sKey;
  if (forceRefresh)
    sKey.clear();
  if (sKey.empty() && !forceRefresh)
    sKey = LoadKeyFromDisk();
  if (sKey.empty())
  {
    sKey = ExtractKeyFromSite();
    if (!sKey.empty())
      SaveKeyToDisk(sKey);
  }
  return sKey;
}

// ---------------------------------------------------------------------------
// TONE3000 API
// ---------------------------------------------------------------------------

struct SearchResult
{
  long long id = 0;
  std::string title;
  std::string description;
  std::string author;
  std::string gear; // "amp", "amp + cab", "cab", "pedal", ...
  std::string imageUrl;
  std::vector<std::string> tags;
  long long downloads = 0;
  long long total = 0;
};

struct ModelFile
{
  std::string name;
  std::string url;
  std::string arch; // "1" or "2"
};

inline const char* ApiBase()
{
  return "https://api.tone3000.com";
}

// gearFilter: "" (all) or one of the API's gear enum values:
//   "amp-cab", "amp", "pedal", "outboard", "experimental", "cab", "space"
// orderBy: "" (default) or "trending" / "newest" / "oldest" / "downloads-all-time"
// platform: "nam" (default) or "ir" (standalone impulse responses)
// tags: empty (all) or tag names to require
// NOTE: is_calibrated must be false (not null) or the IR platform returns
// nothing -- this mirrors exactly what tone3000.com's own search sends.
inline bool SearchTones(const std::string& query, const std::string& gearFilter, const std::string& orderBy,
                        const std::string& platform, const std::vector<std::string>& tags, int page, int pageSize,
                        std::vector<SearchResult>& out, long long& total, std::string& err)
{
  std::string key = EnsureKey();
  if (key.empty())
  {
    err = "Couldn't reach TONE3000 (no network?)";
    return false;
  }
  nlohmann::json body = {{"query_term", query},     {"page_number", page},        {"page_size", pageSize},
                         {"order_by", nullptr},     {"tag_names", nullptr},       {"make_names", nullptr},
                         {"gear_filters", nullptr}, {"is_calibrated", false},     {"size_filters", nullptr},
                         {"usernames", nullptr},    {"platform_filter", nullptr}, {"architecture_filter", nullptr}};
  if (!gearFilter.empty())
    body["gear_filters"] = nlohmann::json::array({gearFilter});
  if (!orderBy.empty())
    body["order_by"] = orderBy;
  body["platform_filter"] = platform.empty() ? "nam" : platform;
  if (!tags.empty())
  {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tags)
      arr.push_back(t);
    body["tag_names"] = arr;
  }
  for (int attempt = 0; attempt < 2; attempt++)
  {
    std::string resp;
    int status = 0;
    std::wstring host, path;
    SplitUrl(std::string(ApiBase()) + "/rest/v1/rpc/search_tones_a2", host, path);
    if (!HttpRequest(host, path, L"POST", body.dump(), "application/json", key, resp, &status))
    {
      err = "Network request failed";
      return false;
    }
    if (status == 401 || status == 403)
    {
      key = EnsureKey(true); // key rotated: re-extract and retry once
      if (key.empty())
      {
        err = "TONE3000 authorization failed";
        return false;
      }
      continue;
    }
    if (status != 200)
    {
      err = "TONE3000 returned status " + std::to_string(status);
      return false;
    }
    try
    {
      nlohmann::json j = nlohmann::json::parse(resp);
      out.clear();
      total = 0;
      for (const auto& row : j)
      {
        SearchResult r;
        r.id = row.value("id", 0LL);
        r.title = row.value("title", "");
        r.description = row.value("description", "");
        r.author = row.value("username", "");
        if (row.contains("gear") && row["gear"].is_string())
          r.gear = row["gear"].get<std::string>();
        if (row.contains("images") && row["images"].is_array() && !row["images"].empty()
            && row["images"][0].is_string())
          r.imageUrl = row["images"][0].get<std::string>();
        if (row.contains("tags") && row["tags"].is_array())
          for (const auto& t : row["tags"])
            if (t.is_string())
              r.tags.push_back(t.get<std::string>());
        r.downloads = row.value("downloads_count", 0LL);
        r.total = row.value("total_count", 0LL);
        total = r.total;
        out.push_back(std::move(r));
      }
      return true;
    }
    catch (const std::exception&)
    {
      err = "Couldn't read TONE3000's response";
      return false;
    }
  }
  return false;
}

// TONE3000 curates a ranked list of popular tags (the same ~50 its own
// website shows in the Tags filter). Anyone can read it anonymously.
inline bool FetchRankedTags(std::vector<std::string>& out, std::string& err)
{
  const std::string key = EnsureKey();
  if (key.empty())
  {
    err = "Couldn't reach TONE3000";
    return false;
  }
  std::string resp;
  int status = 0;
  const std::string url = std::string(ApiBase())
                          + "/rest/v1/tags?select=name,category_rank&category_rank=not.is.null"
                            "&order=category_rank.asc&limit=100";
  if (!HttpGetUrl(url, key, resp, &status) || status != 200)
  {
    err = "Couldn't load the tag list";
    return false;
  }
  try
  {
    nlohmann::json j = nlohmann::json::parse(resp);
    for (const auto& row : j)
      if (row.contains("name") && row["name"].is_string())
        out.push_back(row["name"].get<std::string>());
    return true;
  }
  catch (const std::exception&)
  {
    err = "Couldn't read the tag list";
    return false;
  }
}

inline bool FetchModels(long long toneId, std::vector<ModelFile>& out, std::string& err)
{
  const std::string key = EnsureKey();
  if (key.empty())
  {
    err = "Couldn't reach TONE3000";
    return false;
  }
  std::string resp;
  int status = 0;
  const std::string url = std::string(ApiBase()) + "/rest/v1/models?tone_id=eq." + std::to_string(toneId)
                          + "&select=id,name,model_url,architecture_version&order=name";
  if (!HttpGetUrl(url, key, resp, &status) || status != 200)
  {
    err = "Couldn't list the tone's files";
    return false;
  }
  try
  {
    nlohmann::json j = nlohmann::json::parse(resp);
    for (const auto& row : j)
    {
      ModelFile m;
      m.name = row.value("name", "");
      m.url = row.value("model_url", "");
      if (row.contains("architecture_version") && row["architecture_version"].is_string())
        m.arch = row["architecture_version"].get<std::string>();
      if (!m.url.empty())
        out.push_back(std::move(m));
    }
    return true;
  }
  catch (const std::exception&)
  {
    err = "Couldn't read the file list";
    return false;
  }
}

inline std::string SafeFileName(const std::string& name)
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
  if (out.empty())
    out = "Unnamed Tone";
  return out;
}

inline std::string Slugify(const std::string& s)
{
  std::string out;
  bool lastDash = true;
  for (char c : s)
  {
    if (isalnum((unsigned char)c))
    {
      out += (char)tolower((unsigned char)c);
      lastDash = false;
    }
    else if (!lastDash)
    {
      out += '-';
      lastDash = true;
    }
  }
  while (!out.empty() && out.back() == '-')
    out.pop_back();
  return out;
}

inline std::string GearToType(const std::string& gear)
{
  std::string g = gear;
  std::transform(g.begin(), g.end(), g.begin(), [](unsigned char c) { return (char)tolower(c); });
  // The API's gear enum spells it "amp-cab"; the website spells it "amp + cab".
  if (g.find("amp + cab") != std::string::npos || g.find("amp-cab") != std::string::npos
      || g.find("amp cab") != std::string::npos || g.find("full rig") != std::string::npos)
    return "amp_cab";
  if (g.find("pedal") != std::string::npos)
    return "pedal";
  if (g.find("cab") != std::string::npos || g.find("ir") != std::string::npos || g.find("space") != std::string::npos)
    return "ir";
  if (g.find("amp") != std::string::npos)
    return "amp";
  return "other";
}

inline std::string UrlExtension(const std::string& url, const std::string& fallback)
{
  const size_t q = url.find('?');
  const std::string clean = q == std::string::npos ? url : url.substr(0, q);
  const size_t dot = clean.rfind('.');
  if (dot == std::string::npos || clean.size() - dot > 6)
    return fallback;
  return clean.substr(dot);
}

// Download a search result into the local tone library. Returns true on
// success (at least one model file downloaded).
inline bool DownloadTone(const SearchResult& tone, std::string& err)
{
  namespace fs = std::filesystem;
  try
  {
    const fs::path folder = tonegallery::GetToneLibraryRoot() / tonegallery::UTF8ToPath(SafeFileName(tone.title));
    fs::create_directories(folder);

    // Cover photo
    std::string imageName;
    if (!tone.imageUrl.empty())
    {
      imageName = "cover" + UrlExtension(tone.imageUrl, ".jpg");
      if (!DownloadUrlToFile(tone.imageUrl, folder / imageName))
        imageName = "";
    }

    // Model files: prefer A2 versions; fall back to whatever exists. Dedupe
    // by capture name so we don't save both A1 and A2 of the same capture.
    std::vector<ModelFile> models;
    if (!FetchModels(tone.id, models, err))
      return false;
    std::map<std::string, ModelFile> byName;
    for (const auto& m : models)
    {
      auto found = byName.find(m.name);
      if (found == byName.end() || (m.arch == "2" && found->second.arch != "2"))
        byName[m.name] = m;
    }
    int downloaded = 0;
    std::string firstModel; // first .nam
    std::string firstIR; // first .wav (IR tones download as wav files)
    for (const auto& pair : byName)
    {
      const ModelFile& m = pair.second;
      const std::string fileName = SafeFileName(m.name) + UrlExtension(m.url, ".nam");
      if (DownloadUrlToFile(m.url, folder / tonegallery::UTF8ToPath(fileName)))
      {
        downloaded++;
        const bool isWav = fileName.size() > 4 && fileName.compare(fileName.size() - 4, 4, ".wav") == 0;
        if (isWav && firstIR.empty())
          firstIR = fileName;
        if (!isWav && firstModel.empty())
          firstModel = fileName;
      }
    }
    if (downloaded == 0)
    {
      err = "No model files could be downloaded";
      return false;
    }

    // tone.json
    nlohmann::json j;
    j["name"] = tone.title;
    j["author"] = tone.author;
    j["description"] = tone.description;
    j["gear_type"] = GearToType(tone.gear);
    j["tags"] = tone.tags;
    j["image"] = imageName;
    j["url"] = "https://www.tone3000.com/tones/" + Slugify(tone.title) + "-" + std::to_string(tone.id);
    if (!firstModel.empty())
      j["model"] = firstModel;
    if (!firstIR.empty())
      j["ir"] = firstIR;
    std::ofstream f(folder / "tone.json");
    f << j.dump(2);
    return true;
  }
  catch (const std::exception& e)
  {
    err = e.what();
    return false;
  }
}

// ---------------------------------------------------------------------------
// Background worker (shared, sequential job queue)
// ---------------------------------------------------------------------------

class Worker
{
public:
  static Worker& Get()
  {
    static Worker sWorker;
    return sWorker;
  }

  void Enqueue(std::function<void()> job)
  {
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mJobs.push_back(std::move(job));
      if (!mStarted)
      {
        mStarted = true;
        mThread = std::thread([this]() { Run(); });
      }
    }
    mCV.notify_one();
  }

  ~Worker()
  {
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mQuit = true;
    }
    mCV.notify_one();
    if (mThread.joinable())
      mThread.join();
  }

private:
  void Run()
  {
    while (true)
    {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mMutex);
        mCV.wait(lock, [this]() { return mQuit || !mJobs.empty(); });
        if (mQuit && mJobs.empty())
          return;
        job = std::move(mJobs.front());
        mJobs.pop_front();
      }
      try
      {
        job();
      }
      catch (const std::exception&)
      {
      }
    }
  }

  std::mutex mMutex;
  std::condition_variable mCV;
  std::deque<std::function<void()>> mJobs;
  std::thread mThread;
  bool mStarted = false;
  bool mQuit = false;
};

// Shared state between UI and worker jobs (jobs hold shared_ptr copies so the
// UI can be destroyed safely mid-request).
struct BrowserShared
{
  std::mutex mtx;
  std::atomic<bool> busy{false};
  std::atomic<int> generation{0};
  std::vector<SearchResult> results;
  long long total = 0;
  int page = 1;
  std::string query;
  std::string gear; // active gear_filters value ("" = all)
  std::string order; // active order_by value ("" = default)
  std::string platform; // "nam" or "ir"
  std::vector<std::string> tags; // required tag names (empty = all)
  std::string error;
};

// The curated tag list (fetched once per session; same ~50 tags the site shows).
struct TagsShared
{
  std::mutex mtx;
  std::atomic<int> status{0}; // 0=none 1=fetching 2=done 3=error
  std::vector<std::string> names;
};

// Model-file list for the detail page (fetched when a tone is opened).
struct ModelsShared
{
  std::mutex mtx;
  std::atomic<int> status{0}; // 0=none 1=fetching 2=done 3=error
  std::vector<ModelFile> models;
};

struct DownloadShared
{
  std::atomic<int> status{0}; // 0=none 1=downloading 2=done 3=error
  std::string error;
  bool announced = false; // UI-side: refresh done?
};

} // namespace t3k

// ---------------------------------------------------------------------------
// The browser overlay control
// ---------------------------------------------------------------------------

class NAMTone3000BrowserControl : public IControl
{
public:
  static constexpr int kNumCols = 3;
  static constexpr float kCardHeight = 150.0f;
  static constexpr float kCardGap = 10.0f;
  static constexpr float kPhotoHeight = 84.0f;
  static constexpr int kPageSize = 24;

  NAMTone3000BrowserControl(const IRECT& bounds, IFileDialogCompletionHandlerFunc loadModelFunc,
                            IFileDialogCompletionHandlerFunc loadIRFunc)
  : IControl(bounds)
  , mShared(std::make_shared<t3k::BrowserShared>())
  , mLoadModelFunc(loadModelFunc)
  , mLoadIRFunc(loadIRFunc)
  {
  }

  void Show()
  {
    Hide(false);
    if (!mHasSearched)
      StartSearch("");
    SetDirty(false);
  }

  // --- Filter chips -------------------------------------------------------
  static constexpr int kNumGearChips = 6;
  static constexpr int kNumSortChips = 4;
  static constexpr int kNumFormatChips = 2;
  const char* GearChipLabelFor(int i) const
  {
    static const char* kNAM[kNumGearChips] = {"ALL", "AMP+CAB", "AMPS", "PEDALS", "OUTBOARD", "EXPERIMENTAL"};
    static const char* kIR[kNumGearChips] = {"ALL", "CABS", "SPACES", "PEDALS", "OUTBOARD", "EXPERIMENTAL"};
    return mFormatSel == 1 ? kIR[i] : kNAM[i];
  }
  const char* GearChipValueFor(int i) const
  {
    static const char* kNAM[kNumGearChips] = {"", "amp-cab", "amp", "pedal", "outboard", "experimental"};
    static const char* kIR[kNumGearChips] = {"", "cab", "space", "pedal", "outboard", "experimental"};
    return mFormatSel == 1 ? kIR[i] : kNAM[i];
  }
  static const char* SortChipLabel(int i)
  {
    static const char* kLabels[kNumSortChips] = {"TRENDING", "NEWEST", "OLDEST", "MOST DL"};
    return kLabels[i];
  }
  static const char* SortChipValue(int i)
  {
    static const char* kValues[kNumSortChips] = {"trending", "newest", "oldest", "downloads-all-time"};
    return kValues[i];
  }
  static const char* FormatChipLabel(int i)
  {
    static const char* kLabels[kNumFormatChips] = {"NAM", "IRS"};
    return kLabels[i];
  }
  static const char* FormatChipValue(int i)
  {
    static const char* kValues[kNumFormatChips] = {"nam", "ir"};
    return kValues[i];
  }

  void StartSearch(const std::string& query)
  {
    mHasSearched = true;
    auto shared = mShared;
    const int generation = ++shared->generation;
    const std::string gear = GearChipValueFor(mGearSel);
    const std::string order = SortChipValue(mSortSel);
    const std::string platform = FormatChipValue(mFormatSel);
    const std::vector<std::string> tags(mSelectedTags.begin(), mSelectedTags.end());
    {
      std::lock_guard<std::mutex> lock(shared->mtx);
      shared->query = query;
      shared->gear = gear;
      shared->order = order;
      shared->platform = platform;
      shared->tags = tags;
      shared->error.clear();
      shared->page = 1;
    }
    shared->busy = true;
    mScroll = 0.0f;
    t3k::Worker::Get().Enqueue([shared, query, gear, order, platform, tags, generation]() {
      std::vector<t3k::SearchResult> results;
      long long total = 0;
      std::string err;
      const bool ok = t3k::SearchTones(query, gear, order, platform, tags, 1, kPageSize, results, total, err);
      std::lock_guard<std::mutex> lock(shared->mtx);
      if (shared->generation == generation)
      {
        if (ok)
        {
          shared->results = std::move(results);
          shared->total = total;
        }
        else
          shared->error = err;
        shared->busy = false;
      }
    });
    SetDirty(false);
  }

  // Re-run the current query (used when a filter chip changes).
  void RestartSearch()
  {
    std::string query;
    {
      std::lock_guard<std::mutex> lock(mShared->mtx);
      query = mShared->query;
    }
    StartSearch(query);
  }

  void LoadMore()
  {
    auto shared = mShared;
    if (shared->busy)
      return;
    const int generation = shared->generation.load();
    int nextPage;
    std::string query, gear, order, platform;
    std::vector<std::string> tags;
    {
      std::lock_guard<std::mutex> lock(shared->mtx);
      nextPage = shared->page + 1;
      query = shared->query;
      gear = shared->gear;
      order = shared->order;
      platform = shared->platform;
      tags = shared->tags;
    }
    shared->busy = true;
    t3k::Worker::Get().Enqueue([shared, query, gear, order, platform, tags, nextPage, generation]() {
      std::vector<t3k::SearchResult> results;
      long long total = 0;
      std::string err;
      const bool ok = t3k::SearchTones(query, gear, order, platform, tags, nextPage, kPageSize, results, total, err);
      std::lock_guard<std::mutex> lock(shared->mtx);
      if (shared->generation == generation)
      {
        if (ok)
        {
          for (auto& r : results)
            shared->results.push_back(std::move(r));
          shared->total = total;
          shared->page = nextPage;
        }
        else
          shared->error = err;
        shared->busy = false;
      }
    });
    SetDirty(false);
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override { StartSearch(str != nullptr ? str : ""); }

  void Draw(IGraphics& g) override
  {
    const IColor accent = tonegallery::AccentColor();
    g.FillRect(IColor(255, 14, 14, 17), mRECT);

    const IRECT content = mRECT.GetPadded(-24.0f);

    // Title + close
    const IText titleText(16.0f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(titleText, "TONE3000", content.GetFromTop(26.0f));
    IRECT t3kBadge;
    g.MeasureText(titleText, "TONE3000", t3kBadge);
    const IText liveText(9.0f, accent, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(liveText, "LIVE SEARCH", content.GetFromTop(26.0f).GetReducedFromLeft(t3kBadge.W() + 10.0f));
    const IRECT close = CloseRect();
    const IColor xColor = mMouseOverClose ? COLOR_WHITE : IColor(255, 170, 173, 182);
    const IRECT xr = close.GetCentredInside(10.0f);
    g.DrawLine(xColor, xr.L, xr.T, xr.R, xr.B, nullptr, 1.8f);
    g.DrawLine(xColor, xr.L, xr.B, xr.R, xr.T, nullptr, 1.8f);

    // Detail page replaces the search UI while a tone is open.
    if (mDetailOpen)
    {
      bool thumbLoading = false;
      DrawDetail(g, accent, content, thumbLoading);
      FinishDraw(thumbLoading);
      return;
    }

    // Search field
    const IRECT field = SearchFieldRect();
    g.FillRoundRect(IColor(255, 32, 33, 41), field, field.H() * 0.5f);
    g.DrawRoundRect(mMouseOverField ? accent : IColor(18, 255, 255, 255), field, field.H() * 0.5f);
    // magnifier icon
    const IRECT mag = field.GetFromLeft(30.0f).GetCentredInside(12.0f);
    g.DrawEllipse(IColor(255, 139, 142, 152), mag.GetFromTLHC(9.0f, 9.0f));
    g.DrawLine(IColor(255, 139, 142, 152), mag.L + 8.0f, mag.T + 8.0f, mag.R, mag.B, nullptr, 1.6f);
    std::string query;
    std::string error;
    long long total = 0;
    size_t numResults = 0;
    {
      std::lock_guard<std::mutex> lock(mShared->mtx);
      query = mShared->query;
      error = mShared->error;
      total = mShared->total;
      numResults = mShared->results.size();
    }
    const IText fieldText(
      11.0f, query.empty() ? IColor(255, 110, 113, 122) : COLOR_WHITE, "Inter-Regular", EAlign::Near, EVAlign::Middle);
    g.DrawText(fieldText, query.empty() ? "Search amps, pedals, cabs... (click, type, press Enter)" : query.c_str(),
               field.GetReducedFromLeft(32.0f).GetReducedFromRight(10.0f));

    // Row 1: format toggle (NAM / IRS) then gear-type chips.
    for (int i = 0; i < kNumFormatChips; i++)
    {
      const IRECT chip = FormatChipRect(i);
      const bool sel = i == mFormatSel;
      const bool over = mMouseOverFormatChip == i;
      g.FillRoundRect(sel ? accent : IColor(255, 26, 27, 33), chip, 4.0f);
      if (over && !sel)
        g.DrawRoundRect(accent, chip, 4.0f);
      const IText chipText(7.5f, sel ? COLOR_BLACK : (over ? COLOR_WHITE : IColor(255, 150, 153, 162)), "Inter-Bold",
                           EAlign::Center, EVAlign::Middle);
      g.DrawText(chipText, FormatChipLabel(i), chip);
    }
    for (int i = 0; i < kNumGearChips; i++)
    {
      const IRECT chip = GearChipRect(i);
      const bool sel = i == mGearSel;
      const bool over = mMouseOverGearChip == i;
      g.FillRoundRect(sel ? accent : IColor(255, 32, 33, 41), chip, chip.H() * 0.5f);
      if (over && !sel)
        g.DrawRoundRect(accent, chip, chip.H() * 0.5f);
      const IText chipText(7.5f, sel ? COLOR_BLACK : (over ? COLOR_WHITE : IColor(255, 150, 153, 162)), "Inter-Bold",
                           EAlign::Center, EVAlign::Middle);
      g.DrawText(chipText, GearChipLabelFor(i), chip);
    }

    // Row 2: sort order, TAGS toggle, then the status text on the right.
    for (int i = 0; i < kNumSortChips; i++)
    {
      const IRECT chip = SortChipRect(i);
      const bool sel = i == mSortSel;
      const bool over = mMouseOverSortChip == i;
      if (sel)
        g.FillRoundRect(accent.WithOpacity(0.18f), chip, chip.H() * 0.5f);
      const IText chipText(7.5f, sel ? accent : (over ? COLOR_WHITE : IColor(255, 120, 123, 132)), "Inter-Bold",
                           EAlign::Center, EVAlign::Middle);
      g.DrawText(chipText, SortChipLabel(i), chip);
    }
    {
      const IRECT chip = TagsChipRect();
      const bool active = !mSelectedTags.empty() || mTagsOpen;
      g.FillRoundRect(active ? accent : IColor(255, 32, 33, 41), chip, chip.H() * 0.5f);
      if (mMouseOverTagsBtn && !active)
        g.DrawRoundRect(accent, chip, chip.H() * 0.5f);
      const IText chipText(7.5f, active ? COLOR_BLACK : (mMouseOverTagsBtn ? COLOR_WHITE : IColor(255, 150, 153, 162)),
                           "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(chipText, TagsChipLabel().c_str(), chip);
    }

    // Status (right side of row 2)
    const IText statusText(9.0f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Far, EVAlign::Middle);
    const IRECT statusArea = StatusRect();
    if (mShared->busy)
    {
      mSpinnerPhase += 14.0f;
      if (mSpinnerPhase > 360.0f)
        mSpinnerPhase -= 360.0f;
      g.DrawArc(
        accent, statusArea.R - 8.0f, statusArea.MH(), 6.0f, mSpinnerPhase, mSpinnerPhase + 270.0f, nullptr, 2.0f);
      g.DrawText(statusText, "Searching TONE3000...", statusArea.GetReducedFromRight(20.0f));
    }
    else if (!error.empty())
    {
      const IText errText(9.0f, IColor(255, 232, 90, 90), "Inter-Regular", EAlign::Far, EVAlign::Middle);
      g.DrawText(errText, error.c_str(), statusArea);
    }
    else if (numResults > 0)
    {
      std::stringstream ss;
      ss << numResults << " of " << total << (mFormatSel == 1 ? " IRs" : " tones") << " - click for details";
      g.DrawText(statusText, ss.str().c_str(), statusArea);
    }

    // Results grid
    const IRECT grid = GridRect();
    g.PathClipRegion(grid);
    bool anyDownloading = false;
    {
      std::lock_guard<std::mutex> lock(mShared->mtx);
      for (int i = 0; i < (int)mShared->results.size(); i++)
      {
        const IRECT card = CardRect(i);
        if (card.B < grid.T || card.T > grid.B)
          continue;
        DrawCard(g, mShared->results[i], card, i == mMouseOverCard, accent, anyDownloading);
      }
      // Load more button
      if ((long long)mShared->results.size() < mShared->total)
      {
        const IRECT more = LoadMoreRect((int)mShared->results.size());
        if (more.B >= grid.T && more.T <= grid.B)
        {
          g.FillRoundRect(mMouseOverMore ? accent : accent.WithOpacity(0.2f), more, 12.0f);
          const IText moreText(
            10.0f, mMouseOverMore ? COLOR_BLACK : accent, "Inter-Bold", EAlign::Center, EVAlign::Middle);
          g.DrawText(moreText, "LOAD MORE", more);
        }
      }
    }
    // Scrollbar
    const float contentH = ContentHeight();
    if (contentH > grid.H())
    {
      const float frac = grid.H() / contentH;
      const float barH = std::max(18.0f, frac * grid.H());
      const float travel = grid.H() - barH;
      const float pos = (mScroll / (contentH - grid.H())) * travel;
      g.FillRoundRect(
        accent.WithOpacity(0.5f), IRECT(mRECT.R - 6.0f, grid.T + pos, mRECT.R - 4.0f, grid.T + pos + barH), 1.0f);
    }
    g.PathClipRegion();

    // Tags panel (overlays the top of the grid while open)
    if (mTagsOpen)
      DrawTagsPanel(g, accent);

    FinishDraw(anyDownloading);
  }

  void DrawTagsPanel(IGraphics& g, const IColor& accent)
  {
    const IRECT panel = TagPanelRect();
    g.FillRoundRect(IColor(255, 23, 24, 28), panel, 8.0f);
    g.DrawRoundRect(IColor(40, 255, 255, 255), panel, 8.0f);

    const IRECT inner = panel.GetPadded(-10.0f);
    const IText headText(8.5f, IColor(255, 139, 142, 152), "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(headText, "FILTER BY TAGS", inner.GetFromTop(14.0f));
    if (!mSelectedTags.empty())
    {
      const IRECT clear = TagClearRect();
      const IText clearText(8.0f, mMouseOverClear ? COLOR_WHITE : accent, "Inter-Bold", EAlign::Far, EVAlign::Middle);
      g.DrawText(clearText, "CLEAR ALL", clear);
    }

    auto tagsShared = mTagsShared;
    const int status = tagsShared != nullptr ? tagsShared->status.load() : 0;
    if (status == 1)
    {
      mSpinnerPhase += 10.0f;
      if (mSpinnerPhase > 360.0f)
        mSpinnerPhase -= 360.0f;
      g.DrawArc(accent, inner.L + 8.0f, inner.T + 34.0f, 6.0f, mSpinnerPhase, mSpinnerPhase + 270.0f, nullptr, 2.0f);
      const IText loadText(8.5f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Near, EVAlign::Middle);
      g.DrawText(
        loadText, "Loading tags from TONE3000...", IRECT(inner.L + 20.0f, inner.T + 26.0f, inner.R, inner.T + 42.0f));
    }
    else if (status == 3)
    {
      const IText errText(8.5f, IColor(255, 232, 90, 90), "Inter-Regular", EAlign::Near, EVAlign::Middle);
      g.DrawText(errText, "Couldn't load the tag list - try reopening.",
                 IRECT(inner.L, inner.T + 26.0f, inner.R, inner.T + 42.0f));
    }
    else if (status == 2)
    {
      std::vector<std::pair<std::string, IRECT>> chips;
      mTagContentH = LayoutTagChips(chips);
      g.PathClipRegion(inner.GetReducedFromTop(18.0f));
      for (const auto& c : chips)
      {
        const bool sel = mSelectedTags.count(c.first) > 0;
        const bool over = c.first == mHoverTag;
        g.FillRoundRect(sel ? accent : IColor(255, 40, 41, 49), c.second, c.second.H() * 0.5f);
        if (over && !sel)
          g.DrawRoundRect(accent, c.second, c.second.H() * 0.5f);
        const IText tagText(7.0f, sel ? COLOR_BLACK : (over ? COLOR_WHITE : IColor(255, 170, 173, 182)),
                            "Inter-Regular", EAlign::Center, EVAlign::Middle);
        g.DrawText(tagText, c.first.c_str(), c.second);
      }
      // panel scrollbar
      const IRECT scrollArea = inner.GetReducedFromTop(18.0f);
      if (mTagContentH > scrollArea.H())
      {
        const float frac = scrollArea.H() / mTagContentH;
        const float barH = std::max(14.0f, frac * scrollArea.H());
        const float travel = scrollArea.H() - barH;
        const float pos = (mTagScroll / (mTagContentH - scrollArea.H())) * travel;
        g.FillRoundRect(accent.WithOpacity(0.5f),
                        IRECT(panel.R - 5.0f, scrollArea.T + pos, panel.R - 3.0f, scrollArea.T + pos + barH), 1.0f);
      }
      g.PathClipRegion();
    }
  }

  // Common end-of-draw work for both pages: announce finished downloads to
  // the library views, and keep redrawing while anything is in flight.
  void FinishDraw(bool keepPolling)
  {
    for (auto& pair : mDownloads)
    {
      if (pair.second->status == 2 && !pair.second->announced)
      {
        pair.second->announced = true;
        if (IControl* pSidebar = GetUI()->GetControlWithTag(kCtrlTagToneSidebar))
          pSidebar->As<NAMToneSidebarControl>()->Refresh();
      }
      if (pair.second->status == 1)
        keepPolling = true;
    }
    if (mDetailOpen && mDetailModels != nullptr && mDetailModels->status.load() == 1)
      keepPolling = true;
    if (mTagsOpen && mTagsShared != nullptr && mTagsShared->status.load() == 1)
      keepPolling = true;
    if (mShared->busy || keepPolling)
      SetDirty(false); // keep animating/polling
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (CloseRect().Contains(x, y))
    {
      Hide(true);
      mDetailOpen = false;
      return;
    }
    if (mDetailOpen)
    {
      if (BackRect().Contains(x, y))
      {
        mDetailOpen = false;
        SetDirty(false);
        return;
      }
      if (DownloadButtonRect().Contains(x, y))
      {
        auto found = mDownloads.find(mDetailTone.id);
        if (found == mDownloads.end() || found->second->status == 3) // not started, or retry after failure
          StartDownload(mDetailTone);
      }
      return;
    }
    // Tags panel gets first claim on clicks while it's open.
    if (mTagsOpen)
    {
      if (TagsChipRect().Contains(x, y))
      {
        mTagsOpen = false;
        SetDirty(false);
        return;
      }
      if (TagPanelRect().Contains(x, y))
      {
        if (!mSelectedTags.empty() && TagClearRect().Contains(x, y))
        {
          mSelectedTags.clear();
          RestartSearch();
          return;
        }
        std::vector<std::pair<std::string, IRECT>> chips;
        LayoutTagChips(chips);
        const IRECT inner = TagPanelRect().GetPadded(-10.0f).GetReducedFromTop(18.0f);
        for (const auto& c : chips)
        {
          if (c.second.Contains(x, y) && inner.Contains(x, y))
          {
            if (mSelectedTags.count(c.first))
              mSelectedTags.erase(c.first);
            else
              mSelectedTags.insert(c.first);
            RestartSearch();
            return;
          }
        }
        return; // click inside the panel but not on a chip: swallow it
      }
      // click outside the panel closes it (and does nothing else)
      if (GridRect().Contains(x, y))
      {
        mTagsOpen = false;
        SetDirty(false);
        return;
      }
    }
    for (int i = 0; i < kNumFormatChips; i++)
    {
      if (FormatChipRect(i).Contains(x, y))
      {
        if (mFormatSel != i)
        {
          mFormatSel = i;
          RestartSearch();
        }
        return;
      }
    }
    for (int i = 0; i < kNumGearChips; i++)
    {
      if (GearChipRect(i).Contains(x, y))
      {
        if (mGearSel != i)
        {
          mGearSel = i;
          RestartSearch();
        }
        return;
      }
    }
    for (int i = 0; i < kNumSortChips; i++)
    {
      if (SortChipRect(i).Contains(x, y))
      {
        if (mSortSel != i)
        {
          mSortSel = i;
          RestartSearch();
        }
        return;
      }
    }
    if (TagsChipRect().Contains(x, y))
    {
      OpenTagsPanel();
      return;
    }
    if (SearchFieldRect().Contains(x, y))
    {
      std::string query;
      {
        std::lock_guard<std::mutex> lock(mShared->mtx);
        query = mShared->query;
      }
      const IText entryText(12.0f, COLOR_WHITE, "Inter-Regular", EAlign::Near, EVAlign::Middle);
      GetUI()->CreateTextEntry(*this, entryText, SearchFieldRect().GetReducedFromLeft(30.0f), query.c_str());
      return;
    }
    if (GridRect().Contains(x, y))
    {
      bool more = false;
      t3k::SearchResult clicked;
      bool haveClicked = false;
      {
        std::lock_guard<std::mutex> lock(mShared->mtx);
        if ((long long)mShared->results.size() < mShared->total
            && LoadMoreRect((int)mShared->results.size()).Contains(x, y))
          more = true;
        else
        {
          const int idx = CardAt(x, y);
          if (idx >= 0 && idx < (int)mShared->results.size())
          {
            clicked = mShared->results[idx];
            haveClicked = true;
          }
        }
      }
      if (more)
      {
        LoadMore();
        return;
      }
      if (haveClicked)
        OpenDetail(clicked);
    }
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    if (mDetailOpen)
    {
      const float maxScroll = std::max(0.0f, mDetailContentH - DetailScrollRect().H());
      const float next = std::min(maxScroll, std::max(0.0f, mDetailScroll - d * 44.0f));
      if (next != mDetailScroll)
      {
        mDetailScroll = next;
        SetDirty(false);
      }
      return;
    }
    if (mTagsOpen && TagPanelRect().Contains(x, y))
    {
      const float area = TagPanelRect().GetPadded(-10.0f).GetReducedFromTop(18.0f).H();
      const float maxScroll = std::max(0.0f, mTagContentH - area);
      const float next = std::min(maxScroll, std::max(0.0f, mTagScroll - d * 30.0f));
      if (next != mTagScroll)
      {
        mTagScroll = next;
        SetDirty(false);
      }
      return;
    }
    const float maxScroll = std::max(0.0f, ContentHeight() - GridRect().H());
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
    const bool overClose = CloseRect().Contains(x, y);
    bool overField = false, overMore = false, overBack = false, overDL = false;
    bool overTagsBtn = false, overClear = false;
    int overCard = -1, overGearChip = -1, overSortChip = -1, overFormatChip = -1;
    std::string hoverTag;
    if (mDetailOpen)
    {
      overBack = BackRect().Contains(x, y);
      overDL = DownloadButtonRect().Contains(x, y);
    }
    else
    {
      overField = SearchFieldRect().Contains(x, y);
      for (int i = 0; i < kNumFormatChips; i++)
        if (FormatChipRect(i).Contains(x, y))
          overFormatChip = i;
      for (int i = 0; i < kNumGearChips; i++)
        if (GearChipRect(i).Contains(x, y))
          overGearChip = i;
      for (int i = 0; i < kNumSortChips; i++)
        if (SortChipRect(i).Contains(x, y))
          overSortChip = i;
      overTagsBtn = TagsChipRect().Contains(x, y);
      if (mTagsOpen && TagPanelRect().Contains(x, y))
      {
        overClear = !mSelectedTags.empty() && TagClearRect().Contains(x, y);
        std::vector<std::pair<std::string, IRECT>> chips;
        LayoutTagChips(chips);
        const IRECT inner = TagPanelRect().GetPadded(-10.0f).GetReducedFromTop(18.0f);
        for (const auto& c : chips)
          if (c.second.Contains(x, y) && inner.Contains(x, y))
            hoverTag = c.first;
      }
      else if (GridRect().Contains(x, y) && !mTagsOpen)
      {
        std::lock_guard<std::mutex> lock(mShared->mtx);
        overCard = CardAt(x, y);
        if ((long long)mShared->results.size() < mShared->total
            && LoadMoreRect((int)mShared->results.size()).Contains(x, y))
          overMore = true;
      }
    }
    if (overClose != mMouseOverClose || overField != mMouseOverField || overCard != mMouseOverCard
        || overMore != mMouseOverMore || overBack != mMouseOverBack || overDL != mMouseOverDL
        || overGearChip != mMouseOverGearChip || overSortChip != mMouseOverSortChip
        || overFormatChip != mMouseOverFormatChip || overTagsBtn != mMouseOverTagsBtn || overClear != mMouseOverClear
        || hoverTag != mHoverTag)
    {
      mMouseOverClose = overClose;
      mMouseOverField = overField;
      mMouseOverCard = overCard;
      mMouseOverMore = overMore;
      mMouseOverBack = overBack;
      mMouseOverDL = overDL;
      mMouseOverGearChip = overGearChip;
      mMouseOverSortChip = overSortChip;
      mMouseOverFormatChip = overFormatChip;
      mMouseOverTagsBtn = overTagsBtn;
      mMouseOverClear = overClear;
      mHoverTag = hoverTag;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mMouseOverClose = false;
    mMouseOverField = false;
    mMouseOverCard = -1;
    mMouseOverMore = false;
    mMouseOverBack = false;
    mMouseOverDL = false;
    mMouseOverGearChip = -1;
    mMouseOverSortChip = -1;
    mMouseOverFormatChip = -1;
    mMouseOverTagsBtn = false;
    mMouseOverClear = false;
    mHoverTag.clear();
    SetDirty(false);
  }

private:
  void StartDownload(const t3k::SearchResult& tone)
  {
    auto found = mDownloads.find(tone.id);
    if (found != mDownloads.end() && (found->second->status == 1 || found->second->status == 2))
      return; // already downloading or done
    auto state = std::make_shared<t3k::DownloadShared>();
    state->status = 1;
    mDownloads[tone.id] = state;
    t3k::Worker::Get().Enqueue([tone, state]() {
      std::string err;
      const bool ok = t3k::DownloadTone(tone, err);
      state->error = err;
      state->status = ok ? 2 : 3;
    });
    SetDirty(false);
  }

  IRECT CloseRect() const { return mRECT.GetFromTRHC(44.0f, 44.0f).GetCentredInside(22.0f); }
  IRECT SearchFieldRect() const { return mRECT.GetPadded(-24.0f).GetReducedFromTop(32.0f).GetFromTop(30.0f); }
  IRECT GridRect() const { return mRECT.GetPadded(-24.0f).GetReducedFromTop(112.0f).GetReducedFromBottom(2.0f); }

  // --- Filter chip geometry ------------------------------------------------
  static float ChipW(const char* label) { return 14.0f + 4.4f * (float)strlen(label); }
  IRECT ChipRow1() const { return mRECT.GetPadded(-24.0f).GetReducedFromTop(66.0f).GetFromTop(16.0f); }
  IRECT ChipRow2() const { return mRECT.GetPadded(-24.0f).GetReducedFromTop(86.0f).GetFromTop(16.0f); }

  IRECT FormatChipRect(int i) const
  {
    const IRECT row = ChipRow1();
    float x = row.L;
    for (int k = 0; k < i; k++)
      x += ChipW(FormatChipLabel(k)) + 2.0f;
    return IRECT(x, row.T, x + ChipW(FormatChipLabel(i)), row.B);
  }

  float GearChipsStartX() const
  {
    float x = ChipRow1().L;
    for (int k = 0; k < kNumFormatChips; k++)
      x += ChipW(FormatChipLabel(k)) + 2.0f;
    return x + 10.0f; // gap between the format toggle and the gear chips
  }

  IRECT GearChipRect(int i) const
  {
    const IRECT row = ChipRow1();
    float x = GearChipsStartX();
    for (int k = 0; k < i; k++)
      x += ChipW(GearChipLabelFor(k)) + 5.0f;
    return IRECT(x, row.T, x + ChipW(GearChipLabelFor(i)), row.B);
  }

  IRECT SortChipRect(int i) const
  {
    const IRECT row = ChipRow2();
    float x = row.L;
    for (int k = 0; k < i; k++)
      x += ChipW(SortChipLabel(k)) + 5.0f;
    return IRECT(x, row.T, x + ChipW(SortChipLabel(i)), row.B);
  }

  std::string TagsChipLabel() const
  {
    if (mSelectedTags.empty())
      return "TAGS";
    return "TAGS (" + std::to_string(mSelectedTags.size()) + ")";
  }

  IRECT TagsChipRect() const
  {
    const IRECT row = ChipRow2();
    float x = row.L;
    for (int k = 0; k < kNumSortChips; k++)
      x += ChipW(SortChipLabel(k)) + 5.0f;
    x += 8.0f;
    return IRECT(x, row.T, x + ChipW(TagsChipLabel().c_str()), row.B);
  }

  IRECT StatusRect() const
  {
    const IRECT row = ChipRow2();
    return IRECT(TagsChipRect().R + 10.0f, row.T, row.R, row.B);
  }

  // --- Tags panel ----------------------------------------------------------
  IRECT TagPanelRect() const
  {
    const IRECT grid = GridRect();
    return grid.GetFromTop(std::min(grid.H(), 170.0f));
  }

  IRECT TagClearRect() const { return TagPanelRect().GetPadded(-10.0f).GetFromTop(14.0f).GetFromRight(70.0f); }

  // Lays out the tag chips (with the current scroll applied); returns the
  // total content height so scrolling can be clamped.
  float LayoutTagChips(std::vector<std::pair<std::string, IRECT>>& out) const
  {
    const IRECT inner = TagPanelRect().GetPadded(-10.0f).GetReducedFromTop(18.0f);
    std::vector<std::string> names;
    if (mTagsShared != nullptr)
    {
      std::lock_guard<std::mutex> lock(mTagsShared->mtx);
      names = mTagsShared->names;
    }
    float cx = inner.L;
    float cy = inner.T - mTagScroll;
    float rowH = 19.0f;
    for (const auto& name : names)
    {
      const float w = 12.0f + 4.4f * (float)name.size();
      if (cx + w > inner.R && cx > inner.L)
      {
        cx = inner.L;
        cy += rowH;
      }
      out.push_back({name, IRECT(cx, cy, cx + w, cy + 14.0f)});
      cx += w + 5.0f;
    }
    return (cy + 14.0f + mTagScroll) - inner.T + 4.0f;
  }

  void OpenTagsPanel()
  {
    mTagsOpen = true;
    mTagScroll = 0.0f;
    if (mTagsShared == nullptr || mTagsShared->status == 3)
    {
      auto ts = std::make_shared<t3k::TagsShared>();
      mTagsShared = ts;
      ts->status = 1;
      t3k::Worker::Get().Enqueue([ts]() {
        std::vector<std::string> names;
        std::string err;
        const bool ok = t3k::FetchRankedTags(names, err);
        std::lock_guard<std::mutex> lock(ts->mtx);
        ts->names = std::move(names);
        ts->status = ok ? 2 : 3;
      });
    }
    SetDirty(false);
  }

  // --- Detail page ---------------------------------------------------------
  IRECT BackRect() const
  {
    const IRECT row = mRECT.GetPadded(-24.0f).GetFromTop(26.0f);
    return row.GetFromRight(120.0f).GetTranslated(-30.0f, 0.0f);
  }
  IRECT DownloadButtonRect() const { return mRECT.GetPadded(-24.0f).GetFromBottom(32.0f).GetMidHPadded(95.0f); }
  IRECT DetailScrollRect() const
  {
    return mRECT.GetPadded(-24.0f).GetReducedFromTop(34.0f).GetReducedFromBottom(42.0f);
  }

  void OpenDetail(const t3k::SearchResult& tone)
  {
    mDetailOpen = true;
    mDetailTone = tone;
    mDetailScroll = 0.0f;
    mDetailContentH = 0.0f;
    auto ms = std::make_shared<t3k::ModelsShared>();
    mDetailModels = ms;
    ms->status = 1;
    const long long id = tone.id;
    t3k::Worker::Get().Enqueue([ms, id]() {
      std::vector<t3k::ModelFile> models;
      std::string err;
      const bool ok = t3k::FetchModels(id, models, err);
      std::lock_guard<std::mutex> lock(ms->mtx);
      ms->models = std::move(models);
      ms->status = ok ? 2 : 3;
    });
    SetDirty(false);
  }

  void DrawDetail(IGraphics& g, const IColor& accent, const IRECT& content, bool& thumbLoading)
  {
    const t3k::SearchResult& r = mDetailTone;

    // Back button (top row, next to the title)
    const IRECT back = BackRect();
    const IColor backColor = mMouseOverBack ? COLOR_WHITE : IColor(255, 150, 153, 162);
    const IText backText(9.0f, backColor, "Inter-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(backText, "< BACK TO RESULTS", back);

    // Scrollable column
    const IRECT area = DetailScrollRect();
    g.PathClipRegion(area);
    float y = area.T - mDetailScroll;

    // Photo, cover-cropped, with a darker-at-the-top gradient like the
    // library's detail panel.
    const IRECT photo = IRECT(area.L, y, area.R, y + 150.0f);
    const IRECT photoVis = photo.Intersect(area);
    IBitmap* pBitmap = GetThumb(r);
    if (pBitmap != nullptr && pBitmap->W() > 0 && pBitmap->H() > 0 && photoVis.W() > 0.5f && photoVis.H() > 0.5f)
    {
      const float bmpAspect = (float)pBitmap->W() / (float)pBitmap->H();
      const float areaAspect = photo.W() / photo.H();
      IRECT cover = photo;
      if (bmpAspect > areaAspect)
        cover = photo.GetMidHPadded(0.5f * photo.H() * bmpAspect);
      else
        cover = photo.GetMidVPadded(0.5f * photo.W() / bmpAspect);
      g.PathClipRegion(photoVis);
      g.DrawFittedBitmap(*pBitmap, cover);
      // Dark gradient over the top of the photo (darkest at the very top)
      g.PathRect(photo.GetFromTop(64.0f));
      g.PathFill(IPattern::CreateLinearGradient(photo.L, photo.T, photo.L, photo.T + 64.0f,
                                                {{COLOR_BLACK.WithOpacity(0.6f), 0.0f}, {COLOR_TRANSPARENT, 1.0f}}));
      g.PathClipRegion(area);
    }
    else
    {
      g.FillRoundRect(accent.WithOpacity(0.08f), photo, 8.0f);
      if (!r.imageUrl.empty())
        thumbLoading = true;
    }
    y += 160.0f;

    // Full title
    const IText titleText(14.0f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Top);
    for (const auto& line : tonegallery::WrapLines(r.title, 60, 3))
    {
      g.DrawText(titleText, line.c_str(), IRECT(area.L, y, area.R, y + 18.0f));
      y += 18.0f;
    }
    y += 2.0f;

    // Byline
    const IText byText(9.0f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Near, EVAlign::Top);
    std::stringstream by;
    by << "by " << r.author;
    if (r.downloads > 0)
      by << "  -  " << r.downloads << " downloads";
    g.DrawText(byText, by.str().c_str(), IRECT(area.L, y, area.R, y + 12.0f));
    y += 18.0f;

    // Gear chip + tag chips (flowing, wraps to new rows)
    {
      const std::string gearType = t3k::GearToType(r.gear);
      const tonegallery::GearType gt = tonegallery::GearTypeFromString(gearType);
      std::vector<std::pair<std::string, IColor>> chips;
      chips.push_back({tonegallery::GearTypeChipLabel(gt), tonegallery::GearTypeColor(gt)});
      for (const auto& t : r.tags)
        chips.push_back({t, IColor(255, 55, 57, 66)});
      float cx = area.L;
      for (const auto& chip : chips)
      {
        const float w = 12.0f + 4.6f * (float)chip.first.size();
        if (cx + w > area.R && cx > area.L)
        {
          cx = area.L;
          y += 18.0f;
        }
        const IRECT cr = IRECT(cx, y, cx + w, y + 13.0f);
        g.FillRoundRect(chip.second, cr, 5.0f);
        const bool dark = &chip == &chips[0]; // gear chip gets dark text on its bright color
        const IText chipText(
          7.0f, dark ? COLOR_BLACK : IColor(255, 190, 193, 202), "Inter-Bold", EAlign::Center, EVAlign::Middle);
        g.DrawText(chipText, chip.first.c_str(), cr);
        cx += w + 5.0f;
      }
      y += 24.0f;
    }

    // Full description
    if (!r.description.empty())
    {
      const IText descText(9.5f, IColor(255, 170, 173, 182), "Inter-Regular", EAlign::Near, EVAlign::Top);
      for (const auto& line : tonegallery::WrapLines(r.description, 84, 40))
      {
        g.DrawText(descText, line.c_str(), IRECT(area.L, y, area.R, y + 13.0f));
        y += 13.0f;
      }
      y += 10.0f;
    }

    // Variations / included capture files
    const IText headText(9.0f, IColor(255, 139, 142, 152), "Inter-Bold", EAlign::Near, EVAlign::Top);
    auto ms = mDetailModels;
    if (ms != nullptr)
    {
      const int status = ms->status.load();
      if (status == 1)
      {
        mSpinnerPhase += 10.0f;
        if (mSpinnerPhase > 360.0f)
          mSpinnerPhase -= 360.0f;
        g.DrawArc(accent, area.L + 6.0f, y + 6.0f, 5.0f, mSpinnerPhase, mSpinnerPhase + 270.0f, nullptr, 1.8f);
        g.DrawText(headText, "LOADING VARIATIONS...", IRECT(area.L + 16.0f, y, area.R, y + 12.0f));
        y += 18.0f;
      }
      else if (status == 3)
      {
        g.DrawText(headText, "COULDN'T LOAD THE FILE LIST", IRECT(area.L, y, area.R, y + 12.0f));
        y += 18.0f;
      }
      else
      {
        std::vector<t3k::ModelFile> models;
        {
          std::lock_guard<std::mutex> lock(ms->mtx);
          models = ms->models;
        }
        // Same A2-preferred dedupe the download uses, so the list shows
        // exactly what will land in the library.
        std::map<std::string, t3k::ModelFile> byName;
        for (const auto& m : models)
        {
          auto found = byName.find(m.name);
          if (found == byName.end() || (m.arch == "2" && found->second.arch != "2"))
            byName[m.name] = m;
        }
        std::stringstream head;
        head << "VARIATIONS (" << byName.size() << ")";
        g.DrawText(headText, head.str().c_str(), IRECT(area.L, y, area.R, y + 12.0f));
        y += 16.0f;
        const IText rowText(9.5f, COLOR_WHITE, "Inter-Regular", EAlign::Near, EVAlign::Middle);
        for (const auto& pair : byName)
        {
          const IRECT row = IRECT(area.L, y, area.R, y + 18.0f);
          if (row.B >= area.T && row.T <= area.B)
          {
            g.FillRoundRect(IColor(255, 26, 27, 33), row, 5.0f);
            g.FillEllipse(accent, IRECT(row.L + 7.0f, row.MH() - 2.0f, row.L + 11.0f, row.MH() + 2.0f));
            g.DrawText(rowText, tonegallery::Ellipsize(pair.second.name, 70).c_str(),
                       row.GetReducedFromLeft(16.0f).GetReducedFromRight(34.0f));
            const bool isA2 = pair.second.arch == "2";
            const bool isWav = t3k::UrlExtension(pair.second.url, ".nam") == ".wav";
            const IRECT arcChip = row.GetFromRight(28.0f).GetCentredInside(24.0f, 11.0f);
            g.FillRoundRect(isA2 ? accent.WithOpacity(0.25f) : IColor(255, 55, 57, 66), arcChip, 4.0f);
            const IText arcText(
              6.5f, isA2 ? accent : IColor(255, 150, 153, 162), "Inter-Bold", EAlign::Center, EVAlign::Middle);
            g.DrawText(arcText, isWav ? "IR" : (isA2 ? "A2" : "A1"), arcChip);
          }
          y += 21.0f;
        }
      }
    }
    y += 6.0f;

    mDetailContentH = (y + mDetailScroll) - area.T;

    // Scrollbar
    if (mDetailContentH > area.H())
    {
      const float frac = area.H() / mDetailContentH;
      const float barH = std::max(18.0f, frac * area.H());
      const float travel = area.H() - barH;
      const float pos = (mDetailScroll / (mDetailContentH - area.H())) * travel;
      g.FillRoundRect(
        accent.WithOpacity(0.5f), IRECT(mRECT.R - 6.0f, area.T + pos, mRECT.R - 4.0f, area.T + pos + barH), 1.0f);
    }
    g.PathClipRegion();

    // Download button (fixed at the bottom)
    const IRECT btn = DownloadButtonRect();
    auto found = mDownloads.find(r.id);
    const int dlStatus = found != mDownloads.end() ? found->second->status.load() : 0;
    if (dlStatus == 1)
    {
      g.FillRoundRect(accent.WithOpacity(0.25f), btn, btn.H() * 0.5f);
      mSpinnerPhase += 6.0f;
      g.DrawArc(accent, btn.L + 16.0f, btn.MH(), 7.0f, mSpinnerPhase, mSpinnerPhase + 270.0f, nullptr, 2.0f);
      const IText btnText(10.0f, accent, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(btnText, "DOWNLOADING...", btn);
    }
    else if (dlStatus == 2)
    {
      g.FillRoundRect(IColor(255, 88, 214, 141), btn, btn.H() * 0.5f);
      const IText btnText(10.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(btnText, "ADDED TO LIBRARY", btn);
    }
    else if (dlStatus == 3)
    {
      g.FillRoundRect(IColor(255, 232, 90, 90), btn, btn.H() * 0.5f);
      const IText btnText(10.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(btnText, "FAILED - CLICK TO RETRY", btn);
    }
    else
    {
      g.FillRoundRect(mMouseOverDL ? accent : accent.WithOpacity(0.85f), btn, btn.H() * 0.5f);
      if (mMouseOverDL)
        g.DrawRoundRect(accent.WithOpacity(0.35f), btn.GetPadded(2.0f), btn.H() * 0.5f + 2.0f, nullptr, 2.5f);
      const IText btnText(10.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(btnText, "DOWNLOAD TONE", btn);
    }
  }

  float CardWidth() const { return (GridRect().W() - (kNumCols - 1) * kCardGap - 8.0f) / (float)kNumCols; }

  IRECT CardRect(int i) const
  {
    const IRECT grid = GridRect();
    const int col = i % kNumCols;
    const int row = i / kNumCols;
    const float w = CardWidth();
    const float x = grid.L + col * (w + kCardGap);
    const float y = grid.T + row * (kCardHeight + kCardGap) - mScroll;
    return IRECT(x, y, x + w, y + kCardHeight);
  }

  IRECT LoadMoreRect(int numResults) const
  {
    const IRECT grid = GridRect();
    const int numRows = (numResults + kNumCols - 1) / kNumCols;
    const float y = grid.T + numRows * (kCardHeight + kCardGap) - mScroll + 2.0f;
    return IRECT(grid.MW() - 70.0f, y, grid.MW() + 70.0f, y + 26.0f);
  }

  float ContentHeight() const
  {
    size_t n;
    long long total;
    {
      std::lock_guard<std::mutex> lock(mShared->mtx);
      n = mShared->results.size();
      total = mShared->total;
    }
    const int numRows = ((int)n + kNumCols - 1) / kNumCols;
    float h = numRows * (kCardHeight + kCardGap);
    if ((long long)n < total)
      h += 34.0f;
    return h;
  }

  int CardAt(float x, float y) const
  {
    // NOTE: caller holds the lock when needed; geometry only here.
    const IRECT grid = GridRect();
    if (!grid.Contains(x, y))
      return -1;
    const int col = (int)((x - grid.L) / (CardWidth() + kCardGap));
    const int row = (int)((y - grid.T + mScroll) / (kCardHeight + kCardGap));
    if (col < 0 || col >= kNumCols || row < 0)
      return -1;
    const int idx = row * kNumCols + col;
    return CardRect(idx).Contains(x, y) ? idx : -1;
  }

  std::filesystem::path ThumbPath(const t3k::SearchResult& r) const
  {
    return tonegallery::GetToneLibraryRoot() / ".cache"
           / tonegallery::UTF8ToPath("thumb_" + std::to_string(r.id) + t3k::UrlExtension(r.imageUrl, ".jpg"));
  }

  IBitmap* GetThumb(const t3k::SearchResult& r)
  {
    if (r.imageUrl.empty())
      return nullptr;
    auto found = mThumbCache.find(r.id);
    if (found != mThumbCache.end())
      return found->second.GetAPIBitmap() ? &found->second : nullptr;
    const std::filesystem::path path = ThumbPath(r);
    try
    {
      if (std::filesystem::exists(path))
      {
        IBitmap bitmap = GetUI()->LoadBitmap(tonegallery::PathToUTF8(path).c_str());
        auto inserted = mThumbCache.insert({r.id, bitmap});
        return inserted.first->second.GetAPIBitmap() ? &inserted.first->second : nullptr;
      }
    }
    catch (const std::exception&)
    {
    }
    if (mThumbRequested.find(r.id) == mThumbRequested.end())
    {
      mThumbRequested.insert(r.id);
      const std::string url = r.imageUrl;
      const std::filesystem::path dest = path;
      t3k::Worker::Get().Enqueue([url, dest]() {
        try
        {
          std::filesystem::create_directories(dest.parent_path());
          t3k::DownloadUrlToFile(url, dest);
        }
        catch (const std::exception&)
        {
        }
      });
    }
    return nullptr;
  }

  void DrawCard(IGraphics& g, const t3k::SearchResult& r, const IRECT& card, bool mouseOver, const IColor& accent,
                bool& anyDownloading)
  {
    g.FillRoundRect(IColor(255, 32, 33, 41), card, 8.0f);

    // Photo
    const IRECT photo = card.GetFromTop(kPhotoHeight).GetPadded(-1.5f);
    IBitmap* pBitmap = GetThumb(r);
    // NOTE: photo.Intersect(grid) is EMPTY when the photo part of the card is
    // scrolled fully out of view, and an empty clip rect means "no clipping"
    // -- which would splatter the full-size cover image over the whole UI.
    // Only draw the bitmap when the photo is actually (partly) visible.
    const IRECT photoVis = photo.Intersect(GridRect());
    if (pBitmap != nullptr && pBitmap->W() > 0 && pBitmap->H() > 0 && photoVis.W() > 0.5f && photoVis.H() > 0.5f)
    {
      const float bmpAspect = (float)pBitmap->W() / (float)pBitmap->H();
      const float areaAspect = photo.W() / photo.H();
      IRECT cover = photo;
      if (bmpAspect > areaAspect)
        cover = photo.GetMidHPadded(0.5f * photo.H() * bmpAspect);
      else
        cover = photo.GetMidVPadded(0.5f * photo.W() / bmpAspect);
      g.PathClipRegion(photoVis);
      g.DrawFittedBitmap(*pBitmap, cover);
      g.PathClipRegion(GridRect());
    }
    else
    {
      g.FillRoundRect(accent.WithOpacity(0.08f), photo, 6.0f);
      if (!r.imageUrl.empty())
      {
        anyDownloading = true; // keep polling until the thumb arrives
      }
    }

    // Title (2 lines) + author
    const IRECT body = card.GetReducedFromTop(kPhotoHeight).GetPadded(-6.0f);
    const IText nameText(8.5f, COLOR_WHITE, "Inter-Bold", EAlign::Near, EVAlign::Top);
    std::string l1, l2;
    {
      const auto lines = tonegallery::WrapLines(r.title, 26, 2);
      l1 = lines.size() > 0 ? lines[0] : "";
      l2 = lines.size() > 1 ? lines[1] : "";
    }
    g.DrawText(nameText, l1.c_str(), body.GetFromTop(10.0f));
    if (!l2.empty())
      g.DrawText(nameText, l2.c_str(), body.GetReducedFromTop(10.0f).GetFromTop(10.0f));

    const IText metaText(7.5f, IColor(255, 139, 142, 152), "Inter-Regular", EAlign::Near, EVAlign::Middle);
    std::stringstream meta;
    meta << "by " << r.author;
    if (r.downloads > 0)
      meta << "  -  " << r.downloads << " dl";
    g.DrawText(metaText, tonegallery::Ellipsize(meta.str(), 30).c_str(), body.GetFromBottom(20.0f).GetFromTop(10.0f));

    // Gear chip
    const std::string gearType = t3k::GearToType(r.gear);
    const tonegallery::GearType gt = tonegallery::GearTypeFromString(gearType);
    const IColor gearColor = tonegallery::GearTypeColor(gt);
    const char* gearLabel = tonegallery::GearTypeChipLabel(gt);
    const float gw = 10.0f + 4.6f * (float)strlen(gearLabel);
    const IRECT chip = body.GetFromBottom(11.0f).GetFromLeft(gw);
    g.FillRoundRect(gearColor, chip, 4.0f);
    const IText chipText(6.5f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
    g.DrawText(chipText, gearLabel, chip);

    // Download state overlay
    auto found = mDownloads.find(r.id);
    const int dlStatus = found != mDownloads.end() ? found->second->status.load() : 0;
    if (dlStatus == 1)
    {
      anyDownloading = true;
      g.FillRoundRect(COLOR_BLACK.WithOpacity(0.55f), card, 8.0f);
      mSpinnerPhase += 4.0f;
      g.DrawArc(accent, card.MW(), card.MH() - 8.0f, 10.0f, mSpinnerPhase, mSpinnerPhase + 270.0f, nullptr, 2.5f);
      const IText dlText(9.0f, COLOR_WHITE, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(dlText, "DOWNLOADING...", card.GetFromBottom(30.0f));
    }
    else if (dlStatus == 2)
    {
      const IRECT badge = card.GetFromTRHC(58.0f, 20.0f).GetPadded(-2.0f);
      g.FillRoundRect(IColor(255, 88, 214, 141), badge, 9.0f);
      const IText okText(8.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(okText, "ADDED", badge);
    }
    else if (dlStatus == 3)
    {
      const IRECT badge = card.GetFromTRHC(58.0f, 20.0f).GetPadded(-2.0f);
      g.FillRoundRect(IColor(255, 232, 90, 90), badge, 9.0f);
      const IText okText(8.0f, COLOR_BLACK, "Inter-Bold", EAlign::Center, EVAlign::Middle);
      g.DrawText(okText, "FAILED", badge);
    }

    // Border
    if (mouseOver && dlStatus == 0)
    {
      g.DrawRoundRect(accent.WithOpacity(0.25f), card.GetPadded(1.0f), 9.0f, nullptr, 3.0f);
      g.DrawRoundRect(accent, card, 8.0f, nullptr, 1.2f);
    }
    else
    {
      g.DrawRoundRect(IColor(18, 255, 255, 255), card, 8.0f);
    }
  }

  std::shared_ptr<t3k::BrowserShared> mShared;
  std::map<long long, std::shared_ptr<t3k::DownloadShared>> mDownloads;
  std::map<long long, IBitmap> mThumbCache;
  std::set<long long> mThumbRequested;
  bool mHasSearched = false;
  float mScroll = 0.0f;
  float mSpinnerPhase = 0.0f;
  int mGearSel = 0;
  int mSortSel = 0;
  int mFormatSel = 0; // 0 = NAM captures, 1 = impulse responses
  std::set<std::string> mSelectedTags;
  bool mTagsOpen = false;
  float mTagScroll = 0.0f;
  float mTagContentH = 0.0f;
  std::shared_ptr<t3k::TagsShared> mTagsShared;
  std::string mHoverTag;
  bool mDetailOpen = false;
  t3k::SearchResult mDetailTone;
  std::shared_ptr<t3k::ModelsShared> mDetailModels;
  float mDetailScroll = 0.0f;
  float mDetailContentH = 0.0f;
  bool mMouseOverClose = false;
  bool mMouseOverField = false;
  bool mMouseOverMore = false;
  bool mMouseOverBack = false;
  bool mMouseOverDL = false;
  int mMouseOverGearChip = -1;
  int mMouseOverSortChip = -1;
  int mMouseOverFormatChip = -1;
  bool mMouseOverTagsBtn = false;
  bool mMouseOverClear = false;
  int mMouseOverCard = -1;
  IFileDialogCompletionHandlerFunc mLoadModelFunc;
  IFileDialogCompletionHandlerFunc mLoadIRFunc;
};

// The TONE3000 button (top-left of the main panel): a proper two-line button
// that opens the live search browser.
class NAMT3KButtonControl : public IControl
{
public:
  NAMT3KButtonControl(const IRECT& bounds)
  : IControl(bounds)
  {
    SetTooltip("Search tone3000.com and download tones");
  }

  void Draw(IGraphics& g) override
  {
    const IColor accent = tonegallery::AccentColor();

    // AMPRYX: slim square button, near-black fill, thin gold border.
    g.FillRect(IColor(255, 15, 13, 8), mRECT);
    if (mMouseIsOver)
      g.FillRect(accent.WithOpacity(0.10f), mRECT);
    g.DrawRect(mMouseIsOver ? accent : accent.WithOpacity(0.55f), mRECT.GetPadded(-0.5f), nullptr, 1.0f);

    // Globe icon on the left
    const IRECT globe = mRECT.GetFromLeft(26.0f).GetCentredInside(12.0f);
    g.DrawEllipse(accent, globe, nullptr, 1.2f);
    g.DrawEllipse(accent, globe.GetMidHPadded(3.0f), nullptr, 0.9f);
    g.DrawLine(accent, globe.L, globe.MH(), globe.R, globe.MH(), nullptr, 0.9f);

    // Single-line label: TONE3000 (JetBrains Mono, per the reference)
    const IRECT textArea = mRECT.GetReducedFromLeft(26.0f).GetReducedFromRight(6.0f);
    const IText brandText(9.5f, IColor(255, 236, 230, 212), "JetBrainsMono-Bold", EAlign::Near, EVAlign::Middle);
    g.DrawText(brandText, "TONE3000", textArea);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (IControl* pBrowser = GetUI()->GetControlWithTag(kCtrlTagTone3000))
      pBrowser->As<NAMTone3000BrowserControl>()->Show();
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    SetDirty(false);
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    SetDirty(false);
  }
};
