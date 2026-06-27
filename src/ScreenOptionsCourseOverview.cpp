#include "ScreenOptionsCourseOverview.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

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
#include "RageUtil.h"
#include "RageUtil/RandomNumbers.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "ScreenPrompt.h"
#include "ScreenTextEntry.h"
#include "Song.h"
#include "Style.h"
#include "ThemeMetric.h"
#include "Trail.h"

REGISTER_SCREEN_CLASS(ScreenOptionsCourseOverview);

static AutoScreenMessage(SM_BackFromRenameCourseEntry);
static AutoScreenMessage(SM_BackFromDeleteCourseConfirm);
static AutoScreenMessage(SM_BackFromSaveAsRename);

namespace CourseOverview {

static std::vector<CourseEntryInfo> g_vEntries;
static std::string g_sPlayScreen;
static std::string g_sEditScreen;
static std::string g_sPrevScreen;

// Engine-owned UI state.  Reset on every BeginScreen.
static Focus g_Focus = Focus_List;
static int g_iTabIndex = 1;
static int g_iWheelPos = 1;
static const TabId g_TabOrder[NUM_TABS] = {Tab_Play,   Tab_Edit,   Tab_Shuffle,
                                           Tab_Rename, Tab_Delete, Tab_Save,
                                           Tab_Back};

static void Broadcast(const char* sMsg) {
  if (MESSAGEMAN != nullptr) {
    MESSAGEMAN->Broadcast(sMsg);
  }
}

static Course* CurCourse() { return GAMESTATE->m_pCurCourse; }

bool HasCourse() { return CurCourse() != nullptr; }

std::string GetTitle() {
  Course* p = CurCourse();
  return p != nullptr ? p->GetDisplayFullTitle() : std::string();
}

std::string GetPath() {
  Course* p = CurCourse();
  return p != nullptr ? p->m_sPath : std::string();
}

bool IsMachineCourse() {
  Course* p = CurCourse();
  return p != nullptr && p->GetLoadedFromProfileSlot() == ProfileSlot_Machine;
}

int GetNumEntries() {
  Course* p = CurCourse();
  return p != nullptr ? static_cast<int>(p->m_vEntries.size()) : 0;
}

std::string GetStepsType() {
  const StepsType st = GAMESTATE->m_stEdit.Get();
  if (st == StepsType_Invalid) {
    return std::string();
  }
  return GAMEMAN->GetStepsTypeInfo(st).GetLocalizedString();
}

std::string GetCourseDifficulty() {
  const CourseDifficulty cd = GAMESTATE->m_cdEdit.Get();
  if (cd == Difficulty_Invalid) {
    return std::string();
  }
  return CourseDifficultyToLocalizedString(cd);
}

float GetTotalSeconds() {
  Course* p = CurCourse();
  if (p == nullptr) {
    return 0.f;
  }
  const StepsType st = GAMESTATE->m_stEdit.Get();
  if (st == StepsType_Invalid) {
    return 0.f;
  }
  float fSecs = 0.f;
  p->GetTotalSeconds(st, fSecs);
  return fSecs;
}

const std::vector<CourseEntryInfo>& GetEntries() { return g_vEntries; }

void Rebuild() {
  g_vEntries.clear();
  Course* p = CurCourse();
  if (p == nullptr) {
    Broadcast("CourseOverviewCourseChanged");
    return;
  }

  // Prefer the resolved trail so we get actual song/steps pointers; if the
  // trail isn't available (e.g. brand-new empty course) fall back to the raw
  // CourseEntry list.
  Trail* pTrail = nullptr;
  const StepsType st = GAMESTATE->m_stEdit.Get();
  const CourseDifficulty cd = GAMESTATE->m_cdEdit.Get();
  if (st != StepsType_Invalid && cd != Difficulty_Invalid) {
    pTrail = p->GetTrail(st, cd);
  }

  if (pTrail != nullptr && !pTrail->m_vEntries.empty()) {
    g_vEntries.reserve(pTrail->m_vEntries.size());
    for (const TrailEntry& te : pTrail->m_vEntries) {
      CourseEntryInfo info;
      if (te.pSong != nullptr) {
        info.sSongTitle = te.pSong->GetDisplayMainTitle();
        info.sSongSubtitle = te.pSong->GetDisplaySubTitle();
      }
      if (te.dc != Difficulty_Invalid) {
        info.sDifficulty = CourseDifficultyToLocalizedString(te.dc);
      }
      info.iLowMeter = te.iLowMeter;
      info.iHighMeter = te.iHighMeter;
      info.bSecret = te.bSecret;
      g_vEntries.push_back(info);
    }
  } else {
    g_vEntries.reserve(p->m_vEntries.size());
    for (const CourseEntry& ce : p->m_vEntries) {
      CourseEntryInfo info;
      Song* pSong = ce.songID.ToSong();
      if (pSong != nullptr) {
        info.sSongTitle = pSong->GetDisplayMainTitle();
        info.sSongSubtitle = pSong->GetDisplaySubTitle();
      }
      info.iLowMeter = ce.stepsCriteria.m_iLowMeter;
      info.iHighMeter = ce.stepsCriteria.m_iHighMeter;
      info.bSecret = ce.bSecret;
      g_vEntries.push_back(info);
    }
  }
  Broadcast("CourseOverviewEntriesChanged");
}

void Play() {
  if (CurCourse() == nullptr) {
    return;
  }
  EditCourseUtil::PrepareForPlay();
  if (SCREENMAN != nullptr && !g_sPlayScreen.empty()) {
    SCREENMAN->SetNewScreen(g_sPlayScreen);
  }
}

void Edit() {
  if (CurCourse() == nullptr) {
    return;
  }
  if (SCREENMAN != nullptr && !g_sEditScreen.empty()) {
    SCREENMAN->SetNewScreen(g_sEditScreen);
  }
}

void Shuffle() {
  Course* p = CurCourse();
  if (p == nullptr || p->m_vEntries.empty()) {
    return;
  }
  std::shuffle(
      p->m_vEntries.begin(), p->m_vEntries.end(), g_RandomNumberGenerator);
  const StepsType st = GAMESTATE->m_stEdit.Get();
  if (st != StepsType_Invalid) {
    Trail* pTrail = p->GetTrailForceRegenCache(st);
    GAMESTATE->m_pCurTrail[PLAYER_1].Set(pTrail);
  }
  Rebuild();
}

static LocalizedString CO_ERROR_SAVING(
    "ScreenOptionsCourseOverview", "Error saving course.");
static LocalizedString CO_ERROR_NO_COURSE(
    "ScreenOptionsCourseOverview", "No course loaded.");

bool NeedsName() { return EditCourseUtil::s_bNewCourseNeedsName; }

bool Save(std::string& sErrorOut) {
  Course* p = CurCourse();
  if (p == nullptr) {
    sErrorOut = CO_ERROR_NO_COURSE.GetValue();
    return false;
  }
  if (NeedsName()) {
    // Theme is expected to drive ScreenTextEntry and then call Rename, which
    // performs RenameAndSave atomically.  Surfacing the error here lets the
    // theme decide between a prompt and a SystemMessage.
    sErrorOut = "NeedsName";
    return false;
  }
  if (!EditCourseUtil::Save(p)) {
    sErrorOut = CO_ERROR_SAVING.GetValue();
    return false;
  }
  Broadcast("CourseOverviewSaved");
  return true;
}

bool Rename(const std::string& sNewName, std::string& sErrorOut) {
  Course* p = CurCourse();
  if (p == nullptr) {
    sErrorOut = CO_ERROR_NO_COURSE.GetValue();
    return false;
  }
  if (!EditCourseUtil::RenameAndSave(p, sNewName)) {
    sErrorOut = CO_ERROR_SAVING.GetValue();
    return false;
  }
  EditCourseUtil::s_bNewCourseNeedsName = false;
  Broadcast("CourseOverviewSaved");
  Broadcast("CourseOverviewCourseChanged");
  return true;
}

bool Delete(std::string& sErrorOut) {
  Course* p = CurCourse();
  if (p == nullptr) {
    sErrorOut = CO_ERROR_NO_COURSE.GetValue();
    return false;
  }
  if (!EditCourseUtil::RemoveAndDeleteFile(p)) {
    sErrorOut = ssprintf("Couldn't delete %s", p->m_sPath.c_str());
    return false;
  }
  GAMESTATE->m_pCurCourse.Set(nullptr);
  GAMESTATE->m_pCurTrail[PLAYER_1].Set(nullptr);
  g_vEntries.clear();
  Broadcast("CourseOverviewDeleted");
  if (SCREENMAN != nullptr && !g_sPrevScreen.empty()) {
    SCREENMAN->SetNewScreen(g_sPrevScreen);
  }
  return true;
}

std::string ValidateName(const std::string& sName) {
  std::string sErr;
  if (EditCourseUtil::ValidateEditCourseName(sName, sErr)) {
    return std::string();
  }
  return sErr;
}

int GetMaxNameLength() { return EditCourseUtil::MAX_NAME_LENGTH; }

std::string GetPlayScreen() { return g_sPlayScreen; }
std::string GetEditScreen() { return g_sEditScreen; }
std::string GetPrevScreen() { return g_sPrevScreen; }

// ---------------------------------------------------------------------------
// Engine-owned UI state.
// ---------------------------------------------------------------------------
Focus GetFocus() { return g_Focus; }

void SetFocus(Focus f) {
  if (g_Focus == f) {
    return;
  }
  g_Focus = f;
  Broadcast("CourseOverviewFocusChanged");
}

int GetTabIndex() { return g_iTabIndex; }

void SetTabIndex(int iIndex1) {
  if (iIndex1 < 1) {
    iIndex1 = NUM_TABS;
  }
  if (iIndex1 > NUM_TABS) {
    iIndex1 = 1;
  }
  if (iIndex1 == g_iTabIndex) {
    return;
  }
  g_iTabIndex = iIndex1;
  Broadcast("CourseOverviewTabChanged");
}

int GetTabCount() { return NUM_TABS; }

TabId GetTabIdAt(int iIndex1) {
  if (iIndex1 < 1 || iIndex1 > NUM_TABS) {
    return Tab_Play;
  }
  return g_TabOrder[iIndex1 - 1];
}

static LocalizedString CO_TAB_PLAY("ScreenOptionsCourseOverview", "Play");
static LocalizedString CO_TAB_EDIT("ScreenOptionsCourseOverview", "Edit");
static LocalizedString CO_TAB_SHUFFLE("ScreenOptionsCourseOverview", "Shuffle");
static LocalizedString CO_TAB_RENAME("ScreenOptionsCourseOverview", "Rename");
static LocalizedString CO_TAB_DELETE("ScreenOptionsCourseOverview", "Delete");
static LocalizedString CO_TAB_SAVE("ScreenOptionsCourseOverview", "Save");
static LocalizedString CO_TAB_BACK("ScreenOptionsCourseOverview", "Back");

std::string GetTabLabel(int iIndex1) {
  switch (GetTabIdAt(iIndex1)) {
    case Tab_Play:
      return CO_TAB_PLAY.GetValue();
    case Tab_Edit:
      return CO_TAB_EDIT.GetValue();
    case Tab_Shuffle:
      return CO_TAB_SHUFFLE.GetValue();
    case Tab_Rename:
      return CO_TAB_RENAME.GetValue();
    case Tab_Delete:
      return CO_TAB_DELETE.GetValue();
    case Tab_Save:
      return CO_TAB_SAVE.GetValue();
    case Tab_Back:
      return CO_TAB_BACK.GetValue();
    default:
      return std::string();
  }
}

int GetWheelPos() { return g_iWheelPos; }

void SetWheelPos(int iPos1) {
  const int n = static_cast<int>(g_vEntries.size());
  if (n <= 0) {
    g_iWheelPos = 1;
    return;
  }
  if (iPos1 < 1) {
    iPos1 = n;
  }
  if (iPos1 > n) {
    iPos1 = 1;
  }
  if (iPos1 == g_iWheelPos) {
    return;
  }
  g_iWheelPos = iPos1;
  Broadcast("CourseOverviewScrollChanged");
}

void AdvanceWheel(int iDelta) { SetWheelPos(g_iWheelPos + iDelta); }

void AdvanceTab(int iDelta) { SetTabIndex(g_iTabIndex + iDelta); }

void ResetUI() {
  g_Focus = Focus_List;
  g_iTabIndex = 1;
  g_iWheelPos = 1;
}

}  // namespace CourseOverview

