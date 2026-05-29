#include "ScreenNFCLinkProfile.h"

#include <string>
#include <vector>

#include "ActorUtil.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "LuaBinding.h"
#include "MessageManager.h"
#include "NFCManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "ScreenManager.h"
#include "ThemeManager.h"

REGISTER_SCREEN_CLASS(ScreenNFCLinkProfile);

static LocalizedString LINK_NFC_TITLE("ScreenNFCLinkProfile", "Title");
static LocalizedString LINK_NFC_WAITING(
    "ScreenNFCLinkProfile", "WaitingForCardTap");
static LocalizedString LINK_NFC_DETECTED(
    "ScreenNFCLinkProfile", "CardDetected");
static LocalizedString LINK_NFC_LINKED("ScreenNFCLinkProfile", "CardLinked");
static LocalizedString LINK_NFC_FAILED("ScreenNFCLinkProfile", "LinkFailed");
static LocalizedString LINK_NFC_UNAVAILABLE(
    "ScreenNFCLinkProfile", "NFCUnavailable");
static LocalizedString LINK_NFC_UID_LABEL("ScreenNFCLinkProfile", "UIDLabel");
static LocalizedString LINK_NFC_READER_LABEL(
    "ScreenNFCLinkProfile", "ReaderLabel");
static LocalizedString LINK_NFC_READER_UNKNOWN(
    "ScreenNFCLinkProfile", "ReaderUnknown");
static LocalizedString LINK_NFC_PENDING_CONFIRM(
    "ScreenNFCLinkProfile", "ConfirmLink");
static LocalizedString LINK_NFC_PENDING_MOVE(
    "ScreenNFCLinkProfile", "ConfirmMove");
static LocalizedString LINK_NFC_PENDING_CANCELLED(
    "ScreenNFCLinkProfile", "LinkCancelled");
static LocalizedString LINK_NFC_INSTRUCTIONS(
    "ScreenNFCLinkProfile", "PressStartBack");
static LocalizedString LINK_NFC_UID_CURRENT(
    "ScreenNFCLinkProfile", "UIDCurrent");
static LocalizedString LINK_NFC_UID_LAST_TAPPED(
    "ScreenNFCLinkProfile", "UIDLastTapped");
static LocalizedString LINK_NFC_LINKED_CARD_LABEL(
    "ScreenNFCLinkProfile", "LinkedCardLabel");
static LocalizedString LINK_NFC_NO_LINKED_CARD(
    "ScreenNFCLinkProfile", "NoLinkedCard");

void ScreenNFCLinkProfile::Init() {
  ScreenWithMenuElements::Init();

  m_sProfileID = GAMESTATE->m_sEditLocalProfileID;

  if (NFCMAN != nullptr) {
    SubscribeToMessage(Message_NFCCardTapped);
    SubscribeToMessage(Message_NFCCardRemoved);
  }

  m_textTitle.SetName("Title");
  m_textTitle.LoadFromFont(THEME->GetPathF("Common", "normal"));
  m_textTitle.SetText(LINK_NFC_TITLE.GetValue());
  LOAD_ALL_COMMANDS_AND_SET_XY(m_textTitle);
  AddChild(&m_textTitle);

  m_textStatus.SetName("Status");
  m_textStatus.LoadFromFont(THEME->GetPathF("Common", "normal"));
  m_textStatus.SetText("");
  LOAD_ALL_COMMANDS_AND_SET_XY(m_textStatus);
  AddChild(&m_textStatus);

  m_textUID.SetName("UID");
  m_textUID.LoadFromFont(THEME->GetPathF("Common", "normal"));
  m_textUID.SetText("");
  LOAD_ALL_COMMANDS_AND_SET_XY(m_textUID);
  AddChild(&m_textUID);

  m_textLinkedCard.SetName("LinkedCard");
  m_textLinkedCard.LoadFromFont(THEME->GetPathF("Common", "normal"));
  m_textLinkedCard.SetText("");
  LOAD_ALL_COMMANDS_AND_SET_XY(m_textLinkedCard);
  AddChild(&m_textLinkedCard);

  m_textReader.SetName("Reader");
  m_textReader.LoadFromFont(THEME->GetPathF("Common", "normal"));
  m_textReader.SetText("");
  LOAD_ALL_COMMANDS_AND_SET_XY(m_textReader);
  AddChild(&m_textReader);

  m_textInstructions.SetName("Instructions");
  m_textInstructions.LoadFromFont(THEME->GetPathF("Common", "normal"));
  m_textInstructions.SetText(LINK_NFC_INSTRUCTIONS.GetValue());
  LOAD_ALL_COMMANDS_AND_SET_XY(m_textInstructions);
  AddChild(&m_textInstructions);

  RefreshDisplay();
}

