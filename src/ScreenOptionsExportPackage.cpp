#include "ScreenOptionsExportPackage.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "LuaBinding.h"
#include "MessageManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageZipWriter.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenMiniMenu.h"
#include "ScreenTextEntry.h"
#include "ScreenWithMenuElements.h"
#include "SongManager.h"
#include "SpecialFiles.h"

REGISTER_SCREEN_CLASS(ScreenOptionsExportPackage);

static AutoScreenMessage(SM_ExportPackageInfoEdited);
static AutoScreenMessage(SM_BackFromEditInfoMenu);

// ===========================================================================
// ExportPackages module: state, listing helpers, zip job.
// ===========================================================================
namespace ExportPackages {

static std::vector<std::string> g_vsSelected;
static Metadata g_Metadata;

// ---------------------------------------------------------------------------
// Threaded export state.
//
// The export does file I/O + zip compression, which would freeze the main
// render loop for many seconds if run synchronously.  We push the heavy
// work onto a worker thread; the main thread polls progress in Update()
// and broadcasts ExportPackageProgress at most once per frame, which keeps
// the UI responsive and prevents the engine's "Tween overflow" warning
// caused by hundreds of stacked broadcasts firing in a single frame.
//
// Thread-safety contract:
//   - The worker only touches the atomics below + g_sWorkerCurrentFile
//     (under g_ExportMutex).  It never touches Lua, broadcasts, or other
//     engine globals.
//   - The main thread snapshots progress in Update() and updates the
//     Lua-visible counters g_iProgressCurrent / g_iProgressTotal /
//     g_sProgressFile.
// ---------------------------------------------------------------------------
static std::atomic<bool> g_bExporting{false};
static std::atomic<bool> g_bWorkerDirty{false};
static std::atomic<bool> g_bWorkerDone{false};
static std::atomic<int> g_iWorkerCur{0};
static std::atomic<int> g_iWorkerTotal{0};
static std::mutex g_ExportMutex;
static std::string g_sWorkerCurrentFile;  // guarded by g_ExportMutex
static std::string g_sWorkerError;        // guarded by g_ExportMutex
static std::unique_ptr<std::thread> g_pExportThread;

// Lua-visible counters (main-thread only, written by Update()).
static int g_iProgressCurrent = 0;
static int g_iProgressTotal = 0;
static std::string g_sProgressFile;

// Final result snapshot for the theme to display after
// ExportPackageFinished fires.
static std::string g_sLastExportPath;
static std::string g_sLastExportError;

static void Broadcast(const char* sMsg) {
  if (MESSAGEMAN != nullptr) {
    MESSAGEMAN->Broadcast(sMsg);
  }
}

std::vector<std::string>& GetSelected() { return g_vsSelected; }

bool IsSelected(const std::string& sPath) {
  return std::find(g_vsSelected.begin(), g_vsSelected.end(), sPath) !=
         g_vsSelected.end();
}

void ToggleSelected(const std::string& sPath) {
  auto it = std::find(g_vsSelected.begin(), g_vsSelected.end(), sPath);
  if (it == g_vsSelected.end()) {
    g_vsSelected.push_back(sPath);
  } else {
    g_vsSelected.erase(it);
  }
  Broadcast("ExportPackageSelectionChanged");
}

void ClearSelected() {
  g_vsSelected.clear();
  Broadcast("ExportPackageSelectionChanged");
}

Metadata& GetMetadata() { return g_Metadata; }

void SetMetadataField(const std::string& sKey, const std::string& sValue) {
  if (sKey == "Name") {
    g_Metadata.sName = sValue;
  } else if (sKey == "Author") {
    g_Metadata.sAuthor = sValue;
  } else if (sKey == "Description") {
    g_Metadata.sDescription = sValue;
  } else if (sKey == "Version") {
    g_Metadata.sVersion = sValue;
  } else if (sKey == "Homepage") {
    g_Metadata.sHomepage = sValue;
  }
  Broadcast("ExportPackageMetadataChanged");
}

void ResetMetadata() {
  g_Metadata = Metadata();
  g_Metadata.sName = "ITGmania Export";
  g_Metadata.sVersion = "1.0";
  Broadcast("ExportPackageMetadataChanged");
}

bool IsExporting() { return g_bExporting.load(); }
int GetProgressCurrent() { return g_iProgressCurrent; }
int GetProgressTotal() { return g_iProgressTotal; }
std::string GetProgressFile() { return g_sProgressFile; }
std::string GetLastExportPath() { return g_sLastExportPath; }
std::string GetLastExportError() { return g_sLastExportError; }

// ---------------------------------------------------------------------------
// Listing helpers (used by the Lua API).
// ---------------------------------------------------------------------------

std::vector<std::string> GetAvailableCategories() {
  return std::vector<std::string>{"Themes",      "NoteSkins",  "Songs",
                                  "Courses",     "Characters", "Announcers",
                                  "BGAnimations"};
}

static void ListDirSimple(
    const std::string& sGlob, std::vector<std::string>& out) {
  GetDirListing(sGlob, out, true, true);
  StripCvsAndSvn(out);
  StripMacResourceForks(out);
}

static void ListThemes(std::vector<std::string>& out) {
  ListDirSimple(SpecialFiles::THEMES_DIR + "*", out);
}
static void ListCourses(std::vector<std::string>& out) {
  ListDirSimple(SpecialFiles::COURSES_DIR + "*", out);
}
static void ListAnnouncers(std::vector<std::string>& out) {
  ListDirSimple("Announcers/*", out);
}
static void ListCharacters(std::vector<std::string>& out) {
  ListDirSimple("Characters/*", out);
}
static void ListBGAnimations(std::vector<std::string>& out) {
  ListDirSimple("BGAnimations/*", out);
}

static void ListNoteSkins(std::vector<std::string>& out) {
  std::vector<std::string> vsGames;
  ListDirSimple("NoteSkins/*", vsGames);
  for (const std::string& g : vsGames) {
    std::vector<std::string> vsSkins;
    ListDirSimple(g + "/*", vsSkins);
    for (const std::string& s : vsSkins) {
      out.push_back(s);
    }
  }
}

std::vector<std::string> GetSongGroups() {
  std::vector<std::string> vsGroups, out;
  if (SONGMAN != nullptr) {
    SONGMAN->GetSongGroupNames(vsGroups);
  }
  for (const std::string& g : vsGroups) {
    out.push_back(SpecialFiles::SONGS_DIR + g);
  }
  return out;
}

std::vector<std::string> GetSongsInGroup(const std::string& sGroupPath) {
  std::vector<std::string> out;
  ListDirSimple(sGroupPath + "/*", out);
  return out;
}

std::vector<std::string> GetItemsInCategory(const std::string& sCategory) {
  std::vector<std::string> out;
  if (sCategory == "Themes") {
    ListThemes(out);
  } else if (sCategory == "NoteSkins") {
    ListNoteSkins(out);
  } else if (sCategory == "Courses") {
    ListCourses(out);
  } else if (sCategory == "Songs") {
    out = GetSongGroups();
  } else if (sCategory == "Announcers") {
    ListAnnouncers(out);
  } else if (sCategory == "Characters") {
    ListCharacters(out);
  } else if (sCategory == "BGAnimations") {
    ListBGAnimations(out);
  }
  return out;
}

void SelectSongGroup(const std::string& sGroupPath) {
  for (const std::string& s : GetSongsInGroup(sGroupPath)) {
    if (!IsSelected(s)) {
      g_vsSelected.push_back(s);
    }
  }
  Broadcast("ExportPackageSelectionChanged");
}

void DeselectSongGroup(const std::string& sGroupPath) {
  const std::vector<std::string> vs = GetSongsInGroup(sGroupPath);
  for (const std::string& s : vs) {
    auto it = std::find(g_vsSelected.begin(), g_vsSelected.end(), s);
    if (it != g_vsSelected.end()) {
      g_vsSelected.erase(it);
    }
  }
  Broadcast("ExportPackageSelectionChanged");
}

bool IsSongGroupFullySelected(const std::string& sGroupPath) {
  const std::vector<std::string> vs = GetSongsInGroup(sGroupPath);
  if (vs.empty()) {
    return false;
  }
  for (const std::string& s : vs) {
    if (!IsSelected(s)) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Zip helpers.
// ---------------------------------------------------------------------------

static std::string TimestampString() {
  std::time_t t = std::time(nullptr);
  std::tm tm_local;
#if defined(_WIN32)
  localtime_s(&tm_local, &t);
#else
  localtime_r(&t, &tm_local);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tm_local);
  return buf;
}

static std::string BuildManifestIni(
    const std::vector<std::string>& vsDirsToExport) {
  const Metadata& md = GetMetadata();
  std::string out;
  out += "[Package]\n";
  out += "Name=" + md.sName + "\n";
  out += "Author=" + md.sAuthor + "\n";
  out += "Description=" + md.sDescription + "\n";
  out += "Version=" + md.sVersion + "\n";
  out += "Homepage=" + md.sHomepage + "\n";
  out += "ExportedAt=" + TimestampString() + "\n";
  out += "\n";
  out += "[Contents]\n";
  for (std::size_t i = 0; i < vsDirsToExport.size(); ++i) {
    out +=
        ssprintf("Item%d=", static_cast<int>(i + 1)) + vsDirsToExport[i] + "\n";
  }
  return out;
}

// Worker-thread progress setter.  Safe to call from any thread; the main
// thread will pick up the snapshot in Update() and broadcast at most once
// per frame.
static void WorkerSetProgress(int iCur, int iTotal, const std::string& sFile) {
  g_iWorkerCur.store(iCur, std::memory_order_relaxed);
  g_iWorkerTotal.store(iTotal, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(g_ExportMutex);
    g_sWorkerCurrentFile = sFile;
  }
  g_bWorkerDirty.store(true, std::memory_order_release);
}

// Payload handed to the worker thread.  Everything the worker needs is
// snapshotted here on the main thread before launch so the worker never
// touches shared engine state.
struct ExportJob {
  std::vector<std::string> vsFiles;  // absolute VFS paths to add
  std::string sManifestIni;          // full Manifest.ini contents
  std::string sOutPath;              // destination .smzip
};

static void ExportWorkerMain(std::shared_ptr<ExportJob> pJob) {
  RageZipWriter writer;
  if (!writer.Open(pJob->sOutPath)) {
    {
      std::lock_guard<std::mutex> lk(g_ExportMutex);
      g_sWorkerError = writer.GetError();
    }
    g_bWorkerDone.store(true, std::memory_order_release);
    return;
  }

  const int iTotal = static_cast<int>(pJob->vsFiles.size()) + 1;
  WorkerSetProgress(0, iTotal, "Manifest.ini");

  if (!writer.AddFileFromMemory("Manifest.ini", pJob->sManifestIni)) {
    {
      std::lock_guard<std::mutex> lk(g_ExportMutex);
      g_sWorkerError = writer.GetError();
    }
    writer.Close();
    FILEMAN->Remove(pJob->sOutPath);
    g_bWorkerDone.store(true, std::memory_order_release);
    return;
  }
  WorkerSetProgress(1, iTotal, "Manifest.ini");

  int iIdx = 1;
  for (const std::string& s : pJob->vsFiles) {
    std::string sInternal = s;
    while (!sInternal.empty() && sInternal.front() == '/') {
      sInternal.erase(sInternal.begin());
    }
    WorkerSetProgress(iIdx, iTotal, sInternal);
    if (!writer.AddFile(s, sInternal)) {
      {
        std::lock_guard<std::mutex> lk(g_ExportMutex);
        g_sWorkerError = ssprintf(
            "Failed adding '%s': %s", s.c_str(), writer.GetError().c_str());
      }
      writer.Close();
      FILEMAN->Remove(pJob->sOutPath);
      g_bWorkerDone.store(true, std::memory_order_release);
      return;
    }
    ++iIdx;
  }
  WorkerSetProgress(iTotal, iTotal, "");

  if (!writer.Close()) {
    {
      std::lock_guard<std::mutex> lk(g_ExportMutex);
      g_sWorkerError = writer.GetError();
    }
    FILEMAN->Remove(pJob->sOutPath);
    g_bWorkerDone.store(true, std::memory_order_release);
    return;
  }

  g_bWorkerDone.store(true, std::memory_order_release);
}

bool StartExport(std::string& sErrorOut) {
  if (g_bExporting.load()) {
    sErrorOut = "An export is already in progress.";
    return false;
  }
  if (g_vsSelected.empty()) {
    sErrorOut = "No items are selected.";
    return false;
  }

  // Enumerate every file under each selected directory on the main thread
  // so the worker has a flat list and never touches the selection vector.
  // Anything inside a ".git" directory (VCS metadata) is excluded.
  auto pJob = std::make_shared<ExportJob>();
  std::unordered_set<std::string> sAddedFiles;
  auto AddFileIfNeeded = [&](const std::string& sPath) {
    if (sAddedFiles.insert(sPath).second) {
      pJob->vsFiles.push_back(sPath);
    }
  };
  auto IsInsideDotGit = [](const std::string& sPath) {
    // Match "/.git/" anywhere in the path, plus a leading ".git/" prefix.
    if (sPath.find("/.git/") != std::string::npos) {
      return true;
    }
    if (sPath.size() >= 5 && sPath.compare(0, 5, ".git/") == 0) {
      return true;
    }
    return false;
  };
  for (const std::string& d : g_vsSelected) {
    std::string sDir = d;
    while (!sDir.empty() && sDir.front() == '/') {
      sDir.erase(sDir.begin());
    }
    if (sDir.empty() || sDir.back() != '/') {
      sDir += "/";
    }

    // Song selections are stored at the song-folder level. Include the loose
    // files from the containing pack root as well so banners, pack.ini, etc.
    // travel with the exported pack even when only individual songs are chosen.
    if (sDir.rfind("Songs/", 0) == 0) {
      const std::size_t iSongSep = sDir.find('/', 6);
      if (iSongSep != std::string::npos) {
        const std::string sPackRoot = sDir.substr(0, iSongSep);
        std::vector<std::string> vsRootFiles;
        GetDirListing(sPackRoot + "/*", vsRootFiles, false, true);
        for (const std::string& sRootFile : vsRootFiles) {
          if (IsADirectory(sRootFile) || IsInsideDotGit(sRootFile)) {
            continue;
          }
          AddFileIfNeeded(sRootFile);
        }
      }
    }

    std::vector<std::string> vsTmp;
    GetDirListingRecursive(sDir, "*", vsTmp);
    for (const std::string& f : vsTmp) {
      if (IsInsideDotGit(f)) {
        continue;
      }
      AddFileIfNeeded(f);
    }
  }
  if (pJob->vsFiles.empty()) {
    sErrorOut = "No files found in the selected items.";
    return false;
  }

  const std::string sExportDir = "Save/Exports/";
  FILEMAN->CreateDir(sExportDir);
  std::string sBase = g_Metadata.sName.empty() ? std::string("ITGmania-Export")
                                               : g_Metadata.sName;
  // Replace path-unsafe chars in the package name.
  for (char& c : sBase) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|') {
      c = '_';
    }
  }
  pJob->sOutPath = sExportDir + sBase + "_" + TimestampString() + ".smzip";
  pJob->sManifestIni = BuildManifestIni(g_vsSelected);

