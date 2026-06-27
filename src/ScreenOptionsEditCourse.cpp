#include "ScreenOptionsEditCourse.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "Attack.h"
#include "CommonMetrics.h"
#include "Course.h"
#include "CourseUtil.h"
#include "Difficulty.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "MessageManager.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "Song.h"
#include "SongManager.h"
#include "SongUtil.h"
#include "StepsUtil.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"

REGISTER_SCREEN_CLASS(ScreenOptionsEditCourse);

namespace EditCourse {

// ===========================================================================
// State
// ===========================================================================
static bool g_bDirty = false;
static int g_iSelectedEntryIndex = 0;  // 1-based; 0 = none
static std::string g_sPlayScreen;
static std::string g_sPrevScreen;

static Course* Cur() { return GAMESTATE->m_pCurCourse; }
static const CourseEntry* CurEntryConst(int i1) {
  Course* p = Cur();
  if (p == nullptr || i1 < 1 || i1 > static_cast<int>(p->m_vEntries.size())) {
    return nullptr;
  }
  return &p->m_vEntries[i1 - 1];
}
static CourseEntry* CurEntry(int i1) {
  Course* p = Cur();
  if (p == nullptr || i1 < 1 || i1 > static_cast<int>(p->m_vEntries.size())) {
    return nullptr;
  }
  return &p->m_vEntries[i1 - 1];
}

static void Broadcast(const char* sMsg) {
  if (MESSAGEMAN != nullptr) {
    MESSAGEMAN->Broadcast(sMsg);
  }
}

static void InvalidateTrails() {
  Course* p = Cur();
  if (p != nullptr) {
    p->InvalidateTrailCache();
    EditCourseUtil::UpdateAndSetTrail();
  }
}

static void MarkDirtyAndBroadcastProperty() {
  bool bWas = g_bDirty;
  g_bDirty = true;
  Broadcast("EditCoursePropertyChanged");
  if (!bWas) {
    Broadcast("EditCourseDirtyChanged");
  }
}

static void MarkDirtyAndBroadcastEntry() {
  bool bWas = g_bDirty;
  g_bDirty = true;
  InvalidateTrails();
  Broadcast("EditCourseEntryChanged");
  if (!bWas) {
    Broadcast("EditCourseDirtyChanged");
  }
}

static void MarkDirtyAndBroadcastEntries() {
  bool bWas = g_bDirty;
  g_bDirty = true;
  InvalidateTrails();
  Broadcast("EditCourseEntriesChanged");
  if (!bWas) {
    Broadcast("EditCourseDirtyChanged");
  }
}

// ===========================================================================
// Course-level properties
// ===========================================================================
bool HasCourse() { return Cur() != nullptr; }

std::string GetTitle() {
  Course* p = Cur();
  return p != nullptr ? p->m_sMainTitle : std::string();
}
void SetTitle(const std::string& s) {
  Course* p = Cur();
  if (p == nullptr || p->m_sMainTitle == s) {
    return;
  }
  p->m_sMainTitle = s;
  MarkDirtyAndBroadcastProperty();
}

std::string GetSubtitle() {
  Course* p = Cur();
  return p != nullptr ? p->m_sSubTitle : std::string();
}
void SetSubtitle(const std::string& s) {
  Course* p = Cur();
  if (p == nullptr || p->m_sSubTitle == s) {
    return;
  }
  p->m_sSubTitle = s;
  MarkDirtyAndBroadcastProperty();
}

std::string GetScripter() {
  Course* p = Cur();
  return p != nullptr ? p->m_sScripter : std::string();
}
void SetScripter(const std::string& s) {
  Course* p = Cur();
  if (p == nullptr || p->m_sScripter == s) {
    return;
  }
  p->m_sScripter = s;
  MarkDirtyAndBroadcastProperty();
}

std::string GetDescription() {
  Course* p = Cur();
  return p != nullptr ? p->m_sDescription : std::string();
}
void SetDescription(const std::string& s) {
  Course* p = Cur();
  if (p == nullptr || p->m_sDescription == s) {
    return;
  }
  p->m_sDescription = s;
  MarkDirtyAndBroadcastProperty();
}

bool GetRepeat() {
  Course* p = Cur();
  return p != nullptr && p->m_bRepeat;
}
void SetRepeat(bool b) {
  Course* p = Cur();
  if (p == nullptr || p->m_bRepeat == b) {
    return;
  }
  p->m_bRepeat = b;
  MarkDirtyAndBroadcastProperty();
}

bool GetShuffle() {
  Course* p = Cur();
  return p != nullptr && p->m_bShuffle;
}
void SetShuffle(bool b) {
  Course* p = Cur();
  if (p == nullptr || p->m_bShuffle == b) {
    return;
  }
  p->m_bShuffle = b;
  MarkDirtyAndBroadcastProperty();
}

int GetLives() {
  Course* p = Cur();
  return p != nullptr ? p->m_iLives : -1;
}
void SetLives(int n) {
  Course* p = Cur();
  if (p == nullptr || p->m_iLives == n) {
    return;
  }
  p->m_iLives = n;
  MarkDirtyAndBroadcastProperty();
}

float GetGoalSeconds() {
  Course* p = Cur();
  return p != nullptr ? p->m_fGoalSeconds : 0.f;
}
void SetGoalSeconds(float f) {
  Course* p = Cur();
  if (p == nullptr || p->m_fGoalSeconds == f) {
    return;
  }
  p->m_fGoalSeconds = f;
  MarkDirtyAndBroadcastProperty();
}

int GetCustomMeter(int cd) {
  Course* p = Cur();
  if (p == nullptr || cd < 0 || cd >= NUM_Difficulty) {
    return -1;
  }
  return p->m_iCustomMeter[cd];
}
void SetCustomMeter(int cd, int meter) {
  Course* p = Cur();
  if (p == nullptr || cd < 0 || cd >= NUM_Difficulty) {
    return;
  }
  if (p->m_iCustomMeter[cd] == meter) {
    return;
  }
  p->m_iCustomMeter[cd] = meter;
  MarkDirtyAndBroadcastProperty();
}

std::vector<std::string> GetStyles() {
  Course* p = Cur();
  std::vector<std::string> v;
  if (p != nullptr) {
    v.assign(p->m_setStyles.begin(), p->m_setStyles.end());
    std::sort(v.begin(), v.end());
  }
  return v;
}
void AddStyle(const std::string& s) {
  Course* p = Cur();
  if (p == nullptr || s.empty()) {
    return;
  }
  if (p->m_setStyles.insert(s).second) {
    MarkDirtyAndBroadcastProperty();
  }
}
void RemoveStyle(const std::string& s) {
  Course* p = Cur();
  if (p == nullptr) {
    return;
  }
  if (p->m_setStyles.erase(s) > 0) {
    MarkDirtyAndBroadcastProperty();
  }
}

std::vector<std::string> GetAvailableStyles() {
  std::vector<std::string> v;
  if (GAMEMAN == nullptr || GAMESTATE == nullptr) {
    return v;
  }
  std::vector<const Style*> vp;
  GAMEMAN->GetStylesForGame(GAMESTATE->m_pCurGame, vp);
  for (const Style* p : vp) {
    if (p != nullptr && p->m_szName != nullptr) {
      v.emplace_back(p->m_szName);
    }
  }
  return v;
}

// ===========================================================================
// Entry helpers
// ===========================================================================
static std::string DetectSelectorKind(const CourseEntry& e) {
  if (e.bUseSongSelect) {
    return "SongSelect";
  }
  switch (e.songSort) {
    case SongSort_MostPlays:
      return "Best";
    case SongSort_FewestPlays:
      return "Worst";
    case SongSort_TopGrades:
      return "GradeBest";
    case SongSort_LowestGrades:
      return "GradeWorst";
    default:
      break;
  }
  if (e.songID.IsValid()) {
    return "Fixed";
  }
  if (!e.songCriteria.m_vsGroupNames.empty()) {
    return "GroupRandom";
  }
  return "PureRandom";
}

static EntryView ToView(const CourseEntry& e) {
  EntryView v;
  v.sSelectorKind = DetectSelectorKind(e);

  Song* pSong = e.songID.ToSong();
  if (pSong != nullptr) {
    v.sSongTitle = pSong->GetDisplayMainTitle();
    v.sSongSubtitle = pSong->GetDisplaySubTitle();
    v.sSongGroup = pSong->m_sGroupName;
    v.sSongPath = pSong->GetSongDir();
  } else if (!e.songCriteria.m_vsGroupNames.empty()) {
    v.sSongGroup = e.songCriteria.m_vsGroupNames.front();
  }

  v.iChooseIndex = e.iChooseIndex + 1;

  if (e.stepsCriteria.m_difficulty != Difficulty_Invalid) {
    v.sDifficulty =
        CourseDifficultyToLocalizedString(e.stepsCriteria.m_difficulty);
  }
  v.iLowMeter = e.stepsCriteria.m_iLowMeter;
  v.iHighMeter = e.stepsCriteria.m_iHighMeter;
  v.bSecret = e.bSecret;
  v.bNoDifficult = e.bNoDifficult;
  v.iGainLives = e.iGainLives;
  v.fGainSeconds = e.fGainSeconds;
  v.sMods = e.sModifiers;
  v.iNumAttacks = static_cast<int>(e.attacks.size());
  v.bHasSongSelectFilters =
      e.bUseSongSelect &&
      (!e.songCriteria.m_vsSongNames.empty() ||
       !e.songCriteria.m_vsGroupNames.empty() ||
       !e.songCriteria.m_vsArtistNames.empty() ||
       e.songCriteria.m_bUseSongGenreAllowedList ||
       e.songCriteria.m_fMinBPM > 0 || e.songCriteria.m_fMaxBPM > 0 ||
       e.songCriteria.m_fMinDurationSeconds > 0 ||
       e.songCriteria.m_fMaxDurationSeconds > 0 ||
       e.stepsCriteria.m_iLowMeter > 0 || e.stepsCriteria.m_iHighMeter > 0 ||
       !e.stepsCriteria.m_vDifficulties.empty());
  return v;
}

int GetEntryCount() {
  Course* p = Cur();
  return p != nullptr ? static_cast<int>(p->m_vEntries.size()) : 0;
}

EntryView GetEntry(int i1) {
  EntryView v;
  const CourseEntry* e = CurEntryConst(i1);
  if (e != nullptr) {
    v = ToView(*e);
  }
  return v;
}

int AddEntry() {
  Course* p = Cur();
  if (p == nullptr) {
    return 0;
  }
  CourseEntry ce;
  CourseUtil::MakeDefaultEditCourseEntry(ce);
  p->m_vEntries.push_back(ce);
  g_iSelectedEntryIndex = static_cast<int>(p->m_vEntries.size());
  MarkDirtyAndBroadcastEntries();
  Broadcast("EditCourseSelectedEntryChanged");
  return g_iSelectedEntryIndex;
}

bool RemoveEntry(int i1) {
  Course* p = Cur();
  if (p == nullptr || i1 < 1 || i1 > static_cast<int>(p->m_vEntries.size())) {
    return false;
  }
  p->m_vEntries.erase(p->m_vEntries.begin() + (i1 - 1));
  const int iMax = static_cast<int>(p->m_vEntries.size());
  if (g_iSelectedEntryIndex > iMax) {
    g_iSelectedEntryIndex = iMax;
  }
  if (g_iSelectedEntryIndex < 1 && iMax > 0) {
    g_iSelectedEntryIndex = 1;
  }
  MarkDirtyAndBroadcastEntries();
  Broadcast("EditCourseSelectedEntryChanged");
  return true;
}

bool MoveEntry(int from1, int to1) {
  Course* p = Cur();
  if (p == nullptr) {
    return false;
  }
  const int n = static_cast<int>(p->m_vEntries.size());
  if (from1 < 1 || from1 > n || to1 < 1 || to1 > n || from1 == to1) {
    return false;
  }
  CourseEntry e = p->m_vEntries[from1 - 1];
  p->m_vEntries.erase(p->m_vEntries.begin() + (from1 - 1));
  p->m_vEntries.insert(p->m_vEntries.begin() + (to1 - 1), e);
  g_iSelectedEntryIndex = to1;
  MarkDirtyAndBroadcastEntries();
  Broadcast("EditCourseSelectedEntryChanged");
  return true;
}

int DuplicateEntry(int i1) {
  Course* p = Cur();
  if (p == nullptr || i1 < 1 || i1 > static_cast<int>(p->m_vEntries.size())) {
    return 0;
  }
  CourseEntry e = p->m_vEntries[i1 - 1];
  p->m_vEntries.insert(p->m_vEntries.begin() + i1, e);
  g_iSelectedEntryIndex = i1 + 1;
  MarkDirtyAndBroadcastEntries();
  Broadcast("EditCourseSelectedEntryChanged");
  return g_iSelectedEntryIndex;
}

int GetSelectedEntryIndex() { return g_iSelectedEntryIndex; }

void SetSelectedEntryIndex(int i1) {
  Course* p = Cur();
  const int iMax = p != nullptr ? static_cast<int>(p->m_vEntries.size()) : 0;
  if (i1 < 0) {
    i1 = 0;
  }
  if (i1 > iMax) {
    i1 = iMax;
  }
  if (i1 == g_iSelectedEntryIndex) {
    return;
  }
  g_iSelectedEntryIndex = i1;
  Broadcast("EditCourseSelectedEntryChanged");
}

// ===========================================================================
// Per-entry mutators
// ===========================================================================
// Clears all selector-kind specific state so the new kind starts clean.
static void ResetSelectorState(CourseEntry& e) {
  e.songID = SongID();
  e.songCriteria = SongCriteria();
  e.bUseSongSelect = false;
  e.songSort = SongSort_Randomize;
  e.iChooseIndex = 0;
}

bool SetEntrySelectorKind(int i1, const std::string& sKind) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (DetectSelectorKind(*e) == sKind) {
    return true;
  }