void ScreenNFCLinkProfile::Update(float fDeltaTime) {
  Screen::Update(fDeltaTime);
  RefreshDisplay();
}

void ScreenNFCLinkProfile::HandleMessage(const Message& msg) {
  if (msg.GetName() == MessageIDToString(Message_NFCCardTapped)) {
    std::string sUID;
    if (msg.GetParam("UID", sUID) && !sUID.empty()) {
      StageUIDForLink(sUID);
    }
    RefreshDisplay();
  } else if (msg.GetName() == MessageIDToString(Message_NFCCardRemoved)) {
    RefreshDisplay();
  }

  ScreenWithMenuElements::HandleMessage(msg);
}

bool ScreenNFCLinkProfile::MenuStart(const InputEventPlus& input) {
  if (HasPendingLink()) {
    return ConfirmPendingLink(true);
  }
  return MenuBack(input);
}

bool ScreenNFCLinkProfile::MenuBack(const InputEventPlus&) {
  if (IsTransitioning()) {
    return false;
  }
  if (HasPendingLink()) {
    CancelPendingLink();
    return true;
  }
  SCREENMAN->PlayStartSound();
  StartTransitioningScreen(SM_GoToPrevScreen);
  return true;
}

void ScreenNFCLinkProfile::RefreshDisplay() {
  if (NFCMAN == nullptr || !NFCMAN->IsEnabled()) {
    m_textStatus.SetText(LINK_NFC_UNAVAILABLE.GetValue());
    m_textUID.SetText(LINK_NFC_UID_LABEL.GetValue() + ": ----");
    m_textLinkedCard.SetText(
        LINK_NFC_LINKED_CARD_LABEL.GetValue() + ": " +
        LINK_NFC_NO_LINKED_CARD.GetValue());
    m_textReader.SetText(
        LINK_NFC_READER_LABEL.GetValue() + ": " +
        LINK_NFC_READER_UNKNOWN.GetValue());
    return;
  }

  const bool bCardPresent = NFCMAN->IsCardPresent();
  const std::string sCurrentUID = NFCMAN->GetCurrentCardUID();
  const std::string sLastTappedUID = NFCMAN->GetLastTappedUID();

  if (!m_sErrorStatus.empty()) {
    m_textStatus.SetText(m_sErrorStatus);
  } else if (HasPendingLink()) {
    if (m_bPendingMove) {
      std::string sName = m_sPendingSourceProfileName;
      if (sName.empty()) {
        sName = m_sPendingSourceProfileID;
      }
      m_textStatus.SetText(
          ssprintf(LINK_NFC_PENDING_MOVE.GetValue().c_str(), sName.c_str()));
    } else {
      m_textStatus.SetText(LINK_NFC_PENDING_CONFIRM.GetValue());
    }
  } else if (bCardPresent) {
    m_textStatus.SetText(LINK_NFC_DETECTED.GetValue());
  } else if (m_bTappedCard) {
    m_textStatus.SetText(LINK_NFC_LINKED.GetValue());
  } else {
    m_textStatus.SetText(LINK_NFC_WAITING.GetValue());
  }

  // UID display: show current card with "Current" label, fall back to last
  // tapped with "Last Tapped" label, or "----" if neither is known.
  if (bCardPresent && !sCurrentUID.empty()) {
    m_textUID.SetText(
        LINK_NFC_UID_LABEL.GetValue() + " (" + LINK_NFC_UID_CURRENT.GetValue() +
        "): " + sCurrentUID);
  } else if (!sLastTappedUID.empty()) {
    m_textUID.SetText(
        LINK_NFC_UID_LABEL.GetValue() + " (" +
        LINK_NFC_UID_LAST_TAPPED.GetValue() + "): " + sLastTappedUID);
  } else {
    m_textUID.SetText(LINK_NFC_UID_LABEL.GetValue() + ": ----");
  }

  // Show the card already linked to this profile, if any.
  const Profile* pProfile = PROFILEMAN->GetLocalProfile(m_sProfileID);
  const std::string sLinkedUID =
      (pProfile != nullptr) ? pProfile->m_sNFCCardUID : "";
  if (!sLinkedUID.empty()) {
    m_textLinkedCard.SetText(
        LINK_NFC_LINKED_CARD_LABEL.GetValue() + ": " + sLinkedUID);
  } else {
    m_textLinkedCard.SetText(
        LINK_NFC_LINKED_CARD_LABEL.GetValue() + ": " +
        LINK_NFC_NO_LINKED_CARD.GetValue());
  }

  std::vector<std::string> vsReaders = NFCMAN->GetReaderNames();
  std::string sReaderName = LINK_NFC_READER_UNKNOWN.GetValue();
  if (!vsReaders.empty()) {
    sReaderName = vsReaders[0];
  }
  m_textReader.SetText(LINK_NFC_READER_LABEL.GetValue() + ": " + sReaderName);
}