  if (LOG) {
    LOG->Trace(
        "Exporting %d files from %d items to '%s'", (int)pJob->vsFiles.size(),
        (int)g_vsSelected.size(), pJob->sOutPath.c_str());
  }

  // Reset progress state.
  g_iProgressCurrent = 0;
  g_iProgressTotal = static_cast<int>(pJob->vsFiles.size()) + 1;
  g_sProgressFile = "Manifest.ini";
  g_iWorkerCur.store(0, std::memory_order_relaxed);
  g_iWorkerTotal.store(g_iProgressTotal, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(g_ExportMutex);
    g_sWorkerCurrentFile = "Manifest.ini";
    g_sWorkerError.clear();
  }
  g_bWorkerDirty.store(false, std::memory_order_relaxed);
  g_bWorkerDone.store(false, std::memory_order_relaxed);
  g_sLastExportPath.clear();
  g_sLastExportError.clear();

  // Save the intended path so the main thread can publish it on finish.
  g_sLastExportPath = pJob->sOutPath;

  g_bExporting.store(true, std::memory_order_release);
  Broadcast("ExportPackageStarted");
  Broadcast("ExportPackageProgress");

  g_pExportThread.reset(new std::thread([pJob]() { ExportWorkerMain(pJob); }));
  return true;
}