  ResetSelectorState(*e);
  if (sKind == "Fixed") {
    // Caller follows up with SetEntrySongByPath.
  } else if (sKind == "PureRandom") {
    e->bSecret = true;
  } else if (sKind == "GroupRandom") {
    e->bSecret = true;
  } else if (sKind == "Best") {
    e->songSort = SongSort_MostPlays;
  } else if (sKind == "Worst") {
    e->songSort = SongSort_FewestPlays;
  } else if (sKind == "GradeBest") {
    e->songSort = SongSort_TopGrades;
  } else if (sKind == "GradeWorst") {
    e->songSort = SongSort_LowestGrades;
  } else if (sKind == "SongSelect") {
    e->bUseSongSelect = true;
  } else {
    return false;
  }
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntrySongByPath(
    int i1, const std::string& sGroup, const std::string& sTitle) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  Song* p = SONGMAN->FindSong(sGroup, sTitle);
  if (p == nullptr) {
    return false;
  }
  e->songID.FromSong(p);
  // Track the group too so the writer can round-trip "Group/Title".
  e->songCriteria.m_vsGroupNames.clear();
  if (!sGroup.empty()) {
    e->songCriteria.m_vsGroupNames.push_back(sGroup);
  }
  e->songSort = SongSort_Randomize;
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryGroup(int i1, const std::string& sGroup) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  e->songCriteria.m_vsGroupNames.clear();
  if (!sGroup.empty()) {
    e->songCriteria.m_vsGroupNames.push_back(sGroup);
  }
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryChooseIndex(int i1, int iIndex1) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr || iIndex1 < 1) {
    return false;
  }
  e->iChooseIndex = iIndex1 - 1;
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryDifficulty(int i1, const std::string& sDifficulty) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (sDifficulty.empty()) {
    e->stepsCriteria.m_difficulty = Difficulty_Invalid;
  } else {
    Difficulty d = StringToDifficulty(sDifficulty);
    if (d == Difficulty_Invalid) {
      d = OldStyleStringToDifficulty(sDifficulty);
    }
    if (d == Difficulty_Invalid) {
      return false;
    }
    e->stepsCriteria.m_difficulty = d;
    // Difficulty and meter range are mutually exclusive in old-style entries.
    e->stepsCriteria.m_iLowMeter = -1;
    e->stepsCriteria.m_iHighMeter = -1;
  }
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryMeterRange(int i1, int iLow, int iHigh) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (iLow < 0) {
    iLow = -1;
  }
  if (iHigh < 0) {
    iHigh = -1;
  }
  if (iLow > 0 && iHigh > 0 && iHigh < iLow) {
    std::swap(iLow, iHigh);
  }
  e->stepsCriteria.m_iLowMeter = iLow;
  e->stepsCriteria.m_iHighMeter = iHigh;
  if (iLow > 0 || iHigh > 0) {
    e->stepsCriteria.m_difficulty = Difficulty_Invalid;
  }
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntrySecret(int i1, bool b) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (e->bSecret == b) {
    return true;
  }
  e->bSecret = b;
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryNoDifficult(int i1, bool b) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (e->bNoDifficult == b) {
    return true;
  }
  e->bNoDifficult = b;
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryGainLives(int i1, int n) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (e->iGainLives == n) {
    return true;
  }
  e->iGainLives = n;
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryGainSeconds(int i1, float f) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (e->fGainSeconds == f) {
    return true;
  }
  e->fGainSeconds = f;
  MarkDirtyAndBroadcastEntry();
  return true;
}

