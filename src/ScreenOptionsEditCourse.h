#ifndef ScreenOptionsEditCourse_H
#define ScreenOptionsEditCourse_H

#include <string>
#include <vector>

#include "ScreenWithMenuElements.h"

class ScreenOptionsEditCourse : public ScreenWithMenuElements {
 public:
  void Init() override;
  void BeginScreen() override;
};

// ---------------------------------------------------------------------------
// EditCourse: full read/write model for the course currently held in
// GAMESTATE->m_pCurCourse.  All mutations broadcast Messages so the theme can
// react.  The screen itself is intentionally thin -- the UI lives in Lua.
// ---------------------------------------------------------------------------
namespace EditCourse {

// Mirrors CourseEntry but presented as a flat table for Lua.
struct EntryView {
  // "Fixed", "PureRandom", "GroupRandom", "Best", "Worst", "GradeBest",
  // "GradeWorst", "SongSelect"
  std::string sSelectorKind;
  // Resolved song info (for Fixed entries) and authored selector input.
  std::string sSongTitle;
  std::string sSongSubtitle;
  std::string sSongGroup;       // group name for GroupRandom or Fixed (resolved)
  std::string sSongPath;        // empty if unresolved
  int iChooseIndex = 0;         // 1-based, for Best/Worst/GradeBest/GradeWorst
  std::string sDifficulty;      // localized name, "" if range or any
  int iLowMeter = -1;
  int iHighMeter = -1;
  bool bSecret = false;
  bool bNoDifficult = false;
  int iGainLives = -1;
  float fGainSeconds = 0.f;
  std::string sMods;
  int iNumAttacks = 0;          // count only; full list via GetEntryAttacks
  bool bHasSongSelectFilters = false;
};

struct AttackView {
  float fStart = 0.f;
  float fLength = 0.f;
  std::string sMods;
};

// ---- Course-level properties ----------------------------------------------
bool HasCourse();
std::string GetTitle();
void SetTitle(const std::string& s);
std::string GetSubtitle();
void SetSubtitle(const std::string& s);
std::string GetScripter();
void SetScripter(const std::string& s);
std::string GetDescription();
void SetDescription(const std::string& s);
bool GetRepeat();
void SetRepeat(bool b);
bool GetShuffle();
void SetShuffle(bool b);
int GetLives();
void SetLives(int n);
float GetGoalSeconds();
void SetGoalSeconds(float f);
int GetCustomMeter(int cd);            // CourseDifficulty enum value
void SetCustomMeter(int cd, int meter);
std::vector<std::string> GetStyles();
void AddStyle(const std::string& s);
void RemoveStyle(const std::string& s);
std::vector<std::string> GetAvailableStyles();

// ---- Entry list -----------------------------------------------------------
int GetEntryCount();
EntryView GetEntry(int i1);
int AddEntry();
bool RemoveEntry(int i1);
bool MoveEntry(int from1, int to1);
int DuplicateEntry(int i1);
int GetSelectedEntryIndex();
void SetSelectedEntryIndex(int i1);

// ---- Per-entry mutators ---------------------------------------------------
bool SetEntrySelectorKind(int i1, const std::string& sKind);
// pSong may be nullptr to clear; group/title are also accepted.
bool SetEntrySongByPath(int i1, const std::string& sGroup, const std::string& sTitle);
bool SetEntryGroup(int i1, const std::string& sGroup);
bool SetEntryChooseIndex(int i1, int iIndex1);
bool SetEntryDifficulty(int i1, const std::string& sDifficulty);  // "" = any
bool SetEntryMeterRange(int i1, int iLow, int iHigh);
bool SetEntrySecret(int i1, bool b);
bool SetEntryNoDifficult(int i1, bool b);
bool SetEntryGainLives(int i1, int n);
bool SetEntryGainSeconds(int i1, float f);
bool SetEntryMods(int i1, const std::string& sMods);

// Timed attacks editor (entry.attacks).
std::vector<AttackView> GetEntryAttacks(int i1);
bool SetEntryAttacks(int i1, const std::vector<AttackView>& v);

// SONGSELECT filter editor (entry.songCriteria + stepsCriteria, when
// bUseSongSelect is true).  We expose a small, focused subset and round-trip
// everything else opaquely so hand-authored .crs files don't lose data.
struct SongSelectView {
  std::vector<std::string> titles;
  std::vector<std::string> groups;
  std::vector<std::string> artists;
  std::vector<std::string> genres;
  float fMinBPM = -1.f;
  float fMaxBPM = -1.f;
  float fMinDuration = -1.f;
  float fMaxDuration = -1.f;
  int iMinMeter = -1;
  int iMaxMeter = -1;
  std::vector<std::string> difficulties;  // string names
  std::string sSortKind;                  // "" = none
  int iSortIndex = 0;                     // 1-based
};
SongSelectView GetEntrySongSelect(int i1);
bool SetEntrySongSelect(int i1, const SongSelectView& v);

// ---- Helpers / catalog ----------------------------------------------------
std::vector<std::string> GetSelectorKinds();
std::vector<std::string> GetDifficulties();        // localized
std::vector<std::string> GetCourseDifficulties();  // localized
std::vector<std::string> GetSortKinds();           // localized SongSort
std::vector<std::string> GetAllGroups();
// Returns {Title, Subtitle, Group} for each song matching the legacy filter
// (no autogen / no tutorial / unlocked).  Sorted by title.
struct SongRow {
  std::string sTitle;
  std::string sSubtitle;
  std::string sGroup;
  std::string sPath;
};
std::vector<SongRow> GetSongsInGroup(const std::string& sGroup);
std::vector<SongRow> GetAllSongs();

struct ModCategory {
  std::string sName;
  std::vector<std::string> mods;
};
std::vector<ModCategory> GetModCategories();
// Splits a comma-separated mod string into trimmed tokens.
std::vector<std::string> ParseModList(const std::string& s);
std::string JoinModList(const std::vector<std::string>& v);

// ---- Actions / save lifecycle --------------------------------------------
bool IsDirty();
void MarkDirty();
void MarkClean();
bool Save(std::string& sErrorOut);
bool NeedsName();
bool Rename(const std::string& sName, std::string& sErrorOut);
void TestPlay();
void ReturnToPrev();
std::string ValidateName(const std::string& sName);
int GetMaxNameLength();

}  // namespace EditCourse

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
