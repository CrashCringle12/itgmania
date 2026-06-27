#include "ScreenOptionsManageCourses.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

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
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenManager.h"
#include "ScreenMessage.h"
#include "SongManager.h"
#include "Style.h"
#include "ThemeManager.h"
#include "ThemeMetric.h"

REGISTER_SCREEN_CLASS(ScreenOptionsManageCourses);

// ===========================================================================
// ManageCourses module: state + actions invoked by the theme via Lua.
// ===========================================================================
namespace ManageCourses {

static std::vector<CourseInfo> g_vCourses;  // current snapshot
static std::vector<Course*> g_vpCourses;    // engine pointers (parallel)
static int g_iSelectedIndex = 0;            // 1-based; 0 = none
static std::string g_sCreateNewScreen;
static std::string g_sNextScreen;
static EditMode g_EditMode = EditMode_Practice;

// Engine-owned transient UI state.  Reset on every BeginScreen.
static Focus g_Focus = Focus_List;
static int g_iTabIndex = 1;  // 1-based, within the tab strip
static const TabId g_TabOrder[NUM_TABS] = {Tab_Open, Tab_New, Tab_Back};

static void Broadcast(const char* sMsg) {
  if (MESSAGEMAN != nullptr) {
    MESSAGEMAN->Broadcast(sMsg);
  }
}

const std::vector<CourseInfo>& GetCourses() { return g_vCourses; }

int GetSelectedIndex() { return g_iSelectedIndex; }

static void ApplyCurrentSelection() {
  Course* pCourse = nullptr;
  if (g_iSelectedIndex >= 1 &&
      g_iSelectedIndex <= static_cast<int>(g_vpCourses.size())) {
    pCourse = g_vpCourses[g_iSelectedIndex - 1];
  }
  GAMESTATE->m_pCurCourse.Set(pCourse);
  if (pCourse != nullptr) {
    EditCourseUtil::UpdateAndSetTrail();
  } else {
    GAMESTATE->m_pCurTrail[PLAYER_1].Set(nullptr);
  }
}

void SetSelectedIndex(int iIndex1) {
  const int iMax = static_cast<int>(g_vpCourses.size());
  if (iIndex1 < 0) {
    iIndex1 = 0;
  }
  if (iIndex1 > iMax) {
    iIndex1 = iMax;
  }
  if (iIndex1 == g_iSelectedIndex) {
    return;
  }
  g_iSelectedIndex = iIndex1;
  ApplyCurrentSelection();
  Broadcast("ManageCoursesSelectionChanged");
}

// Build the (StepsType, CourseDifficulty) combo list from CommonMetrics.
static void GetStepsTypeCombos(
    std::vector<std::pair<StepsType, CourseDifficulty>>& out) {
  for (const StepsType& st : CommonMetrics::STEPS_TYPES_TO_SHOW.GetValue()) {
    for (const CourseDifficulty& cd :
         CommonMetrics::COURSE_DIFFICULTIES_TO_SHOW.GetValue()) {
      out.push_back(std::make_pair(st, cd));
    }
  }
}

static void EnsureValidStepsTypeCombo() {
  std::vector<std::pair<StepsType, CourseDifficulty>> v;
  GetStepsTypeCombos(v);
  if (v.empty()) {
    return;
  }
  auto cur =
      std::make_pair(GAMESTATE->m_stEdit.Get(), GAMESTATE->m_cdEdit.Get());
  if (std::find(v.begin(), v.end(), cur) == v.end()) {
    GAMESTATE->m_stEdit.Set(v.front().first);
    GAMESTATE->m_cdEdit.Set(v.front().second);
  }
}

void CycleStepsType() {
  std::vector<std::pair<StepsType, CourseDifficulty>> v;
  GetStepsTypeCombos(v);
  if (v.empty()) {
    return;
  }
  auto cur =
      std::make_pair(GAMESTATE->m_stEdit.Get(), GAMESTATE->m_cdEdit.Get());
  auto it = std::find(v.begin(), v.end(), cur);
  if (it == v.end() || ++it == v.end()) {
    it = v.begin();
  }
  GAMESTATE->m_stEdit.Set(it->first);
  GAMESTATE->m_cdEdit.Set(it->second);
  EditCourseUtil::UpdateAndSetTrail();
  Broadcast("ManageCoursesStepsTypeChanged");
}

std::string GetCurrentStepsType() {
  const StepsType st = GAMESTATE->m_stEdit.Get();
  if (st == StepsType_Invalid) {
    return std::string();
  }
  return GAMEMAN->GetStepsTypeInfo(st).GetLocalizedString();
}

std::string GetCurrentCourseDifficulty() {
  const CourseDifficulty cd = GAMESTATE->m_cdEdit.Get();
  if (cd == Difficulty_Invalid) {
    return std::string();
  }
  return CourseDifficultyToLocalizedString(cd);
}

void Rebuild() {
  // Remember which course was selected so we can re-find it after the
  // machine profile reload invalidates pointers.
  CourseID idPrev;
  idPrev.FromCourse(GAMESTATE->m_pCurCourse);

  // Match the original engine flow: flush dir cache + reload machine
  // edits so courses authored elsewhere show up.
  FILEMAN->FlushDirCache();
  PROFILEMAN->LoadMachineProfileEdits();

  g_vpCourses.clear();
  switch (g_EditMode) {
    case EditMode_Home:
      EditCourseUtil::GetAllEditCourses(g_vpCourses);
      break;
    case EditMode_Practice:
    case EditMode_Full:
    default:
      SONGMAN->GetAllCourses(g_vpCourses, false);
      break;
  }

  g_vCourses.clear();
  g_vCourses.reserve(g_vpCourses.size());
  for (const Course* p : g_vpCourses) {
    CourseInfo info;
    info.sName = p->GetDisplayFullTitle();
    info.sPath = p->m_sPath;
    info.bIsMachineCourse =
        (p->GetLoadedFromProfileSlot() == ProfileSlot_Machine);
    info.iNumEntries = static_cast<int>(p->m_vEntries.size());
    g_vCourses.push_back(info);
  }

  // Restore selection.
  g_iSelectedIndex = 0;
  Course* pRestored = idPrev.ToCourse();
  if (pRestored != nullptr) {
    auto it = std::find(g_vpCourses.begin(), g_vpCourses.end(), pRestored);
    if (it != g_vpCourses.end()) {
      g_iSelectedIndex = static_cast<int>(it - g_vpCourses.begin()) + 1;
    }
  }
  if (g_iSelectedIndex == 0 && !g_vpCourses.empty()) {
    g_iSelectedIndex = 1;
  }
  ApplyCurrentSelection();

  Broadcast("ManageCoursesListChanged");
}

bool CanCreateMore() {
  std::vector<Course*> vp;
  EditCourseUtil::GetAllEditCourses(vp);
  return static_cast<int>(vp.size()) < EditCourseUtil::MAX_PER_PROFILE;
}

static LocalizedString MC_YOU_HAVE_MAX(
    "ScreenOptionsManageCourses", "You have %d, the maximum number allowed.");
static LocalizedString MC_YOU_MUST_DELETE(
    "ScreenOptionsManageCourses",
    "You must delete an existing before creating a new.");

bool CreateNewCourse(std::string& sErrorOut) {
  if (!CanCreateMore()) {
    sErrorOut = ssprintf(
        (MC_YOU_HAVE_MAX.GetValue() + "  " + MC_YOU_MUST_DELETE.GetValue())
            .c_str(),
        EditCourseUtil::MAX_PER_PROFILE);
    return false;
  }

  // Mirrors the original "create new" branch in HandleScreenMessage: allocate
  // the Course now, hand ownership to SongManager, mark it as needing a name
  // on first save, and update the trail so the overview/edit screens have
  // something to render.
  Course* pCourse = new Course;
  EditCourseUtil::LoadDefaults(*pCourse);
  pCourse->m_LoadedFromProfile = ProfileSlot_Machine;
  SONGMAN->AddCourse(pCourse);
  GAMESTATE->m_pCurCourse.Set(pCourse);
  EditCourseUtil::s_bNewCourseNeedsName = true;
  EditCourseUtil::UpdateAndSetTrail();
  return true;
}

void OpenSelectedCourse() {
  if (g_iSelectedIndex < 1 ||
      g_iSelectedIndex > static_cast<int>(g_vpCourses.size())) {
    return;
  }
  GAMESTATE->m_pCurCourse.Set(g_vpCourses[g_iSelectedIndex - 1]);
  EditCourseUtil::s_bNewCourseNeedsName = false;
  EditCourseUtil::UpdateAndSetTrail();
  if (!g_sNextScreen.empty() && SCREENMAN != nullptr) {
    SCREENMAN->SetNewScreen(g_sNextScreen);
  }
}

std::string GetCreateNewScreen() { return g_sCreateNewScreen; }
std::string GetNextScreen() { return g_sNextScreen; }

// ---------------------------------------------------------------------------
// Engine-owned UI state implementation.
// ---------------------------------------------------------------------------
Focus GetFocus() { return g_Focus; }

void SetFocus(Focus f) {
  if (g_Focus == f) {
    return;
  }
  g_Focus = f;
  Broadcast("ManageCoursesFocusChanged");
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
  Broadcast("ManageCoursesTabChanged");
}

int GetTabCount() { return NUM_TABS; }

TabId GetTabIdAt(int iIndex1) {
  if (iIndex1 < 1 || iIndex1 > NUM_TABS) {
    return Tab_Open;
  }
  return g_TabOrder[iIndex1 - 1];
}

static LocalizedString MC_TAB_OPEN("ScreenOptionsManageCourses", "Open");
static LocalizedString MC_TAB_NEW("ScreenOptionsManageCourses", "New Course");
static LocalizedString MC_TAB_BACK("ScreenOptionsManageCourses", "Back");
static LocalizedString MC_MAX_REACHED(
    "ScreenOptionsManageCourses",
    "Maximum number of courses reached. Delete one first.");

std::string GetTabLabel(int iIndex1) {
  switch (GetTabIdAt(iIndex1)) {
    case Tab_Open:
      return MC_TAB_OPEN.GetValue();
    case Tab_New:
      return MC_TAB_NEW.GetValue();
    case Tab_Back:
      return MC_TAB_BACK.GetValue();
    default:
      return std::string();
  }
}

void AdvanceSelection(int iDelta) {
  const int n = static_cast<int>(g_vpCourses.size());
  if (n <= 0) {
    return;
  }
  int next = g_iSelectedIndex + iDelta;
  // Wrap.  GetSelectedIndex() may be 0 when empty; treat 0 as "before 1".
  if (next < 1) {
    next = n;
  }
  if (next > n) {
    next = 1;
  }
  SetSelectedIndex(next);
}

void AdvanceTab(int iDelta) {
  int next = g_iTabIndex + iDelta;
  if (next < 1) {
    next = NUM_TABS;
  }
  if (next > NUM_TABS) {
    next = 1;
  }
  SetTabIndex(next);
}

void RequestBack() {
  if (SCREENMAN == nullptr) {
    return;
  }
  Screen* pTop = SCREENMAN->GetTopScreen();
  if (pTop != nullptr) {
    pTop->PostScreenMessage(SM_GoToPrevScreen, 0);
  }
}

void TriggerCurrentTab() {
  switch (GetTabIdAt(g_iTabIndex)) {
    case Tab_Open: {
      OpenSelectedCourse();
      break;
    }
    case Tab_New: {
      std::string sErr;
      if (CreateNewCourse(sErr)) {
        if (!g_sCreateNewScreen.empty() && SCREENMAN != nullptr) {
          SCREENMAN->SetNewScreen(g_sCreateNewScreen);
        }
      } else if (SCREENMAN != nullptr) {
        SCREENMAN->SystemMessage(sErr);
      }
      break;
    }
    case Tab_Back: {
      RequestBack();
      break;
    }
    default:
      break;
  }
}

void ResetUI() {
  g_Focus = Focus_List;
  g_iTabIndex = 1;
}

}  // namespace ManageCourses