bool SetEntryMods(int i1, const std::string& sMods) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  if (e->sModifiers == sMods) {
    return true;
  }
  e->sModifiers = sMods;
  MarkDirtyAndBroadcastEntry();
  return true;
}

std::vector<AttackView> GetEntryAttacks(int i1) {
  std::vector<AttackView> v;
  const CourseEntry* e = CurEntryConst(i1);
  if (e == nullptr) {
    return v;
  }
  v.reserve(e->attacks.size());
  for (const Attack& a : e->attacks) {
    AttackView av;
    av.fStart = a.fStartSecond;
    av.fLength = a.fSecsRemaining;
    av.sMods = a.sModifiers;
    v.push_back(av);
  }
  return v;
}

bool SetEntryAttacks(int i1, const std::vector<AttackView>& v) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  e->attacks.clear();
  e->attacks.reserve(v.size());
  for (const AttackView& av : v) {
    Attack a;
    a.fStartSecond = av.fStart;
    a.fSecsRemaining = av.fLength;
    a.sModifiers = av.sMods;
    e->attacks.push_back(a);
  }
  MarkDirtyAndBroadcastEntry();
  return true;
}

SongSelectView GetEntrySongSelect(int i1) {
  SongSelectView v;
  const CourseEntry* e = CurEntryConst(i1);
  if (e == nullptr) {
    return v;
  }
  v.titles = e->songCriteria.m_vsSongNames;
  v.groups = e->songCriteria.m_vsGroupNames;
  v.artists = e->songCriteria.m_vsArtistNames;
  v.genres = e->songCriteria.m_vsSongGenreAllowedList;
  v.fMinBPM = e->songCriteria.m_fMinBPM;
  v.fMaxBPM = e->songCriteria.m_fMaxBPM;
  v.fMinDuration = e->songCriteria.m_fMinDurationSeconds;
  v.fMaxDuration = e->songCriteria.m_fMaxDurationSeconds;
  v.iMinMeter = e->stepsCriteria.m_iLowMeter;
  v.iMaxMeter = e->stepsCriteria.m_iHighMeter;
  for (Difficulty d : e->stepsCriteria.m_vDifficulties) {
    v.difficulties.emplace_back(DifficultyToString(d));
  }
  v.sSortKind = SongSortToString(e->songSort);
  v.iSortIndex = e->iChooseIndex + 1;
  return v;
}

