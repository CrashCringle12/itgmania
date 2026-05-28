#include "ScreenNFCLinkProfile.h"

#include <string>
#include <vector>

#include "ActorUtil.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "MessageManager.h"
#include "NFCManager.h"
#include "Profile.h"
#include "ProfileManager.h"
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
static LocalizedString LINK_NFC_INSTRUCTIONS(
    "ScreenNFCLinkProfile", "PressStartBack");

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
      LinkUIDToProfile(sUID);
    }
    RefreshDisplay();
  } else if (msg.GetName() == MessageIDToString(Message_NFCCardRemoved)) {
    RefreshDisplay();
  }

  ScreenWithMenuElements::HandleMessage(msg);
}

bool ScreenNFCLinkProfile::MenuStart(const InputEventPlus& input) {
  return MenuBack(input);
}

bool ScreenNFCLinkProfile::MenuBack(const InputEventPlus&) {
  if (IsTransitioning()) {
    return false;
  }
  SCREENMAN->PlayStartSound();
  StartTransitioningScreen(SM_GoToPrevScreen);
  return true;
}

void ScreenNFCLinkProfile::RefreshDisplay() {
  if (NFCMAN == nullptr || !NFCMAN->IsEnabled()) {
    m_textStatus.SetText(LINK_NFC_UNAVAILABLE.GetValue());
    m_textUID.SetText(LINK_NFC_UID_LABEL.GetValue() + ": ----");
    m_textReader.SetText(
        LINK_NFC_READER_LABEL.GetValue() + ": " +
        LINK_NFC_READER_UNKNOWN.GetValue());
    return;
  }

  const bool bCardPresent = NFCMAN->IsCardPresent();
  const std::string sCurrentUID = NFCMAN->GetCurrentCardUID();

  if (!m_sErrorStatus.empty()) {
    m_textStatus.SetText(m_sErrorStatus);
  } else if (bCardPresent) {
    m_textStatus.SetText(LINK_NFC_DETECTED.GetValue());
  } else if (m_bTappedCard) {
    m_textStatus.SetText(LINK_NFC_LINKED.GetValue());
  } else {
    m_textStatus.SetText(LINK_NFC_WAITING.GetValue());
  }

  std::string sUIDToShow = sCurrentUID;
  if (sUIDToShow.empty()) {
    sUIDToShow = m_sLastLinkedUID;
  }
  if (sUIDToShow.empty()) {
    sUIDToShow = "----";
  }
  m_textUID.SetText(LINK_NFC_UID_LABEL.GetValue() + ": " + sUIDToShow);

  std::vector<std::string> vsReaders = NFCMAN->GetReaderNames();
  std::string sReaderName = LINK_NFC_READER_UNKNOWN.GetValue();
  if (!vsReaders.empty()) {
    sReaderName = vsReaders[0];
  }
  m_textReader.SetText(LINK_NFC_READER_LABEL.GetValue() + ": " + sReaderName);
}

void ScreenNFCLinkProfile::LinkUIDToProfile(const std::string& sUID) {
  m_sErrorStatus.clear();

  if (sUID.empty()) {
    return;
  }

  Profile* pProfile = PROFILEMAN->GetLocalProfile(m_sProfileID);
  if (pProfile == nullptr) {
    m_sErrorStatus = LINK_NFC_FAILED.GetValue();
    return;
  }

  pProfile->m_sNFCCardUID = sUID;
  if (!PROFILEMAN->SaveLocalProfile(m_sProfileID)) {
    m_sErrorStatus = LINK_NFC_FAILED.GetValue();
    return;
  }

  m_bTappedCard = true;
  m_sLastLinkedUID = sUID;
}