void Update(float /* fDeltaTime */) {
  if (!g_bExporting.load(std::memory_order_acquire)) {
    return;
  }

  // Drain progress at most once per frame.
  if (g_bWorkerDirty.exchange(false, std::memory_order_acq_rel)) {
    g_iProgressCurrent = g_iWorkerCur.load(std::memory_order_relaxed);
    g_iProgressTotal = g_iWorkerTotal.load(std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lk(g_ExportMutex);
      g_sProgressFile = g_sWorkerCurrentFile;
    }
    Broadcast("ExportPackageProgress");
  }

  if (g_bWorkerDone.load(std::memory_order_acquire)) {
    if (g_pExportThread && g_pExportThread->joinable()) {
      g_pExportThread->join();
    }
    g_pExportThread.reset();

    {
      std::lock_guard<std::mutex> lk(g_ExportMutex);
      g_sLastExportError = g_sWorkerError;
    }
    if (!g_sLastExportError.empty()) {
      // On failure, clear the would-be path so the theme doesn't claim
      // success.
      g_sLastExportPath.clear();
    }
    g_bExporting.store(false, std::memory_order_release);
    Broadcast("ExportPackageFinished");
  }
}

}  // namespace ExportPackages

// ===========================================================================
// "Edit Package Info" ScreenMiniMenu plumbing.
// ===========================================================================
namespace {

enum EditInfoChoice {
  EIC_Name,
  EIC_Author,
  EIC_Description,
  EIC_Version,
  EIC_Homepage,
  NUM_EditInfoChoice
};

static MenuDef g_EditPackageInfoMenu(
    "ScreenMiniMenuEditPackageInfo",
    MenuRowDef(
        EIC_Name, "Name", true, EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        EIC_Author, "Author", true, EditMode_Practice, true, true, 0, nullptr),
    MenuRowDef(
        EIC_Description, "Description", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        EIC_Version, "Version", true, EditMode_Practice, true, true, 0,
        nullptr),
    MenuRowDef(
        EIC_Homepage, "Homepage", true, EditMode_Practice, true, true, 0,
        nullptr));

// Set when the user picks a mini-menu row; consumed by OnEditFieldOK.
static std::string g_sFieldKeyBeingEdited;

static std::string FieldKeyFromChoice(int iChoice) {
  switch (iChoice) {
    case EIC_Name:
      return "Name";
    case EIC_Author:
      return "Author";
    case EIC_Description:
      return "Description";
    case EIC_Version:
      return "Version";
    case EIC_Homepage:
      return "Homepage";
    default:
      return "";
  }
}

static std::string FieldValue(const std::string& sKey) {
  const auto& md = ExportPackages::GetMetadata();
  if (sKey == "Name") {
    return md.sName;
  }
  if (sKey == "Author") {
    return md.sAuthor;
  }
  if (sKey == "Description") {
    return md.sDescription;
  }
  if (sKey == "Version") {
    return md.sVersion;
  }
  if (sKey == "Homepage") {
    return md.sHomepage;
  }
  return "";
}

static int FieldMaxLen(const std::string& sKey) {
  if (sKey == "Description" || sKey == "Homepage") {
    return 256;
  }
  if (sKey == "Author") {
    return 64;
  }
  if (sKey == "Version") {
    return 32;
  }
  return 128;
}

static void OnEditFieldOK(const std::string& sAnswer) {
  if (!g_sFieldKeyBeingEdited.empty()) {
    ExportPackages::SetMetadataField(g_sFieldKeyBeingEdited, sAnswer);
    g_sFieldKeyBeingEdited.clear();
  }
  SCREENMAN->PostMessageToTopScreen(SM_ExportPackageInfoEdited, 0);
}

}  // namespace