bool SetEntrySongSelect(int i1, const SongSelectView& v) {
  CourseEntry* e = CurEntry(i1);
  if (e == nullptr) {
    return false;
  }
  e->bUseSongSelect = true;
  e->songCriteria.m_vsSongNames = v.titles;
  e->songCriteria.m_vsGroupNames = v.groups;
  e->songCriteria.m_vsArtistNames = v.artists;
  e->songCriteria.m_vsSongGenreAllowedList = v.genres;
  e->songCriteria.m_bUseSongGenreAllowedList = !v.genres.empty();
  e->songCriteria.m_fMinBPM = v.fMinBPM;
  e->songCriteria.m_fMaxBPM = v.fMaxBPM;
  e->songCriteria.m_fMinDurationSeconds = v.fMinDuration;
  e->songCriteria.m_fMaxDurationSeconds = v.fMaxDuration;
  e->stepsCriteria.m_iLowMeter = v.iMinMeter;
  e->stepsCriteria.m_iHighMeter = v.iMaxMeter;
  e->stepsCriteria.m_vDifficulties.clear();
  for (const std::string& s : v.difficulties) {
    Difficulty d = StringToDifficulty(s);
    if (d == Difficulty_Invalid) {
      d = OldStyleStringToDifficulty(s);
    }
    if (d != Difficulty_Invalid) {
      e->stepsCriteria.m_vDifficulties.push_back(d);
    }
  }
  if (!v.sSortKind.empty()) {
    SongSort ss = StringToSongSort(v.sSortKind);
    if (ss != SongSort_Invalid) {
      e->songSort = ss;
      e->iChooseIndex = std::max(0, v.iSortIndex - 1);
    }
  }
  MarkDirtyAndBroadcastEntry();
  return true;
}

// ===========================================================================
// Helpers / catalog
// ===========================================================================
std::vector<std::string> GetSelectorKinds() {
  return {"Fixed", "PureRandom", "GroupRandom", "Best",
          "Worst", "GradeBest",  "GradeWorst",  "SongSelect"};
}

std::vector<std::string> GetDifficulties() {
  std::vector<std::string> v;
  FOREACH_ENUM(Difficulty, d) {
    v.emplace_back(CourseDifficultyToLocalizedString(d));
  }
  return v;
}

std::vector<std::string> GetCourseDifficulties() {
  std::vector<std::string> v;
  for (const CourseDifficulty& cd :
       CommonMetrics::COURSE_DIFFICULTIES_TO_SHOW.GetValue()) {
    v.emplace_back(CourseDifficultyToLocalizedString(cd));
  }
  return v;
}

std::vector<std::string> GetSortKinds() {
  std::vector<std::string> v;
  FOREACH_ENUM(SongSort, ss) { v.emplace_back(SongSortToLocalizedString(ss)); }
  return v;
}

std::vector<std::string> GetAllGroups() {
  std::vector<std::string> v;
  if (SONGMAN != nullptr) {
    SONGMAN->GetSongGroupNames(v);
    std::sort(v.begin(), v.end());
  }
  return v;
}

static bool IsSongSelectable(const Song* p) {
  if (p == nullptr) {
    return false;
  }
  if (p->IsTutorial()) {
    return false;
  }
  // Honor selectable + unlock state same way the legacy editor did.
  SongCriteria sc;
  sc.m_Selectable = SongCriteria::Selectable_Yes;
  sc.m_Tutorial = SongCriteria::Tutorial_No;
  sc.m_Locked = SongCriteria::Locked_Unlocked;
  return sc.Matches(p);
}

std::vector<SongRow> GetSongsInGroup(const std::string& sGroup) {
  std::vector<SongRow> out;
  if (SONGMAN == nullptr) {
    return out;
  }
  const std::vector<Song*>& v = SONGMAN->GetSongs(sGroup);
  std::vector<Song*> kept;
  kept.reserve(v.size());
  for (Song* p : v) {
    if (IsSongSelectable(p)) {
      kept.push_back(p);
    }
  }
  SongUtil::SortSongPointerArrayByTitle(kept);
  out.reserve(kept.size());
  for (Song* p : kept) {
    SongRow r;
    r.sTitle = p->GetDisplayMainTitle();
    r.sSubtitle = p->GetDisplaySubTitle();
    r.sGroup = p->m_sGroupName;
    r.sPath = p->GetSongDir();
    out.push_back(r);
  }
  return out;
}

std::vector<SongRow> GetAllSongs() {
  std::vector<SongRow> out;
  if (SONGMAN == nullptr) {
    return out;
  }
  std::vector<Song*> kept;
  for (Song* p : SONGMAN->GetAllSongs()) {
    if (IsSongSelectable(p)) {
      kept.push_back(p);
    }
  }
  SongUtil::SortSongPointerArrayByTitle(kept);
  out.reserve(kept.size());
  for (Song* p : kept) {
    SongRow r;
    r.sTitle = p->GetDisplayMainTitle();
    r.sSubtitle = p->GetDisplaySubTitle();
    r.sGroup = p->m_sGroupName;
    r.sPath = p->GetSongDir();
    out.push_back(r);
  }
  return out;
}

std::vector<ModCategory> GetModCategories() {
  // Curated lists of common StepMania mods.  Order matters (these appear in
  // the chooser exactly as listed).  Speed mods are split out so the chooser
  // can render them as a single radio group.
  return {
      {"Speed",
       {"0.5x", "0.75x", "1x", "1.25x", "1.5x", "1.75x", "2x", "2.5x", "3x",
        "4x", "5x", "8x", "C200", "C300", "C400", "C500", "M450", "M550"}},
      {"Direction",
       {"mirror", "left", "right", "shuffle", "soft-shuffle", "super-shuffle",
        "stomp"}},
      {"Visual",
       {"hidden", "hiddenoffset", "sudden", "suddenoffset", "stealth", "blink",
        "dark", "blind"}},
      {"Accel", {"boost", "brake", "wave", "expand", "boomerang"}},
      {"Effect",
       {"drunk", "dizzy", "confusion", "mini", "tiny", "flip", "invert",
        "tornado", "twirl", "roll"}},
      {"Appearance",
       {"reverse", "split", "alternate", "cross", "centered", "incoming",
        "space", "hallway", "distant"}},
      {"Hold/Mine",
       {"no-holds", "no-mines", "no-stretch", "all-mines", "no-rolls",
        "no-lifts", "no-fakes"}},
      {"Jumps/Hands",
       {"no-jumps", "no-hands", "no-quads", "all-jumps", "all-hands",
        "all-quads"}},
      {"Difficulty",
       {"no-fail", "no-attack", "battery", "lifetime", "lifetime500",
        "lifetime1000"}},
  };
}

std::vector<std::string> ParseModList(const std::string& s) {
  std::vector<std::string> out;
  std::vector<std::string> parts;
  split(s, ",", parts, true);
  for (std::string& p : parts) {
    TrimLeft(p);
    TrimRight(p);
    if (!p.empty()) {
      out.push_back(p);
    }
  }
  return out;
}

std::string JoinModList(const std::vector<std::string>& v) {
  std::vector<std::string> tmp;
  tmp.reserve(v.size());
  for (const std::string& s : v) {
    std::string t = s;
    TrimLeft(t);
    TrimRight(t);
    if (!t.empty()) {
      tmp.push_back(t);
    }
  }
  return join(", ", tmp);
}