bool ScreenNFCLinkProfile::HasPendingLink() const {
  return !m_sPendingUID.empty();
}

std::string ScreenNFCLinkProfile::GetPendingCardUID() const {
  return m_sPendingUID;
}

bool ScreenNFCLinkProfile::PendingLinkRequiresMove() const {
  return HasPendingLink() && m_bPendingMove;
}

std::string ScreenNFCLinkProfile::GetPendingSourceProfileID() const {
  return m_sPendingSourceProfileID;
}

std::string ScreenNFCLinkProfile::GetPendingSourceProfileName() const {
  return m_sPendingSourceProfileName;
}

void ScreenNFCLinkProfile::StageUIDForLink(const std::string& sUID) {
  m_sErrorStatus.clear();
  m_bTappedCard = false;

  if (sUID.empty()) {
    return;
  }

  m_sPendingUID = sUID;
  m_sPendingSourceProfileID.clear();
  m_sPendingSourceProfileName.clear();
  m_bPendingMove = false;

  const int iExistingProfile = PROFILEMAN->GetLocalProfileIndexByNFCUID(sUID);
  if (iExistingProfile < 0) {
    return;
  }

  const std::string sExistingProfileID =
      PROFILEMAN->GetLocalProfileIDFromIndex(iExistingProfile);
  if (sExistingProfileID == m_sProfileID) {
    return;
  }

  Profile* pExistingProfile = PROFILEMAN->GetLocalProfile(sExistingProfileID);
  if (pExistingProfile == nullptr) {
    return;
  }

  m_bPendingMove = true;
  m_sPendingSourceProfileID = sExistingProfileID;
  m_sPendingSourceProfileName = pExistingProfile->m_sDisplayName;
}

bool ScreenNFCLinkProfile::ConfirmPendingLink(bool bMoveExisting) {
  if (!HasPendingLink()) {
    return false;
  }
  if (!LinkUIDToProfile(m_sPendingUID, bMoveExisting)) {
    return false;
  }
  SCREENMAN->PlayStartSound();
  return true;
}

void ScreenNFCLinkProfile::CancelPendingLink() {
  ClearPendingLink();
  m_sErrorStatus = LINK_NFC_PENDING_CANCELLED.GetValue();
}

void ScreenNFCLinkProfile::ClearPendingLink() {
  m_sPendingUID.clear();
  m_sPendingSourceProfileID.clear();
  m_sPendingSourceProfileName.clear();
  m_bPendingMove = false;
}