namespace ExportPackages {
void OpenEditPackageInfoMenu() {
  ScreenMiniMenu::MiniMenu(
      &g_EditPackageInfoMenu, SM_BackFromEditInfoMenu, SM_None);
}
}  // namespace ExportPackages

// ===========================================================================
// ScreenOptionsExportPackage: thin host screen.
// ===========================================================================
void ScreenOptionsExportPackage::Init() { ScreenWithMenuElements::Init(); }

void ScreenOptionsExportPackage::BeginScreen() {
  if (ExportPackages::GetMetadata().sName.empty()) {
    ExportPackages::ResetMetadata();
  }
  ScreenWithMenuElements::BeginScreen();
}

void ScreenOptionsExportPackage::Update(float fDeltaTime) {
  ScreenWithMenuElements::Update(fDeltaTime);
  ExportPackages::Update(fDeltaTime);
}

void ScreenOptionsExportPackage::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_BackFromEditInfoMenu) {
    if (!ScreenMiniMenu::s_bCancelled) {
      const std::string sKey =
          FieldKeyFromChoice(ScreenMiniMenu::s_iLastRowCode);
      if (!sKey.empty()) {
        g_sFieldKeyBeingEdited = sKey;
        ScreenTextEntry::TextEntry(
            SM_None, "Edit " + sKey, FieldValue(sKey), FieldMaxLen(sKey),
            nullptr /* validate */, OnEditFieldOK, nullptr /* cancel cb */);
      }
    }
    return;
  }
  if (SM == SM_ExportPackageInfoEdited) {
    // Theme listens to the broadcast; nothing for the engine to do here.
    return;
  }
  ScreenWithMenuElements::HandleScreenMessage(SM);
}