// ===========================================================================
// Actions / save
// ===========================================================================
bool IsDirty() { return g_bDirty; }
void MarkDirty() {
  if (g_bDirty) {
    return;
  }
  g_bDirty = true;
  Broadcast("EditCourseDirtyChanged");
}
void MarkClean() {
  if (!g_bDirty) {
    return;
  }
  g_bDirty = false;
  Broadcast("EditCourseDirtyChanged");
}

static LocalizedString EC_ERROR_SAVING(
    "ScreenOptionsEditCourse", "Error saving course.");
static LocalizedString EC_ERROR_NO_COURSE(
    "ScreenOptionsEditCourse", "No course loaded.");

bool NeedsName() { return EditCourseUtil::s_bNewCourseNeedsName; }

bool Save(std::string& sErrorOut) {
  Course* p = Cur();
  if (p == nullptr) {
    sErrorOut = EC_ERROR_NO_COURSE.GetValue();
    return false;
  }
  if (NeedsName()) {
    sErrorOut = "NeedsName";
    return false;
  }
  if (!EditCourseUtil::Save(p)) {
    sErrorOut = EC_ERROR_SAVING.GetValue();
    return false;
  }
  MarkClean();
  Broadcast("EditCourseSaved");
  return true;
}

bool Rename(const std::string& sName, std::string& sErrorOut) {
  Course* p = Cur();
  if (p == nullptr) {
    sErrorOut = EC_ERROR_NO_COURSE.GetValue();
    return false;
  }
  if (!EditCourseUtil::RenameAndSave(p, sName)) {
    sErrorOut = EC_ERROR_SAVING.GetValue();
    return false;
  }
  EditCourseUtil::s_bNewCourseNeedsName = false;
  MarkClean();
  Broadcast("EditCoursePropertyChanged");
  Broadcast("EditCourseSaved");
  return true;
}

void TestPlay() {
  Course* p = Cur();
  if (p == nullptr || p->m_vEntries.empty() || SCREENMAN == nullptr) {
    return;
  }
  EditCourseUtil::PrepareForPlay();
  if (!g_sPlayScreen.empty()) {
    SCREENMAN->SetNewScreen(g_sPlayScreen);
  }
}

void ReturnToPrev() {
  if (SCREENMAN == nullptr || g_sPrevScreen.empty()) {
    return;
  }
  SCREENMAN->SetNewScreen(g_sPrevScreen);
}

std::string ValidateName(const std::string& sName) {
  std::string sErr;
  if (EditCourseUtil::ValidateEditCourseName(sName, sErr)) {
    return std::string();
  }
  return sErr;
}

int GetMaxNameLength() { return EditCourseUtil::MAX_NAME_LENGTH; }

}  // namespace EditCourse

// ===========================================================================
// Screen class
// ===========================================================================
void ScreenOptionsEditCourse::Init() {
  ScreenWithMenuElements::Init();

  ThemeMetric<std::string> playScreen(m_sName, "PlayScreen");
  ThemeMetric<std::string> prevScreen(m_sName, "PrevScreen");
  EditCourse::g_sPlayScreen = playScreen.GetValue();
  EditCourse::g_sPrevScreen = prevScreen.GetValue();
}

void ScreenOptionsEditCourse::BeginScreen() {
  // Establish a default style so the trail picker has a valid game context.
  std::vector<const Style*> vpStyles;
  GAMEMAN->GetStylesForGame(GAMESTATE->m_pCurGame, vpStyles);
  if (!vpStyles.empty()) {
    GAMESTATE->SetCurrentStyle(vpStyles[0], PLAYER_INVALID);
  }
  EditCourse::g_iSelectedEntryIndex = EditCourse::GetEntryCount() > 0 ? 1 : 0;
  EditCourse::MarkClean();
  EditCourseUtil::UpdateAndSetTrail();

  ScreenWithMenuElements::BeginScreen();
}

// ===========================================================================
// Lua bindings
// ===========================================================================
namespace {

// ----- Lua helpers --------------------------------------------------------
static void PushStringArray(lua_State* L, const std::vector<std::string>& v) {
  lua_createtable(L, static_cast<int>(v.size()), 0);
  for (std::size_t i = 0; i < v.size(); ++i) {
    lua_pushstring(L, v[i].c_str());
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
}

static std::vector<std::string> ReadStringArray(lua_State* L, int idx) {
  std::vector<std::string> out;
  if (!lua_istable(L, idx)) {
    return out;
  }
  const int n = static_cast<int>(lua_objlen(L, idx));
  out.reserve(n);
  for (int i = 1; i <= n; ++i) {
    lua_rawgeti(L, idx, i);
    if (lua_isstring(L, -1)) {
      out.emplace_back(lua_tostring(L, -1));
    }
    lua_pop(L, 1);
  }
  return out;
}

// ----- Property getters/setters -------------------------------------------
#define DEF_STR_GET(NAME, FN)                    \
  int Lua##NAME(lua_State* L) {                  \
    lua_pushstring(L, EditCourse::FN().c_str()); \
    return 1;                                    \
  }
#define DEF_STR_SET(NAME, FN)   \
  int Lua##NAME(lua_State* L) { \
    EditCourse::FN(SArg(1));    \
    return 0;                   \
  }
#define DEF_BOOL_GET(NAME, FN)                    \
  int Lua##NAME(lua_State* L) {                   \
    lua_pushboolean(L, EditCourse::FN() ? 1 : 0); \
    return 1;                                     \
  }
#define DEF_BOOL_SET(NAME, FN)  \
  int Lua##NAME(lua_State* L) { \
    EditCourse::FN(BArg(1));    \
    return 0;                   \
  }
#define DEF_INT_GET(NAME, FN)             \
  int Lua##NAME(lua_State* L) {           \
    lua_pushinteger(L, EditCourse::FN()); \
    return 1;                             \
  }
#define DEF_INT_SET(NAME, FN)   \
  int Lua##NAME(lua_State* L) { \
    EditCourse::FN(IArg(1));    \
    return 0;                   \
  }
#define DEF_FLOAT_GET(NAME, FN)          \
  int Lua##NAME(lua_State* L) {          \
    lua_pushnumber(L, EditCourse::FN()); \
    return 1;                            \
  }
#define DEF_FLOAT_SET(NAME, FN)                  \
  int Lua##NAME(lua_State* L) {                  \
    EditCourse::FN(static_cast<float>(FArg(1))); \
    return 0;                                    \
  }

int LuaHasCourse(lua_State* L) {
  lua_pushboolean(L, EditCourse::HasCourse() ? 1 : 0);
  return 1;
}
DEF_STR_GET(GetTitle, GetTitle)
DEF_STR_SET(SetTitle, SetTitle)
DEF_STR_GET(GetSubtitle, GetSubtitle)
DEF_STR_SET(SetSubtitle, SetSubtitle)
DEF_STR_GET(GetScripter, GetScripter)
DEF_STR_SET(SetScripter, SetScripter)
DEF_STR_GET(GetDescription, GetDescription)
DEF_STR_SET(SetDescription, SetDescription)
DEF_BOOL_GET(GetRepeat, GetRepeat)
DEF_BOOL_SET(SetRepeat, SetRepeat)
DEF_BOOL_GET(GetShuffle, GetShuffle)
DEF_BOOL_SET(SetShuffle, SetShuffle)
DEF_INT_GET(GetLives, GetLives)
DEF_INT_SET(SetLives, SetLives)
DEF_FLOAT_GET(GetGoalSeconds, GetGoalSeconds)
DEF_FLOAT_SET(SetGoalSeconds, SetGoalSeconds)

