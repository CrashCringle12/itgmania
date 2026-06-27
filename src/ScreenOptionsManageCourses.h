#ifndef ScreenOptionsManageCourses_H
#define ScreenOptionsManageCourses_H

#include <string>
#include <vector>

#include "ScreenWithMenuElements.h"

class Course;

// ---------------------------------------------------------------------------
// ManageCourses: engine-side state + actions for the "Manage Courses" flow.
// Themes drive the UI entirely from Lua; this engine module owns the course
// list snapshot, current selection, "create new" allocation, and the
// StepsType / CourseDifficulty preview state.
//
// All mutations broadcast a Message so themes can react:
//   ManageCoursesListChanged       (course list was rebuilt)
//   ManageCoursesSelectionChanged  (current course selection changed)
//   ManageCoursesStepsTypeChanged  (StepsType/CourseDifficulty cycled)
// ---------------------------------------------------------------------------
namespace ManageCourses {

struct CourseInfo {
  std::string sName;      // Display title.
  std::string sPath;      // VFS path; empty for unsaved courses.
  bool bIsMachineCourse;  // Loaded from machine profile?
  int iNumEntries;        // Count of CourseEntry rows.
};

// Returns the current snapshot.  Call Rebuild() to refresh.
const std::vector<CourseInfo>& GetCourses();

// Rebuilds the snapshot from SONGMAN / the machine profile.  Preserves the
// current selection by CourseID when possible.  Broadcasts
// ManageCoursesListChanged.
void Rebuild();

// 1-based index into GetCourses(); 0 if nothing is selected.
int GetSelectedIndex();
void SetSelectedIndex(int iIndex1);

// Allocates a fresh unnamed Course in the machine profile and selects it.
// Returns false with the reason in sErrorOut if the per-profile limit has
// been reached.  Themes call this then transition to GetCreateNewScreen().
bool CreateNewCourse(std::string& sErrorOut);
bool CanCreateMore();

// Sets GAMESTATE->m_pCurCourse from the selection and transitions to the
// configured NextScreen (Course Overview by default).
void OpenSelectedCourse();

// Cycles the (StepsType x CourseDifficulty) preview combination and
// updates the trail.  Broadcasts ManageCoursesStepsTypeChanged.
void CycleStepsType();
std::string GetCurrentStepsType();
std::string GetCurrentCourseDifficulty();

// Theme metric accessors (loaded from ScreenOptionsManageCourses metrics
// during the screen's Init).
std::string GetCreateNewScreen();
std::string GetNextScreen();

// ---------------------------------------------------------------------------
// Engine-owned UI state.  The theme overlay is a pure renderer that reads
// these getters and reacts to broadcast messages; all input handling and
// state mutation lives in the engine (see ScreenOptionsManageCourses::Input).
// ---------------------------------------------------------------------------
enum Focus {
  Focus_List = 0,
  Focus_Tabs,
};
enum TabId {
  Tab_Open = 0,
  Tab_New,
  Tab_Back,
  NUM_TABS,
};

Focus GetFocus();
void SetFocus(Focus f);
int GetTabIndex();  // 1-based
void SetTabIndex(int iIndex1);
int GetTabCount();
TabId GetTabIdAt(int iIndex1);
std::string GetTabLabel(int iIndex1);

// Increments/decrements the selected course index by delta, wrapping.
void AdvanceSelection(int iDelta);
// Increments/decrements the active tab index, wrapping.
void AdvanceTab(int iDelta);
// Runs the action associated with the current tab.
void TriggerCurrentTab();
// Convenience: pops back to the previous screen via the screen's Cancel().
void RequestBack();

// Reset transient UI state for a fresh visit (focus, tab index).
void ResetUI();

}  // namespace ManageCourses

// ---------------------------------------------------------------------------
// ScreenOptionsManageCourses: thin host screen owning all input.  All UI
// rendering lives in the theme overlay.  The screen translates menu/device
// inputs into ManageCourses:: actions and broadcasts so the overlay can
// react.
// ---------------------------------------------------------------------------
class ScreenOptionsManageCourses : public ScreenWithMenuElements {
 public:
  void Init() override;
  void BeginScreen() override;

  bool MenuLeft(const InputEventPlus& input) override;
  bool MenuRight(const InputEventPlus& input) override;
  bool MenuUp(const InputEventPlus& input) override;
  bool MenuDown(const InputEventPlus& input) override;
  bool MenuStart(const InputEventPlus& input) override;
  bool MenuSelect(const InputEventPlus& input) override;
  bool MenuBack(const InputEventPlus& input) override;
};

#endif

/*
 * (c) 2003-2006 Chris Danford, Steve Checkoway
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