bool ScreenNFCLinkProfile::LinkUIDToProfile(
    const std::string& sUID, bool bMoveExisting) {
  m_sErrorStatus.clear();

  if (sUID.empty()) {
    return false;
  }

  Profile* pProfile = PROFILEMAN->GetLocalProfile(m_sProfileID);
  if (pProfile == nullptr) {
    m_sErrorStatus = LINK_NFC_FAILED.GetValue();
    return false;
  }

  const std::string sOldCurrentUID = pProfile->m_sNFCCardUID;

  Profile* pExistingProfile = nullptr;
  if (m_bPendingMove && !m_sPendingSourceProfileID.empty()) {
    pExistingProfile = PROFILEMAN->GetLocalProfile(m_sPendingSourceProfileID);
    if (pExistingProfile == nullptr) {
      m_sErrorStatus = LINK_NFC_FAILED.GetValue();
      return false;
    }
    if (!bMoveExisting) {
      m_sErrorStatus = LINK_NFC_PENDING_CANCELLED.GetValue();
      return false;
    }
  }

  std::string sExistingUID;
  if (pExistingProfile != nullptr) {
    sExistingUID = pExistingProfile->m_sNFCCardUID;
    pExistingProfile->m_sNFCCardUID.clear();
    if (!PROFILEMAN->SaveLocalProfile(m_sPendingSourceProfileID)) {
      pExistingProfile->m_sNFCCardUID = sExistingUID;
      m_sErrorStatus = LINK_NFC_FAILED.GetValue();
      return false;
    }
  }

  pProfile->m_sNFCCardUID = sUID;
  if (!PROFILEMAN->SaveLocalProfile(m_sProfileID)) {
    pProfile->m_sNFCCardUID = sOldCurrentUID;
    if (pExistingProfile != nullptr) {
      pExistingProfile->m_sNFCCardUID = sExistingUID;
      PROFILEMAN->SaveLocalProfile(m_sPendingSourceProfileID);
    }
    m_sErrorStatus = LINK_NFC_FAILED.GetValue();
    return false;
  }

  m_bTappedCard = true;
  m_sLastLinkedUID = sUID;
  ClearPendingLink();
  return true;
}

std::string ScreenNFCLinkProfile::GetLinkedCardUID() const {
  const Profile* pProfile = PROFILEMAN->GetLocalProfile(m_sProfileID);
  if (pProfile == nullptr) {
    return "";
  }
  return pProfile->m_sNFCCardUID;
}

bool ScreenNFCLinkProfile::IsCardLinked() const {
  return !GetLinkedCardUID().empty();
}

class LunaScreenNFCLinkProfile : public Luna<ScreenNFCLinkProfile> {
 public:
  static int HasPendingLink(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->HasPendingLink());
    return 1;
  }

  static int GetPendingCardUID(T* p, lua_State* L) {
    const std::string sUID = p->GetPendingCardUID();
    if (sUID.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, sUID.c_str());
    }
    return 1;
  }

  static int PendingLinkRequiresMove(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->PendingLinkRequiresMove());
    return 1;
  }

  static int GetPendingSourceProfileID(T* p, lua_State* L) {
    const std::string sProfileID = p->GetPendingSourceProfileID();
    if (sProfileID.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, sProfileID.c_str());
    }
    return 1;
  }

  static int GetPendingSourceProfileName(T* p, lua_State* L) {
    const std::string sProfileName = p->GetPendingSourceProfileName();
    if (sProfileName.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, sProfileName.c_str());
    }
    return 1;
  }

  static int ConfirmPendingLink(T* p, lua_State* L) {
    const bool bMoveExisting = lua_gettop(L) >= 1 ? BArg(1) : true;
    LuaHelpers::Push(L, p->ConfirmPendingLink(bMoveExisting));
    return 1;
  }

  static int CancelPendingLink(T* p, lua_State* L) {
    p->CancelPendingLink();
    COMMON_RETURN_SELF;
  }

  static int GetLinkedCardUID(T* p, lua_State* L) {
    const std::string sUID = p->GetLinkedCardUID();
    if (sUID.empty()) {
      lua_pushnil(L);
    } else {
      lua_pushstring(L, sUID.c_str());
    }
    return 1;
  }

  static int IsCardLinked(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->IsCardLinked());
    return 1;
  }

  LunaScreenNFCLinkProfile() {
    ADD_METHOD(HasPendingLink);
    ADD_METHOD(GetPendingCardUID);
    ADD_METHOD(PendingLinkRequiresMove);
    ADD_METHOD(GetPendingSourceProfileID);
    ADD_METHOD(GetPendingSourceProfileName);
    ADD_METHOD(ConfirmPendingLink);
    ADD_METHOD(CancelPendingLink);
    ADD_METHOD(GetLinkedCardUID);
    ADD_METHOD(IsCardLinked);
  }
};

LUA_REGISTER_DERIVED_CLASS(ScreenNFCLinkProfile, ScreenWithMenuElements)