int LuaGetCustomMeter(lua_State* L) {
  lua_pushinteger(L, EditCourse::GetCustomMeter(IArg(1)));
  return 1;
}
int LuaSetCustomMeter(lua_State* L) {
  EditCourse::SetCustomMeter(IArg(1), IArg(2));
  return 0;
}
int LuaGetStyles(lua_State* L) {
  PushStringArray(L, EditCourse::GetStyles());
  return 1;
}
int LuaAddStyle(lua_State* L) {
  EditCourse::AddStyle(SArg(1));
  return 0;
}
int LuaRemoveStyle(lua_State* L) {
  EditCourse::RemoveStyle(SArg(1));
  return 0;
}
int LuaGetAvailableStyles(lua_State* L) {
  PushStringArray(L, EditCourse::GetAvailableStyles());
  return 1;
}

// ----- Entry list ---------------------------------------------------------
int LuaGetEntryCount(lua_State* L) {
  lua_pushinteger(L, EditCourse::GetEntryCount());
  return 1;
}

static void PushEntryView(lua_State* L, const EditCourse::EntryView& v) {
  lua_createtable(L, 0, 14);
  lua_pushstring(L, v.sSelectorKind.c_str());
  lua_setfield(L, -2, "SelectorKind");
  lua_pushstring(L, v.sSongTitle.c_str());
  lua_setfield(L, -2, "SongTitle");
  lua_pushstring(L, v.sSongSubtitle.c_str());
  lua_setfield(L, -2, "SongSubtitle");
  lua_pushstring(L, v.sSongGroup.c_str());
  lua_setfield(L, -2, "SongGroup");
  lua_pushstring(L, v.sSongPath.c_str());
  lua_setfield(L, -2, "SongPath");
  lua_pushinteger(L, v.iChooseIndex);
  lua_setfield(L, -2, "ChooseIndex");
  lua_pushstring(L, v.sDifficulty.c_str());
  lua_setfield(L, -2, "Difficulty");
  lua_pushinteger(L, v.iLowMeter);
  lua_setfield(L, -2, "LowMeter");
  lua_pushinteger(L, v.iHighMeter);
  lua_setfield(L, -2, "HighMeter");
  lua_pushboolean(L, v.bSecret ? 1 : 0);
  lua_setfield(L, -2, "Secret");
  lua_pushboolean(L, v.bNoDifficult ? 1 : 0);
  lua_setfield(L, -2, "NoDifficult");
  lua_pushinteger(L, v.iGainLives);
  lua_setfield(L, -2, "GainLives");
  lua_pushnumber(L, v.fGainSeconds);
  lua_setfield(L, -2, "GainSeconds");
  lua_pushstring(L, v.sMods.c_str());
  lua_setfield(L, -2, "Mods");
  lua_pushinteger(L, v.iNumAttacks);
  lua_setfield(L, -2, "NumAttacks");
  lua_pushboolean(L, v.bHasSongSelectFilters ? 1 : 0);
  lua_setfield(L, -2, "HasSongSelectFilters");
}

int LuaGetEntry(lua_State* L) {
  PushEntryView(L, EditCourse::GetEntry(IArg(1)));
  return 1;
}

int LuaAddEntry(lua_State* L) {
  lua_pushinteger(L, EditCourse::AddEntry());
  return 1;
}
int LuaRemoveEntry(lua_State* L) {
  lua_pushboolean(L, EditCourse::RemoveEntry(IArg(1)) ? 1 : 0);
  return 1;
}
int LuaMoveEntry(lua_State* L) {
  lua_pushboolean(L, EditCourse::MoveEntry(IArg(1), IArg(2)) ? 1 : 0);
  return 1;
}
int LuaDuplicateEntry(lua_State* L) {
  lua_pushinteger(L, EditCourse::DuplicateEntry(IArg(1)));
  return 1;
}
int LuaGetSelectedEntryIndex(lua_State* L) {
  lua_pushinteger(L, EditCourse::GetSelectedEntryIndex());
  return 1;
}
int LuaSetSelectedEntryIndex(lua_State* L) {
  EditCourse::SetSelectedEntryIndex(IArg(1));
  return 0;
}

// ----- Entry mutators -----------------------------------------------------
int LuaSetEntrySelectorKind(lua_State* L) {
  lua_pushboolean(
      L, EditCourse::SetEntrySelectorKind(IArg(1), SArg(2)) ? 1 : 0);
  return 1;
}
int LuaSetEntrySong(lua_State* L) {
  // Args: (entryIndex, groupName, songTitle)
  lua_pushboolean(
      L, EditCourse::SetEntrySongByPath(IArg(1), SArg(2), SArg(3)) ? 1 : 0);
  return 1;
}
int LuaSetEntryGroup(lua_State* L) {
  lua_pushboolean(L, EditCourse::SetEntryGroup(IArg(1), SArg(2)) ? 1 : 0);
  return 1;
}
int LuaSetEntryChooseIndex(lua_State* L) {
  lua_pushboolean(L, EditCourse::SetEntryChooseIndex(IArg(1), IArg(2)) ? 1 : 0);
  return 1;
}
int LuaSetEntryDifficulty(lua_State* L) {
  // Difficulty can be "" to clear.
  const char* s = lua_isnoneornil(L, 2) ? "" : luaL_checkstring(L, 2);
  lua_pushboolean(
      L,
      EditCourse::SetEntryDifficulty(IArg(1), s != nullptr ? s : "") ? 1 : 0);
  return 1;
}
int LuaSetEntryMeterRange(lua_State* L) {
  lua_pushboolean(
      L, EditCourse::SetEntryMeterRange(IArg(1), IArg(2), IArg(3)) ? 1 : 0);
  return 1;
}
int LuaSetEntrySecret(lua_State* L) {
  lua_pushboolean(L, EditCourse::SetEntrySecret(IArg(1), BArg(2)) ? 1 : 0);
  return 1;
}
int LuaSetEntryNoDifficult(lua_State* L) {
  lua_pushboolean(L, EditCourse::SetEntryNoDifficult(IArg(1), BArg(2)) ? 1 : 0);
  return 1;
}
int LuaSetEntryGainLives(lua_State* L) {
  lua_pushboolean(L, EditCourse::SetEntryGainLives(IArg(1), IArg(2)) ? 1 : 0);
  return 1;
}
int LuaSetEntryGainSeconds(lua_State* L) {
  lua_pushboolean(
      L, EditCourse::SetEntryGainSeconds(IArg(1), static_cast<float>(FArg(2)))
             ? 1
             : 0);
  return 1;
}
int LuaSetEntryMods(lua_State* L) {
  lua_pushboolean(L, EditCourse::SetEntryMods(IArg(1), SArg(2)) ? 1 : 0);
  return 1;
}