// ===========================================================================
// ScreenOptionsManageCourses: thin host screen.
// ===========================================================================
void ScreenOptionsManageCourses::Init() {
  if (PREFSMAN->m_iArcadeOptionsNavigation) {
    // No-op for the thin screen; kept so existing arcade preference still
    // means "Arcade-style navigation" if the theme cares.
  }
  ScreenWithMenuElements::Init();

  // Pick up theme metrics into the ManageCourses namespace so Lua callers
  // can drive screen transitions without knowing the metric names.
  ThemeMetric<EditMode> editMode(m_sName, "EditMode");
  ThemeMetric<std::string> createNew(m_sName, "CreateNewScreen");
  ThemeMetric<std::string> nextScreen(m_sName, "NextScreen");
  ManageCourses::g_EditMode = editMode.GetValue();
  ManageCourses::g_sCreateNewScreen = createNew.GetValue();
  ManageCourses::g_sNextScreen = nextScreen.GetValue();
}

void ScreenOptionsManageCourses::BeginScreen() {
  // Establish a default style so the (StepsType, CourseDifficulty) preview
  // has a valid game context to work against.  Matches the legacy flow.
  std::vector<const Style*> vpStyles;
  GAMEMAN->GetStylesForGame(GAMESTATE->m_pCurGame, vpStyles);
  if (!vpStyles.empty()) {
    GAMESTATE->SetCurrentStyle(vpStyles[0], PLAYER_INVALID);
  }
  ManageCourses::EnsureValidStepsTypeCombo();

  ManageCourses::ResetUI();
  ManageCourses::Rebuild();

  ScreenWithMenuElements::BeginScreen();
}