// ===========================================================================
// Lua bindings.  Themes use ExportPackages.* to drive the whole UI.
// ===========================================================================
namespace {

static void PushStringVector(lua_State* L, const std::vector<std::string>& v) {
  lua_createtable(L, static_cast<int>(v.size()), 0);
  for (std::size_t i = 0; i < v.size(); ++i) {
    lua_pushstring(L, v[i].c_str());
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
}

int LuaGetAvailableCategories(lua_State* L) {
  PushStringVector(L, ExportPackages::GetAvailableCategories());
  return 1;
}

int LuaGetItemsInCategory(lua_State* L) {
  PushStringVector(L, ExportPackages::GetItemsInCategory(SArg(1)));
  return 1;
}

int LuaGetSongGroups(lua_State* L) {
  PushStringVector(L, ExportPackages::GetSongGroups());
  return 1;
}

int LuaGetSongsInGroup(lua_State* L) {
  PushStringVector(L, ExportPackages::GetSongsInGroup(SArg(1)));
  return 1;
}

int LuaGetSelected(lua_State* L) {
  PushStringVector(L, ExportPackages::GetSelected());
  return 1;
}

int LuaGetSelectedCount(lua_State* L) {
  lua_pushinteger(
      L, static_cast<lua_Integer>(ExportPackages::GetSelected().size()));
  return 1;
}

int LuaIsSelected(lua_State* L) {
  lua_pushboolean(L, ExportPackages::IsSelected(SArg(1)) ? 1 : 0);
  return 1;
}

int LuaToggleSelected(lua_State* L) {
  ExportPackages::ToggleSelected(SArg(1));
  return 0;
}

int LuaClearSelected(lua_State* /* L */) {
  ExportPackages::ClearSelected();
  return 0;
}

int LuaSelectSongGroup(lua_State* L) {
  ExportPackages::SelectSongGroup(SArg(1));
  return 0;
}

int LuaDeselectSongGroup(lua_State* L) {
  ExportPackages::DeselectSongGroup(SArg(1));
  return 0;
}

int LuaIsSongGroupFullySelected(lua_State* L) {
  lua_pushboolean(L, ExportPackages::IsSongGroupFullySelected(SArg(1)) ? 1 : 0);
  return 1;
}

int LuaIsExporting(lua_State* L) {
  lua_pushboolean(L, ExportPackages::IsExporting() ? 1 : 0);
  return 1;
}

int LuaGetProgressCurrent(lua_State* L) {
  lua_pushinteger(L, ExportPackages::GetProgressCurrent());
  return 1;
}

int LuaGetProgressTotal(lua_State* L) {
  lua_pushinteger(L, ExportPackages::GetProgressTotal());
  return 1;
}

int LuaGetProgressFraction(lua_State* L) {
  const int total = ExportPackages::GetProgressTotal();
  const float f =
      total > 0 ? static_cast<float>(ExportPackages::GetProgressCurrent()) /
                      static_cast<float>(total)
                : 0.0f;
  lua_pushnumber(L, f);
  return 1;
}

int LuaGetProgressFile(lua_State* L) {
  lua_pushstring(L, ExportPackages::GetProgressFile().c_str());
  return 1;
}

int LuaGetLastExportPath(lua_State* L) {
  lua_pushstring(L, ExportPackages::GetLastExportPath().c_str());
  return 1;
}

int LuaGetLastExportError(lua_State* L) {
  lua_pushstring(L, ExportPackages::GetLastExportError().c_str());
  return 1;
}

int LuaGetMetadata(lua_State* L) {
  const auto& md = ExportPackages::GetMetadata();
  lua_newtable(L);
  lua_pushstring(L, md.sName.c_str());
  lua_setfield(L, -2, "Name");
  lua_pushstring(L, md.sAuthor.c_str());
  lua_setfield(L, -2, "Author");
  lua_pushstring(L, md.sDescription.c_str());
  lua_setfield(L, -2, "Description");
  lua_pushstring(L, md.sVersion.c_str());
  lua_setfield(L, -2, "Version");
  lua_pushstring(L, md.sHomepage.c_str());
  lua_setfield(L, -2, "Homepage");
  return 1;
}

int LuaSetMetadata(lua_State* L) {
  ExportPackages::SetMetadataField(SArg(1), SArg(2));
  return 0;
}

int LuaResetMetadata(lua_State* /* L */) {
  ExportPackages::ResetMetadata();
  return 0;
}

int LuaOpenEditPackageInfoMenu(lua_State* /* L */) {
  ExportPackages::OpenEditPackageInfoMenu();
  return 0;
}

// Kicks off the export on a background thread.  Returns (true, nil) if
// the worker started, or (nil, errorString) on setup failure.  Themes
// listen for ExportPackageFinished and then call GetLastExportPath /
// GetLastExportError to show a success / failure message.
int LuaStartExport(lua_State* L) {
  std::string sErr;
  const bool bOk = ExportPackages::StartExport(sErr);
  if (!bOk) {
    lua_pushnil(L);
    lua_pushstring(L, sErr.c_str());
  } else {
    lua_pushboolean(L, 1);
    lua_pushnil(L);
  }
  return 2;
}

const luaL_Reg ExportPackagesTable[] = {
    {"GetAvailableCategories", LuaGetAvailableCategories},
    {"GetItemsInCategory", LuaGetItemsInCategory},
    {"GetSongGroups", LuaGetSongGroups},
    {"GetSongsInGroup", LuaGetSongsInGroup},
    {"GetSelected", LuaGetSelected},
    {"GetSelectedCount", LuaGetSelectedCount},
    {"IsSelected", LuaIsSelected},
    {"ToggleSelected", LuaToggleSelected},
    {"ClearSelected", LuaClearSelected},
    {"SelectSongGroup", LuaSelectSongGroup},
    {"DeselectSongGroup", LuaDeselectSongGroup},
    {"IsSongGroupFullySelected", LuaIsSongGroupFullySelected},
    {"IsExporting", LuaIsExporting},
    {"GetProgressCurrent", LuaGetProgressCurrent},
    {"GetProgressTotal", LuaGetProgressTotal},
    {"GetProgressFraction", LuaGetProgressFraction},
    {"GetProgressFile", LuaGetProgressFile},
    {"GetLastExportPath", LuaGetLastExportPath},
    {"GetLastExportError", LuaGetLastExportError},
    {"GetMetadata", LuaGetMetadata},
    {"SetMetadata", LuaSetMetadata},
    {"ResetMetadata", LuaResetMetadata},
    {"OpenEditPackageInfoMenu", LuaOpenEditPackageInfoMenu},
    {"StartExport", LuaStartExport},
    {nullptr, nullptr},
};

void RegisterExportPackagesLua(lua_State* L) {
  luaL_register(L, "ExportPackages", ExportPackagesTable);
  lua_pop(L, 1);
}
REGISTER_WITH_LUA_FUNCTION(RegisterExportPackagesLua);

}  // namespace

/*
 * (c) 2002-2014 Chris Danford, AJ Kelly, Renaud Lepage
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