int LuaGetEntryAttacks(lua_State* L) {
  auto v = EditCourse::GetEntryAttacks(IArg(1));
  lua_createtable(L, static_cast<int>(v.size()), 0);
  for (std::size_t i = 0; i < v.size(); ++i) {
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, v[i].fStart);
    lua_setfield(L, -2, "Start");
    lua_pushnumber(L, v[i].fLength);
    lua_setfield(L, -2, "Length");
    lua_pushstring(L, v[i].sMods.c_str());
    lua_setfield(L, -2, "Mods");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  return 1;
}

int LuaSetEntryAttacks(lua_State* L) {
  int iEntry = IArg(1);
  std::vector<EditCourse::AttackView> v;
  if (lua_istable(L, 2)) {
    const int n = static_cast<int>(lua_objlen(L, 2));
    for (int i = 1; i <= n; ++i) {
      lua_rawgeti(L, 2, i);
      if (lua_istable(L, -1)) {
        EditCourse::AttackView av;
        lua_getfield(L, -1, "Start");
        av.fStart = static_cast<float>(luaL_optnumber(L, -1, 0.0));
        lua_pop(L, 1);
        lua_getfield(L, -1, "Length");
        av.fLength = static_cast<float>(luaL_optnumber(L, -1, 0.0));
        lua_pop(L, 1);
        lua_getfield(L, -1, "Mods");
        av.sMods = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        v.push_back(av);
      }
      lua_pop(L, 1);
    }
  }
  lua_pushboolean(L, EditCourse::SetEntryAttacks(iEntry, v) ? 1 : 0);
  return 1;
}

int LuaGetEntrySongSelect(lua_State* L) {
  EditCourse::SongSelectView v = EditCourse::GetEntrySongSelect(IArg(1));
  lua_createtable(L, 0, 12);
  PushStringArray(L, v.titles);
  lua_setfield(L, -2, "Titles");
  PushStringArray(L, v.groups);
  lua_setfield(L, -2, "Groups");
  PushStringArray(L, v.artists);
  lua_setfield(L, -2, "Artists");
  PushStringArray(L, v.genres);
  lua_setfield(L, -2, "Genres");
  lua_pushnumber(L, v.fMinBPM);
  lua_setfield(L, -2, "MinBPM");
  lua_pushnumber(L, v.fMaxBPM);
  lua_setfield(L, -2, "MaxBPM");
  lua_pushnumber(L, v.fMinDuration);
  lua_setfield(L, -2, "MinDuration");
  lua_pushnumber(L, v.fMaxDuration);
  lua_setfield(L, -2, "MaxDuration");
  lua_pushinteger(L, v.iMinMeter);
  lua_setfield(L, -2, "MinMeter");
  lua_pushinteger(L, v.iMaxMeter);
  lua_setfield(L, -2, "MaxMeter");
  PushStringArray(L, v.difficulties);
  lua_setfield(L, -2, "Difficulties");
  lua_pushstring(L, v.sSortKind.c_str());
  lua_setfield(L, -2, "SortKind");
  lua_pushinteger(L, v.iSortIndex);
  lua_setfield(L, -2, "SortIndex");
  return 1;
}

int LuaSetEntrySongSelect(lua_State* L) {
  int iEntry = IArg(1);
  EditCourse::SongSelectView v;
  if (lua_istable(L, 2)) {
    lua_getfield(L, 2, "Titles");
    v.titles = ReadStringArray(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "Groups");
    v.groups = ReadStringArray(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "Artists");
    v.artists = ReadStringArray(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "Genres");
    v.genres = ReadStringArray(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "MinBPM");
    v.fMinBPM = static_cast<float>(luaL_optnumber(L, -1, -1.0));
    lua_pop(L, 1);
    lua_getfield(L, 2, "MaxBPM");
    v.fMaxBPM = static_cast<float>(luaL_optnumber(L, -1, -1.0));
    lua_pop(L, 1);
    lua_getfield(L, 2, "MinDuration");
    v.fMinDuration = static_cast<float>(luaL_optnumber(L, -1, -1.0));
    lua_pop(L, 1);
    lua_getfield(L, 2, "MaxDuration");
    v.fMaxDuration = static_cast<float>(luaL_optnumber(L, -1, -1.0));
    lua_pop(L, 1);
    lua_getfield(L, 2, "MinMeter");
    v.iMinMeter = static_cast<int>(luaL_optinteger(L, -1, -1));
    lua_pop(L, 1);
    lua_getfield(L, 2, "MaxMeter");
    v.iMaxMeter = static_cast<int>(luaL_optinteger(L, -1, -1));
    lua_pop(L, 1);
    lua_getfield(L, 2, "Difficulties");
    v.difficulties = ReadStringArray(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 2, "SortKind");
    v.sSortKind = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    lua_getfield(L, 2, "SortIndex");
    v.iSortIndex = static_cast<int>(luaL_optinteger(L, -1, 1));
    lua_pop(L, 1);
  }
  lua_pushboolean(L, EditCourse::SetEntrySongSelect(iEntry, v) ? 1 : 0);
  return 1;
}

// ----- Catalog ------------------------------------------------------------
int LuaGetSelectorKinds(lua_State* L) {
  PushStringArray(L, EditCourse::GetSelectorKinds());
  return 1;
}
int LuaGetDifficulties(lua_State* L) {
  PushStringArray(L, EditCourse::GetDifficulties());
  return 1;
}
int LuaGetCourseDifficulties(lua_State* L) {
  PushStringArray(L, EditCourse::GetCourseDifficulties());
  return 1;
}
int LuaGetSortKinds(lua_State* L) {
  PushStringArray(L, EditCourse::GetSortKinds());
  return 1;
}
int LuaGetAllGroups(lua_State* L) {
  PushStringArray(L, EditCourse::GetAllGroups());
  return 1;
}