// ---------------------------------------------------------------------------
// Input: keymap lives in the engine.  MenuLeft/Right scroll the focused
// zone; MenuUp/Down toggle the focus zone (list <-> tab strip); Start
// triggers the focused element; Select cycles the StepsType preview;
// Back exits to the previous screen.
// ---------------------------------------------------------------------------
bool ScreenOptionsManageCourses::MenuLeft(const InputEventPlus& /* input */) {
  using namespace ManageCourses;
  if (GetFocus() == Focus_List) {
    AdvanceSelection(-1);
  } else {
    AdvanceTab(-1);
  }
  return true;
}

bool ScreenOptionsManageCourses::MenuRight(const InputEventPlus& /* input */) {
  using namespace ManageCourses;
  if (GetFocus() == Focus_List) {
    AdvanceSelection(+1);
  } else {
    AdvanceTab(+1);
  }
  return true;
}

bool ScreenOptionsManageCourses::MenuUp(const InputEventPlus& /* input */) {
  using namespace ManageCourses;
  SetFocus(GetFocus() == Focus_List ? Focus_Tabs : Focus_List);
  return true;
}

bool ScreenOptionsManageCourses::MenuDown(const InputEventPlus& input) {
  return MenuUp(input);
}

bool ScreenOptionsManageCourses::MenuStart(const InputEventPlus& /* input */) {
  using namespace ManageCourses;
  if (GetFocus() == Focus_List) {
    OpenSelectedCourse();
  } else {
    TriggerCurrentTab();
  }
  return true;
}

