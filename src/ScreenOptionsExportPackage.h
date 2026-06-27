#ifndef ScreenOptionsExportPackage_H
#define ScreenOptionsExportPackage_H

#include <string>
#include <vector>

#include "ScreenMessage.h"
#include "ScreenWithMenuElements.h"

// ---------------------------------------------------------------------------
// ExportPackages: engine-side state + actions for the "Export Packages"
// flow.  Themes drive the UI entirely from Lua; this engine module owns the
// selection list, package metadata, zip job, and progress state.
//
// All mutations broadcast a Message so themes can react:
//   ExportPackageSelectionChanged
//   ExportPackageMetadataChanged
//   ExportPackageStarted
//   ExportPackageProgress
//   ExportPackageFinished
// ---------------------------------------------------------------------------
namespace ExportPackages {

// Flat selection list (full VFS paths, e.g. "Themes/Default",
// "Songs/ITG/I Need You").
std::vector<std::string>& GetSelected();
bool IsSelected(const std::string& sPath);
void ToggleSelected(const std::string& sPath);
void ClearSelected();

// Package metadata that ends up in Manifest.ini at the root of the zip.
struct Metadata {
  std::string sName;
  std::string sAuthor;
  std::string sDescription;
  std::string sVersion;
  std::string sHomepage;
};
Metadata& GetMetadata();
void SetMetadataField(const std::string& sKey, const std::string& sValue);
void ResetMetadata();

// Listing helpers used by the Lua bindings (and the engine zip job).
std::vector<std::string> GetAvailableCategories();
std::vector<std::string> GetItemsInCategory(const std::string& sCategory);
std::vector<std::string> GetSongGroups();
std::vector<std::string> GetSongsInGroup(const std::string& sGroupPath);

// Whole-group select/clear convenience for the Songs category.
void SelectSongGroup(const std::string& sGroupPath);
void DeselectSongGroup(const std::string& sGroupPath);
bool IsSongGroupFullySelected(const std::string& sGroupPath);

// Export action.  Kicks off a background worker that builds Manifest.ini
// and packs everything currently in GetSelected() into a timestamped
// .smzip under Save/Exports/.  Returns true if the worker started OK;
// returns false with the reason in sErrorOut on setup failure.
// Broadcasts ExportPackageStarted immediately, then ExportPackageProgress
// (throttled to once per frame by the screen's Update) and finally
// ExportPackageFinished when the worker is done.
bool StartExport(std::string& sErrorOut);

// Called from ScreenOptionsExportPackage::Update once per frame.  Drains
// thread-safe progress state into the Lua-visible counters, broadcasts
// ExportPackageProgress at most once per frame, and joins the worker +
// broadcasts ExportPackageFinished when the export completes.
void Update(float fDeltaTime);

// Progress query (consumed by the theme's progress overlay).
bool IsExporting();
int GetProgressCurrent();
int GetProgressTotal();
std::string GetProgressFile();

// Final result of the most recent export (populated when
// ExportPackageFinished fires).  GetLastExportError is empty on success.
std::string GetLastExportPath();
std::string GetLastExportError();

// Opens the "Edit Package Info" ScreenMiniMenu on top of whatever screen is
// currently active.  Themes call this from a button handler.
void OpenEditPackageInfoMenu();

}  // namespace ExportPackages

// ---------------------------------------------------------------------------
// ScreenOptionsExportPackage: a thin host screen.  All UI lives in the
// theme overlays.  Existing only to: (a) own the BeginScreen reset of
// metadata, (b) provide a screen for the theme to attach overlays to,
// (c) host the "Edit Package Info" mini-menu callback plumbing.
// ---------------------------------------------------------------------------
class ScreenOptionsExportPackage : public ScreenWithMenuElements {
 public:
  void Init() override;
  void BeginScreen() override;
  void Update(float fDeltaTime) override;
  void HandleScreenMessage(const ScreenMessage SM) override;
};

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