static void PushSongRows(
    lua_State* L, const std::vector<EditCourse::SongRow>& v) {
  lua_createtable(L, static_cast<int>(v.size()), 0);
  for (std::size_t i = 0; i < v.size(); ++i) {
    lua_createtable(L, 0, 4);
    lua_pushstring(L, v[i].sTitle.c_str());
    lua_setfield(L, -2, "Title");
    lua_pushstring(L, v[i].sSubtitle.c_str());
    lua_setfield(L, -2, "Subtitle");
    lua_pushstring(L, v[i].sGroup.c_str());
    lua_setfield(L, -2, "Group");
    lua_pushstring(L, v[i].sPath.c_str());
    lua_setfield(L, -2, "Path");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
}

int LuaGetSongsInGroup(lua_State* L) {
  PushSongRows(L, EditCourse::GetSongsInGroup(SArg(1)));
  return 1;
}
int LuaGetAllSongs(lua_State* L) {
  PushSongRows(L, EditCourse::GetAllSongs());
  return 1;
}

int LuaGetModCategories(lua_State* L) {
  auto cats = EditCourse::GetModCategories();
  lua_createtable(L, static_cast<int>(cats.size()), 0);
  for (std::size_t i = 0; i < cats.size(); ++i) {
    lua_createtable(L, 0, 2);
    lua_pushstring(L, cats[i].sName.c_str());
    lua_setfield(L, -2, "Name");
    PushStringArray(L, cats[i].mods);
    lua_setfield(L, -2, "Mods");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  return 1;
}

int LuaParseModList(lua_State* L) {
  PushStringArray(L, EditCourse::ParseModList(SArg(1)));
  return 1;
}
int LuaJoinModList(lua_State* L) {
  lua_pushstring(L, EditCourse::JoinModList(ReadStringArray(L, 1)).c_str());
  return 1;
}

// ----- Actions ------------------------------------------------------------
int LuaIsDirty(lua_State* L) {
  lua_pushboolean(L, EditCourse::IsDirty() ? 1 : 0);
  return 1;
}
int LuaMarkClean(lua_State* /* L */) {
  EditCourse::MarkClean();
  return 0;
}
int LuaNeedsName(lua_State* L) {
  lua_pushboolean(L, EditCourse::NeedsName() ? 1 : 0);
  return 1;
}
int LuaSave(lua_State* L) {
  std::string sErr;
  if (EditCourse::Save(sErr)) {
    lua_pushboolean(L, 1);
    lua_pushnil(L);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, sErr.c_str());
  }
  return 2;
}
int LuaRename(lua_State* L) {
  std::string sErr;
  if (EditCourse::Rename(SArg(1), sErr)) {
    lua_pushboolean(L, 1);
    lua_pushnil(L);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, sErr.c_str());
  }
  return 2;
}
int LuaValidateName(lua_State* L) {
  std::string sErr = EditCourse::ValidateName(SArg(1));
  if (sErr.empty()) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, sErr.c_str());
  }
  return 1;
}
int LuaGetMaxNameLength(lua_State* L) {
  lua_pushinteger(L, EditCourse::GetMaxNameLength());
  return 1;
}
int LuaTestPlay(lua_State* /* L */) {
  EditCourse::TestPlay();
  return 0;
}
int LuaReturnToPrev(lua_State* /* L */) {
  EditCourse::ReturnToPrev();
  return 0;
}

const luaL_Reg EditCourseTable[] = {
    {"HasCourse", LuaHasCourse},
    {"GetTitle", LuaGetTitle},
    {"SetTitle", LuaSetTitle},
    {"GetSubtitle", LuaGetSubtitle},
    {"SetSubtitle", LuaSetSubtitle},
    {"GetScripter", LuaGetScripter},
    {"SetScripter", LuaSetScripter},
    {"GetDescription", LuaGetDescription},
    {"SetDescription", LuaSetDescription},
    {"GetRepeat", LuaGetRepeat},
    {"SetRepeat", LuaSetRepeat},
    {"GetShuffle", LuaGetShuffle},
    {"SetShuffle", LuaSetShuffle},
    {"GetLives", LuaGetLives},
    {"SetLives", LuaSetLives},
    {"GetGoalSeconds", LuaGetGoalSeconds},
    {"SetGoalSeconds", LuaSetGoalSeconds},
    {"GetCustomMeter", LuaGetCustomMeter},
    {"SetCustomMeter", LuaSetCustomMeter},
    {"GetStyles", LuaGetStyles},
    {"AddStyle", LuaAddStyle},
    {"RemoveStyle", LuaRemoveStyle},
    {"GetAvailableStyles", LuaGetAvailableStyles},
    {"GetEntryCount", LuaGetEntryCount},
    {"GetEntry", LuaGetEntry},
    {"AddEntry", LuaAddEntry},
    {"RemoveEntry", LuaRemoveEntry},
    {"MoveEntry", LuaMoveEntry},
    {"DuplicateEntry", LuaDuplicateEntry},
    {"GetSelectedEntryIndex", LuaGetSelectedEntryIndex},
    {"SetSelectedEntryIndex", LuaSetSelectedEntryIndex},
    {"SetEntrySelectorKind", LuaSetEntrySelectorKind},
    {"SetEntrySong", LuaSetEntrySong},
    {"SetEntryGroup", LuaSetEntryGroup},
    {"SetEntryChooseIndex", LuaSetEntryChooseIndex},
    {"SetEntryDifficulty", LuaSetEntryDifficulty},
    {"SetEntryMeterRange", LuaSetEntryMeterRange},
    {"SetEntrySecret", LuaSetEntrySecret},
    {"SetEntryNoDifficult", LuaSetEntryNoDifficult},
    {"SetEntryGainLives", LuaSetEntryGainLives},
    {"SetEntryGainSeconds", LuaSetEntryGainSeconds},
    {"SetEntryMods", LuaSetEntryMods},
    {"GetEntryAttacks", LuaGetEntryAttacks},
    {"SetEntryAttacks", LuaSetEntryAttacks},
    {"GetEntrySongSelect", LuaGetEntrySongSelect},
    {"SetEntrySongSelect", LuaSetEntrySongSelect},
    {"GetSelectorKinds", LuaGetSelectorKinds},
    {"GetDifficulties", LuaGetDifficulties},
    {"GetCourseDifficulties", LuaGetCourseDifficulties},
    {"GetSortKinds", LuaGetSortKinds},
    {"GetAllGroups", LuaGetAllGroups},
    {"GetSongsInGroup", LuaGetSongsInGroup},
    {"GetAllSongs", LuaGetAllSongs},
    {"GetModCategories", LuaGetModCategories},
    {"ParseModList", LuaParseModList},
    {"JoinModList", LuaJoinModList},
    {"IsDirty", LuaIsDirty},
    {"MarkClean", LuaMarkClean},
    {"NeedsName", LuaNeedsName},
    {"Save", LuaSave},
    {"Rename", LuaRename},
    {"ValidateName", LuaValidateName},
    {"GetMaxNameLength", LuaGetMaxNameLength},
    {"TestPlay", LuaTestPlay},
    {"ReturnToPrev", LuaReturnToPrev},
    {nullptr, nullptr},
};

void RegisterEditCourseLua(lua_State* L) {
  luaL_register(L, "EditCourse", EditCourseTable);
  lua_pop(L, 1);
}
REGISTER_WITH_LUA_FUNCTION(RegisterEditCourseLua);

}  // namespace

/*
 * (c) 2003-2004 Chris Danford
 * All rights reserved.
 */