bool ScreenOptionsManageCourses::MenuSelect(const InputEventPlus& /* input */) {
  ManageCourses::CycleStepsType();
  return true;
}

bool ScreenOptionsManageCourses::MenuBack(const InputEventPlus& /* input */) {
  if (ManageCourses::GetFocus() == ManageCourses::Focus_Tabs) {
    ManageCourses::SetFocus(ManageCourses::Focus_List);
    return true;
  }
  Cancel(SM_GoToPrevScreen);
  return true;
}

// ===========================================================================
// Lua bindings.  Themes use ManageCourses.* to drive the whole UI.
// ===========================================================================
namespace {

int LuaGetCourses(lua_State* L) {
  const auto& v = ManageCourses::GetCourses();
  lua_createtable(L, static_cast<int>(v.size()), 0);
  for (std::size_t i = 0; i < v.size(); ++i) {
    lua_createtable(L, 0, 4);
    lua_pushstring(L, v[i].sName.c_str());
    lua_setfield(L, -2, "Name");
    lua_pushstring(L, v[i].sPath.c_str());
    lua_setfield(L, -2, "Path");
    lua_pushboolean(L, v[i].bIsMachineCourse ? 1 : 0);
    lua_setfield(L, -2, "IsMachineCourse");
    lua_pushinteger(L, v[i].iNumEntries);
    lua_setfield(L, -2, "NumEntries");
    lua_rawseti(L, -2, static_cast<int>(i + 1));
  }
  return 1;
}

int LuaGetSelectedIndex(lua_State* L) {
  lua_pushinteger(L, ManageCourses::GetSelectedIndex());
  return 1;
}

int LuaSetSelectedIndex(lua_State* L) {
  ManageCourses::SetSelectedIndex(IArg(1));
  return 0;
}

int LuaCanCreateMore(lua_State* L) {
  lua_pushboolean(L, ManageCourses::CanCreateMore() ? 1 : 0);
  return 1;
}

// Returns (true, nil) on success or (nil, errorString) on failure.  Themes
// follow up with SCREENMAN:SetNewScreen(ManageCourses.GetCreateNewScreen())
// when this succeeds.
int LuaCreateNewCourse(lua_State* L) {
  std::string sErr;
  if (ManageCourses::CreateNewCourse(sErr)) {
    lua_pushboolean(L, 1);
    lua_pushnil(L);
  } else {
    lua_pushnil(L);
    lua_pushstring(L, sErr.c_str());
  }
  return 2;
}

int LuaOpenSelectedCourse(lua_State* /* L */) {
  ManageCourses::OpenSelectedCourse();
  return 0;
}

int LuaCycleStepsType(lua_State* /* L */) {
  ManageCourses::CycleStepsType();
  return 0;
}

int LuaGetCurrentStepsType(lua_State* L) {
  lua_pushstring(L, ManageCourses::GetCurrentStepsType().c_str());
  return 1;
}

int LuaGetCurrentCourseDifficulty(lua_State* L) {
  lua_pushstring(L, ManageCourses::GetCurrentCourseDifficulty().c_str());
  return 1;
}

int LuaGetCreateNewScreen(lua_State* L) {
  lua_pushstring(L, ManageCourses::GetCreateNewScreen().c_str());
  return 1;
}

int LuaGetNextScreen(lua_State* L) {
  lua_pushstring(L, ManageCourses::GetNextScreen().c_str());
  return 1;
}

int LuaGetFocus(lua_State* L) {
  lua_pushstring(
      L,
      ManageCourses::GetFocus() == ManageCourses::Focus_Tabs ? "tabs" : "list");
  return 1;
}

int LuaGetTabIndex(lua_State* L) {
  lua_pushinteger(L, ManageCourses::GetTabIndex());
  return 1;
}

int LuaGetTabs(lua_State* L) {
  const int n = ManageCourses::GetTabCount();
  lua_createtable(L, n, 0);
  for (int i = 1; i <= n; ++i) {
    lua_createtable(L, 0, 2);
    const ManageCourses::TabId id = ManageCourses::GetTabIdAt(i);
    const char* sKey = "open";
    switch (id) {
      case ManageCourses::Tab_Open:
        sKey = "open";
        break;
      case ManageCourses::Tab_New:
        sKey = "new";
        break;
      case ManageCourses::Tab_Back:
        sKey = "back";
        break;
      default:
        break;
    }
    lua_pushstring(L, sKey);
    lua_setfield(L, -2, "Id");
    lua_pushstring(L, ManageCourses::GetTabLabel(i).c_str());
    lua_setfield(L, -2, "Label");
    lua_rawseti(L, -2, i);
  }
  return 1;
}

const luaL_Reg ManageCoursesTable[] = {
    {"GetCourses", LuaGetCourses},
    {"GetSelectedIndex", LuaGetSelectedIndex},
    {"SetSelectedIndex", LuaSetSelectedIndex},
    {"CanCreateMore", LuaCanCreateMore},
    {"CreateNewCourse", LuaCreateNewCourse},
    {"OpenSelectedCourse", LuaOpenSelectedCourse},
    {"CycleStepsType", LuaCycleStepsType},
    {"GetCurrentStepsType", LuaGetCurrentStepsType},
    {"GetCurrentCourseDifficulty", LuaGetCurrentCourseDifficulty},
    {"GetCreateNewScreen", LuaGetCreateNewScreen},
    {"GetNextScreen", LuaGetNextScreen},
    {"GetFocus", LuaGetFocus},
    {"GetTabIndex", LuaGetTabIndex},
    {"GetTabs", LuaGetTabs},
    {nullptr, nullptr},
};

void RegisterManageCoursesLua(lua_State* L) {
  luaL_register(L, "ManageCourses", ManageCoursesTable);
  lua_pop(L, 1);
}
REGISTER_WITH_LUA_FUNCTION(RegisterManageCoursesLua);

}  // namespace

/*
 * (c) 2002-2004 Chris Danford
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