// ===========================================================================
// ScreenOptionsCourseOverview: thin host screen.
// ===========================================================================
void ScreenOptionsCourseOverview::Init() {
  ScreenWithMenuElements::Init();

  ThemeMetric<std::string> playScreen(m_sName, "PlayScreen");
  ThemeMetric<std::string> editScreen(m_sName, "EditScreen");
  ThemeMetric<std::string> prevScreen(m_sName, "PrevScreen");
  CourseOverview::g_sPlayScreen = playScreen.GetValue();
  CourseOverview::g_sEditScreen = editScreen.GetValue();
  CourseOverview::g_sPrevScreen = prevScreen.GetValue();
}

void ScreenOptionsCourseOverview::BeginScreen() {
  CourseOverview::ResetUI();
  CourseOverview::Rebuild();
  ScreenWithMenuElements::BeginScreen();
}

// ---------------------------------------------------------------------------
// Tab action dispatch + interactive prompts.
// ---------------------------------------------------------------------------
namespace {

// Tracks whether a pending rename prompt is for "Save when NeedsName" (we
// should retry Save afterwards) vs a regular Rename action.
static bool s_bPendingSaveAfterRename = false;

static LocalizedString CO_RENAME_QUESTION(
    "ScreenOptionsCourseOverview", "Enter a name for this course.");
static LocalizedString CO_DELETE_CONFIRM(
    "ScreenOptionsCourseOverview",
    "This course will be permanently deleted.\n\nContinue?");
static LocalizedString CO_EMPTY_COURSE(
    "ScreenOptionsCourseOverview", "Course has no entries.");
static LocalizedString CO_SAVED("ScreenOptionsCourseOverview", "Course saved.");
static LocalizedString CO_DELETED(
    "ScreenOptionsCourseOverview", "Course deleted.");

static bool ValidateCourseName(
    const std::string& sAnswer, std::string& sErrorOut) {
  sErrorOut = CourseOverview::ValidateName(sAnswer);
  return sErrorOut.empty();
}

static void OnRenameOK(const std::string& sAnswer) {
  std::string sErr;
  const bool bWasSaveRetry = s_bPendingSaveAfterRename;
  s_bPendingSaveAfterRename = false;
  if (!CourseOverview::Rename(sAnswer, sErr)) {
    if (SCREENMAN != nullptr) {
      SCREENMAN->SystemMessage(sErr);
    }
    return;
  }
  if (bWasSaveRetry) {
    // Rename writes the file already; nothing else needed for save.
  }
  if (SCREENMAN != nullptr) {
    SCREENMAN->SystemMessage(CO_SAVED.GetValue());
  }
}

static void OnRenameCancel() { s_bPendingSaveAfterRename = false; }

static void PromptForName(bool bIsSaveRetry) {
  s_bPendingSaveAfterRename = bIsSaveRetry;
  ScreenTextEntry::TextEntry(
      SM_BackFromRenameCourseEntry, CO_RENAME_QUESTION.GetValue(),
      CourseOverview::GetTitle(), CourseOverview::GetMaxNameLength(),
      ValidateCourseName, OnRenameOK, OnRenameCancel);
}

static void DispatchTab(CourseOverview::TabId id) {
  using namespace CourseOverview;
  switch (id) {
    case Tab_Play: {
      if (!HasCourse()) {
        return;
      }
      if (GetNumEntries() == 0) {
        if (SCREENMAN != nullptr) {
          SCREENMAN->SystemMessage(CO_EMPTY_COURSE.GetValue());
        }
        return;
      }
      Play();
      break;
    }
    case Tab_Edit:
      Edit();
      break;
    case Tab_Shuffle:
      Shuffle();
      break;
    case Tab_Rename: {
      if (!HasCourse()) {
        return;
      }
      PromptForName(false);
      break;
    }
    case Tab_Delete: {
      if (!HasCourse()) {
        return;
      }
      ScreenPrompt::Prompt(
          SM_BackFromDeleteCourseConfirm, CO_DELETE_CONFIRM.GetValue(),
          PROMPT_YES_NO, ANSWER_NO);
      break;
    }
    case Tab_Save: {
      if (!HasCourse()) {
        return;
      }
      std::string sErr;
      if (CourseOverview::Save(sErr)) {
        if (SCREENMAN != nullptr) {
          SCREENMAN->SystemMessage(CO_SAVED.GetValue());
        }
      } else if (sErr == "NeedsName") {
        PromptForName(true);
      } else if (SCREENMAN != nullptr) {
        SCREENMAN->SystemMessage(sErr);
      }
      break;
    }
    case Tab_Back: {
      if (SCREENMAN != nullptr && !GetPrevScreen().empty()) {
        SCREENMAN->SetNewScreen(GetPrevScreen());
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace

void ScreenOptionsCourseOverview::HandleScreenMessage(const ScreenMessage SM) {
  if (SM == SM_BackFromDeleteCourseConfirm) {
    if (ScreenPrompt::s_LastAnswer == ANSWER_YES) {
      std::string sErr;
      if (CourseOverview::Delete(sErr)) {
        SCREENMAN->SystemMessage(CO_DELETED.GetValue());
      } else if (SCREENMAN != nullptr) {
        SCREENMAN->SystemMessage(sErr);
      }
    }
    return;
  }
  if (SM == SM_BackFromRenameCourseEntry) {
    // OnRenameOK / OnRenameCancel already handled the work.
    return;
  }
  ScreenWithMenuElements::HandleScreenMessage(SM);
}

// ---------------------------------------------------------------------------
// Input: list focus scrolls the entry preview; tab focus drives the action
// strip.  MenuUp/MenuDown toggles between the two zones.  Select == Back.
// ---------------------------------------------------------------------------
bool ScreenOptionsCourseOverview::MenuLeft(const InputEventPlus& /* input */) {
  using namespace CourseOverview;
  if (GetFocus() == Focus_List) {
    AdvanceWheel(-1);
  } else {
    AdvanceTab(-1);
  }
  return true;
}

bool ScreenOptionsCourseOverview::MenuRight(const InputEventPlus& /* input */) {
  using namespace CourseOverview;
  if (GetFocus() == Focus_List) {
    AdvanceWheel(+1);
  } else {
    AdvanceTab(+1);
  }
  return true;
}

bool ScreenOptionsCourseOverview::MenuUp(const InputEventPlus& /* input */) {
  using namespace CourseOverview;
  SetFocus(GetFocus() == Focus_List ? Focus_Tabs : Focus_List);
  return true;
}

bool ScreenOptionsCourseOverview::MenuDown(const InputEventPlus& input) {
  return MenuUp(input);
}

bool ScreenOptionsCourseOverview::MenuStart(const InputEventPlus& /* input */) {
  using namespace CourseOverview;
  if (GetFocus() == Focus_Tabs) {
    DispatchTab(GetTabIdAt(GetTabIndex()));
  } else {
    // List focus: Start does nothing here (no per-entry action).  Could be
    // wired up later to open an entry editor.
  }
  return true;
}

bool ScreenOptionsCourseOverview::MenuSelect(const InputEventPlus& input) {
  return MenuBack(input);
}

bool ScreenOptionsCourseOverview::MenuBack(const InputEventPlus& /* input */) {
  if (CourseOverview::GetFocus() == CourseOverview::Focus_Tabs) {
    CourseOverview::SetFocus(CourseOverview::Focus_List);
    return true;
  }
  if (SCREENMAN != nullptr && !CourseOverview::GetPrevScreen().empty()) {
    SCREENMAN->SetNewScreen(CourseOverview::GetPrevScreen());
  } else {
    Cancel(SM_GoToPrevScreen);
  }
  return true;
}

// ===========================================================================
// Lua bindings.
// ===========================================================================
namespace {

int LuaHasCourse(lua_State* L) {
  lua_pushboolean(L, CourseOverview::HasCourse() ? 1 : 0);
  return 1;
}

int LuaGetTitle(lua_State* L) {
  lua_pushstring(L, CourseOverview::GetTitle().c_str());
  return 1;
}

int LuaGetPath(lua_State* L) {
  lua_pushstring(L, CourseOverview::GetPath().c_str());
  return 1;
}

int LuaIsMachineCourse(lua_State* L) {
  lua_pushboolean(L, CourseOverview::IsMachineCourse() ? 1 : 0);
  return 1;
}

int LuaGetNumEntries(lua_State* L) {
  lua_pushinteger(L, CourseOverview::GetNumEntries());
  return 1;
}

int LuaGetStepsType(lua_State* L) {
  lua_pushstring(L, CourseOverview::GetStepsType().c_str());
  return 1;
}

int LuaGetCourseDifficulty(lua_State* L) {
  lua_pushstring(L, CourseOverview::GetCourseDifficulty().c_str());
  return 1;
}

int LuaGetTotalSeconds(lua_State* L) {
  lua_pushnumber(L, CourseOverview::GetTotalSeconds());
  return 1;
}

int LuaGetEntries(lua_State* L) {
  const auto& v = CourseOverview::GetEntries();
  lua_createtable(L, static_cast<int>(v.size()), 0);
  for (std::size_t i = 0; i < v.size(); ++i) {
    lua_createtable(L, 0, 6);
    lua_pushstring(L, v[i].sSongTitle.c_str());
    lua_setfield(L, -2, "SongTitle");
    lua_pushstring(L, v[i].sSongSubtitle.c_str());
    lua_setfield(L, -2, "SongSubtitle");
    lua_pushstring(L, v[i].sDifficulty.c_str());
    lua_setfield(L, -2, "Difficulty");
    lua_pushinteger(L, v[i].iLowMeter);
    lua_setfield(L, -2, "LowMeter");
    lua_pushinteger(L, v[i].iHighMeter);
    lua_setfield(L, -2, "HighMeter");
    lua_pushboolean(L, v[i].bSecret ? 1 : 0);
    lua_setfield(L, -2, "IsSecret");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  return 1;
}

int LuaPlay(lua_State* /* L */) {
  CourseOverview::Play();
  return 0;
}

int LuaEdit(lua_State* /* L */) {
  CourseOverview::Edit();
  return 0;
}

int LuaShuffle(lua_State* /* L */) {
  CourseOverview::Shuffle();
  return 0;
}

int LuaNeedsName(lua_State* L) {
  lua_pushboolean(L, CourseOverview::NeedsName() ? 1 : 0);
  return 1;
}

int LuaSave(lua_State* L) {
  std::string sErr;
  if (CourseOverview::Save(sErr)) {
    lua_pushboolean(L, 1);
    lua_pushnil(L);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, sErr.c_str());
  }
  return 2;
}

int LuaRename(lua_State* L) {
  const char* sName = luaL_checkstring(L, 1);
  std::string sErr;
  if (CourseOverview::Rename(sName == nullptr ? "" : sName, sErr)) {
    lua_pushboolean(L, 1);
    lua_pushnil(L);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, sErr.c_str());
  }
  return 2;
}

int LuaDelete(lua_State* L) {
  std::string sErr;
  if (CourseOverview::Delete(sErr)) {
    lua_pushboolean(L, 1);
    lua_pushnil(L);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, sErr.c_str());
  }
  return 2;
}

int LuaValidateName(lua_State* L) {
  const char* sName = luaL_checkstring(L, 1);
  std::string sErr =
      CourseOverview::ValidateName(sName == nullptr ? "" : sName);
  if (sErr.empty()) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, sErr.c_str());
  }
  return 1;
}

int LuaGetMaxNameLength(lua_State* L) {
  lua_pushinteger(L, CourseOverview::GetMaxNameLength());
  return 1;
}

int LuaGetPlayScreen(lua_State* L) {
  lua_pushstring(L, CourseOverview::GetPlayScreen().c_str());
  return 1;
}

int LuaGetEditScreen(lua_State* L) {
  lua_pushstring(L, CourseOverview::GetEditScreen().c_str());
  return 1;
}

int LuaGetPrevScreen(lua_State* L) {
  lua_pushstring(L, CourseOverview::GetPrevScreen().c_str());
  return 1;
}

int LuaGetFocus(lua_State* L) {
  lua_pushstring(
      L, CourseOverview::GetFocus() == CourseOverview::Focus_Tabs ? "tabs"
                                                                  : "list");
  return 1;
}

int LuaGetTabIndex(lua_State* L) {
  lua_pushinteger(L, CourseOverview::GetTabIndex());
  return 1;
}

int LuaGetWheelPos(lua_State* L) {
  lua_pushinteger(L, CourseOverview::GetWheelPos());
  return 1;
}

int LuaGetTabs(lua_State* L) {
  const int n = CourseOverview::GetTabCount();
  lua_createtable(L, n, 0);
  for (int i = 1; i <= n; ++i) {
    lua_createtable(L, 0, 2);
    const CourseOverview::TabId id = CourseOverview::GetTabIdAt(i);
    const char* sKey = "play";
    switch (id) {
      case CourseOverview::Tab_Play:
        sKey = "play";
        break;
      case CourseOverview::Tab_Edit:
        sKey = "edit";
        break;
      case CourseOverview::Tab_Shuffle:
        sKey = "shuffle";
        break;
      case CourseOverview::Tab_Rename:
        sKey = "rename";
        break;
      case CourseOverview::Tab_Delete:
        sKey = "delete";
        break;
      case CourseOverview::Tab_Save:
        sKey = "save";
        break;
      case CourseOverview::Tab_Back:
        sKey = "back";
        break;
      default:
        break;
    }
    lua_pushstring(L, sKey);
    lua_setfield(L, -2, "Id");
    lua_pushstring(L, CourseOverview::GetTabLabel(i).c_str());
    lua_setfield(L, -2, "Label");
    lua_rawseti(L, -2, i);
  }
  return 1;
}

const luaL_Reg CourseOverviewTable[] = {
    {"HasCourse", LuaHasCourse},
    {"GetTitle", LuaGetTitle},
    {"GetPath", LuaGetPath},
    {"IsMachineCourse", LuaIsMachineCourse},
    {"GetNumEntries", LuaGetNumEntries},
    {"GetStepsType", LuaGetStepsType},
    {"GetCourseDifficulty", LuaGetCourseDifficulty},
    {"GetTotalSeconds", LuaGetTotalSeconds},
    {"GetEntries", LuaGetEntries},
    {"Play", LuaPlay},
    {"Edit", LuaEdit},
    {"Shuffle", LuaShuffle},
    {"NeedsName", LuaNeedsName},
    {"Save", LuaSave},
    {"Rename", LuaRename},
    {"Delete", LuaDelete},
    {"ValidateName", LuaValidateName},
    {"GetMaxNameLength", LuaGetMaxNameLength},
    {"GetPlayScreen", LuaGetPlayScreen},
    {"GetEditScreen", LuaGetEditScreen},
    {"GetPrevScreen", LuaGetPrevScreen},
    {"GetFocus", LuaGetFocus},
    {"GetTabIndex", LuaGetTabIndex},
    {"GetWheelPos", LuaGetWheelPos},
    {"GetTabs", LuaGetTabs},
    {nullptr, nullptr},
};

void RegisterCourseOverviewLua(lua_State* L) {
  luaL_register(L, "CourseOverview", CourseOverviewTable);
  lua_pop(L, 1);
}
REGISTER_WITH_LUA_FUNCTION(RegisterCourseOverviewLua);

}  // namespace

/*
 * (c) 2003-2004 Chris Danford
 * All rights reserved.
 */
