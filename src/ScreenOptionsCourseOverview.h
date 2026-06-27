#ifndef ScreenOptionsCourseOverview_H
#define ScreenOptionsCourseOverview_H

#include <string>
#include <vector>

#include "ScreenWithMenuElements.h"

class ScreenOptionsCourseOverview : public ScreenWithMenuElements {
 public:
  void Init() override;
  void BeginScreen() override;
  void HandleScreenMessage(const ScreenMessage SM) override;

  bool MenuLeft(const InputEventPlus& input) override;
  bool MenuRight(const InputEventPlus& input) override;
  bool MenuUp(const InputEventPlus& input) override;
  bool MenuDown(const InputEventPlus& input) override;
  bool MenuStart(const InputEventPlus& input) override;
  bool MenuSelect(const InputEventPlus& input) override;
  bool MenuBack(const InputEventPlus& input) override;
};

// ---------------------------------------------------------------------------
// CourseOverview: namespace module exposed to Lua so themes can render the
// course overview / edit hub without going through ScreenOptions rows.
// ---------------------------------------------------------------------------
namespace CourseOverview {

struct CourseEntryInfo {
  std::string sSongTitle;
  std::string sSongSubtitle;
  std::string sDifficulty;  // localized
  int iLowMeter = -1;
  int iHighMeter = -1;
  bool bSecret = false;
};

// Snapshot of the current course (refreshed by Rebuild).
bool HasCourse();
std::string GetTitle();
std::string GetPath();
bool IsMachineCourse();
int GetNumEntries();
std::string GetStepsType();         // localized
std::string GetCourseDifficulty();  // localized
float GetTotalSeconds();
const std::vector<CourseEntryInfo>& GetEntries();

// Actions: each returns true on success.  Theme is expected to call Rebuild
// indirectly through the broadcast messages or after a Save.
void Play();
void Edit();
void Shuffle();
bool Save(std::string& sErrorOut);
bool NeedsName();
bool Rename(const std::string& sNewName, std::string& sErrorOut);
bool Delete(std::string& sErrorOut);
std::string ValidateName(const std::string& sName);
int GetMaxNameLength();

// Theme metric accessors so Lua can transition without knowing metric keys.
std::string GetPlayScreen();
std::string GetEditScreen();
std::string GetPrevScreen();

// Re-snapshot the entry list from GAMESTATE->m_pCurCourse.
void Rebuild();

// ---------------------------------------------------------------------------
// Engine-owned UI state.  The theme overlay is a pure renderer; all input
// handling and state mutation lives in the screen class (see Input/Menu*).
// ---------------------------------------------------------------------------
enum Focus {
  Focus_List = 0,
  Focus_Tabs,
};
enum TabId {
  Tab_Play = 0,
  Tab_Edit,
  Tab_Shuffle,
  Tab_Rename,
  Tab_Delete,
  Tab_Save,
  Tab_Back,
  NUM_TABS,
};

Focus GetFocus();
void SetFocus(Focus f);
int GetTabIndex();                       // 1-based
void SetTabIndex(int iIndex1);
int GetTabCount();
TabId GetTabIdAt(int iIndex1);
std::string GetTabLabel(int iIndex1);

int GetWheelPos();                       // 1-based scroll position in list
void SetWheelPos(int iPos1);
void AdvanceWheel(int iDelta);

void AdvanceTab(int iDelta);
void TriggerCurrentTab();
void ResetUI();

}  // namespace CourseOverview

#endif

/*
 * (c) 2003-2004 Chris Danford
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
