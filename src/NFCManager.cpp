#include "NFCManager.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "LuaBinding.h"
#include "LuaManager.h"
#include "MessageManager.h"
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "global.h"

#if defined(HAS_NFC)
#include "arch/NFC/NFCDriver_PCSC.h"
#else
#include "arch/NFC/NFCDriver_Null.h"
#endif

namespace {
const size_t kLogCardDataPreviewBytes = 64;
const char* kGrooveStatsSection = "[GrooveStats]";
const char* kGrooveStatsApiKeyName = "ApiKey";
const char* kGrooveStatsUsernameName = "Username";
const char* kGrooveStatsIsPadPlayerName = "IsPadPlayer";
const size_t kGrooveStatsApiKeyBytes = 64;

std::string HexPreview(const std::string& sData) {
  static const char kHexChars[] = "0123456789ABCDEF";
  if (sData.empty()) {
    return "<empty>";
  }

  const size_t iPreviewBytes = (sData.size() > kLogCardDataPreviewBytes)
                                   ? kLogCardDataPreviewBytes
                                   : sData.size();
  std::string out;
  out.reserve(iPreviewBytes * 2 + 24);

  for (size_t i = 0; i < iPreviewBytes; ++i) {
    const unsigned char c = static_cast<unsigned char>(sData[i]);
    out.push_back(kHexChars[(c >> 4) & 0xF]);
    out.push_back(kHexChars[c & 0xF]);
  }

  if (iPreviewBytes < sData.size()) {
    out += ssprintf("...(%zu bytes total)", sData.size());
  }

  return out;
}

bool ParseGrooveStatsCardData(
    const std::string& sData, std::string& sApiKeyOut,
    std::string& sUsernameOut, bool& bIsPadPlayerOut, std::string& sErrorOut) {
  sApiKeyOut.clear();
  sUsernameOut.clear();
  bIsPadPlayerOut = false;
  sErrorOut.clear();

  if (sData.empty()) {
    sErrorOut = "Card has no stored data.";
    return false;
  }

  std::istringstream stream(sData);
  std::string line;
  bool bInGrooveStatsSection = false;

  while (std::getline(stream, line)) {
    Trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      bInGrooveStatsSection = (line == kGrooveStatsSection);
      continue;
    }

    if (!bInGrooveStatsSection) {
      continue;
    }

    const std::string::size_type iEquals = line.find('=');
    if (iEquals == std::string::npos) {
      continue;
    }

    std::string sKey = line.substr(0, iEquals);
    std::string sValue = line.substr(iEquals + 1);
    Trim(sKey);
    Trim(sValue);

    if (sKey == kGrooveStatsApiKeyName) {
      sApiKeyOut = sValue;
    } else if (sKey == kGrooveStatsUsernameName) {
      sUsernameOut = sValue;
    } else if (sKey == kGrooveStatsIsPadPlayerName) {
      bIsPadPlayerOut = (sValue == "1");
    }
  }

  if (sApiKeyOut.size() != kGrooveStatsApiKeyBytes) {
    sErrorOut = "GrooveStats ApiKey is missing or invalid.";
    return false;
  }

  sErrorOut.clear();
  return true;
}

bool SerializeGrooveStatsCardData(
    const std::string& sApiKey, const std::string& sUsername,
    bool bIsPadPlayer, std::string& sDataOut, std::string& sErrorOut) {
  sDataOut.clear();
  sErrorOut.clear();

  if (sApiKey.size() != kGrooveStatsApiKeyBytes) {
    sErrorOut = "GrooveStats ApiKey must be exactly 64 characters.";
    return false;
  }

  if (sApiKey.find('\n') != std::string::npos ||
      sApiKey.find('\r') != std::string::npos) {
    sErrorOut = "GrooveStats ApiKey contains invalid line breaks.";
    return false;
  }

  if (sUsername.find('\n') != std::string::npos ||
      sUsername.find('\r') != std::string::npos) {
    sErrorOut = "GrooveStats Username contains invalid line breaks.";
    return false;
  }

  sDataOut = ssprintf(
      "[GrooveStats]\nApiKey=%s\nIsPadPlayer=%d\nUsername=%s\n",
      sApiKey.c_str(), bIsPadPlayer ? 1 : 0, sUsername.c_str());
  return true;
}
}  // namespace

NFCManager* NFCMAN = nullptr;

Preference<bool> NFCManager::m_bNFCEnabled("NFCEnabled", true);
Preference<float> NFCManager::m_fPollIntervalSeconds(
    "NFCPollIntervalSeconds", 0.25f);

NFCManager::NFCManager()
    : m_Mutex("NFCManager"),
      m_bEnabled(false),
      m_bShutdown(false),
      m_bCardPresent(false) {
  if (!m_bNFCEnabled.Get()) {
    LOG->Info("NFCManager: Disabled by preference.");
    return;
  }

#if defined(HAS_NFC)
  m_pDriver = std::make_unique<NFCDriver_PCSC>();
#else
  m_pDriver = std::make_unique<NFCDriver_Null>();
#endif

  if (!m_pDriver->Init()) {
    LOG->Warn(
        "NFCManager: Driver failed to initialize. "
        "NFC card login will be unavailable.");
    return;
  }

  m_bEnabled = true;
  m_PollThread.SetName("NFCManager poll");
  m_PollThread.Create(PollThread_Start, this);
  LOG->Info("NFCManager: Initialized successfully.");

  // Register with Lua.
  {
    Lua* L = LUA->Get();
    lua_pushstring(L, "NFCMAN");
    this->PushSelf(L);
    lua_settable(L, LUA_GLOBALSINDEX);
    LUA->Release(L);
  }
}

NFCManager::~NFCManager() {
  // Unregister with Lua.
  LUA->UnsetGlobal("NFCMAN");

  if (m_bEnabled) {
    m_bShutdown = true;
    m_PollThread.Wait();
  }
}

// ---------------------------------------------------------------------------
// Background polling thread
// ---------------------------------------------------------------------------

int NFCManager::PollThread_Start(void* p) {
  reinterpret_cast<NFCManager*>(p)->PollThread();
  return 0;
}

void NFCManager::PollThread() {
  bool bWasPresentLastIteration = false;
  std::string sPrevUID;

  while (!m_bShutdown) {
    float fInterval = m_fPollIntervalSeconds.Get();
    if (fInterval < 0.05f) {
      fInterval = 0.05f;
    }

    // Poll the hardware driver.
    m_pDriver->Poll();
    bool bNowPresent = m_pDriver->IsCardPresent();
    std::string sUID = m_pDriver->GetCurrentCardUID();

    {
      LockMut(m_Mutex);
      m_bCardPresent = bNowPresent;
      m_sCurrentCardUID = sUID;
    }

    // Detect transitions.
    if (bNowPresent && !bWasPresentLastIteration) {
      // Card tapped.
      {
        LockMut(m_Mutex);
        m_sLastTappedUID = sUID;
      }
      LOG->Info("NFCManager: Card tapped – UID %s", sUID.c_str());

      std::string sCardData;
      if (ReadCardData(sCardData)) {
        LOG->Info(
            "NFCManager: Card data read success (%zu bytes): %s",
            sCardData.size(), HexPreview(sCardData).c_str());
      } else {
        const std::string sError = GetLastCardIOError();
        LOG->Warn(
            "NFCManager: Card data read failed for UID %s: %s", sUID.c_str(),
            sError.empty() ? "Unknown read error." : sError.c_str());
      }

      Message msg(MessageIDToString(Message_NFCCardTapped));
      msg.SetParam("UID", sUID);
      MESSAGEMAN->Broadcast(msg);

    } else if (!bNowPresent && bWasPresentLastIteration) {
      // Card removed.
      LOG->Info("NFCManager: Card removed (was UID %s)", sPrevUID.c_str());
      Message msg(MessageIDToString(Message_NFCCardRemoved));
      msg.SetParam("UID", sPrevUID);
      MESSAGEMAN->Broadcast(msg);
    }

    bWasPresentLastIteration = bNowPresent;
    sPrevUID = sUID;

    std::this_thread::sleep_for(std::chrono::duration<float>(fInterval));
  }
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

std::string NFCManager::GetCurrentCardUID() const {
  LockMut(m_Mutex);
  return m_sCurrentCardUID;
}

std::string NFCManager::GetLastTappedUID() const {
  LockMut(m_Mutex);
  return m_sLastTappedUID;
}

bool NFCManager::IsCardPresent() const {
  LockMut(m_Mutex);
  return m_bCardPresent;
}

std::vector<std::string> NFCManager::GetReaderNames() const {
  if (!m_bEnabled) {
    return {};
  }
  return m_pDriver->GetReaderNames();
}

bool NFCManager::SupportsCardDataIO() const {
  return m_bEnabled && m_pDriver != nullptr && m_pDriver->SupportsCardDataIO();
}

bool NFCManager::SupportsCardDataWrite() const {
  return m_bEnabled && m_pDriver != nullptr && m_pDriver->SupportsCardDataWrite();
}

int NFCManager::GetMaxCardDataBytes() const {
  if (!SupportsCardDataIO()) {
    return 0;
  }
  return m_pDriver->GetMaxCardDataBytes();
}

bool NFCManager::ReadCardData(std::string& sDataOut) {
  sDataOut.clear();

  std::string sError;
  if (!SupportsCardDataIO()) {
    sError = "Card data I/O is unavailable.";
  } else if (!m_pDriver->ReadCardData(sDataOut, sError)) {
    if (sError.empty()) {
      sError = "Failed to read NFC card data.";
    }
  }

  {
    LockMut(m_Mutex);
    m_sLastCardIOError = sError;
  }
  return sError.empty();
}

bool NFCManager::WriteCardData(const std::string& sDataIn) {
  std::string sError;
  if (!SupportsCardDataWrite()) {
    sError = "Card data writing is unavailable.";
  } else if (!m_pDriver->WriteCardData(sDataIn, sError)) {
    if (sError.empty()) {
      sError = "Failed to write NFC card data.";
    }
  }

  {
    LockMut(m_Mutex);
    m_sLastCardIOError = sError;
  }
  return sError.empty();
}

bool NFCManager::HasCardData() {
  std::string sData;
  if (!ReadCardData(sData)) {
    return false;
  }
  return !sData.empty();
}

bool NFCManager::ReadGrooveStatsCardData(
    std::string& sApiKeyOut, std::string& sUsernameOut,
    bool& bIsPadPlayerOut) {
  sApiKeyOut.clear();
  sUsernameOut.clear();
  bIsPadPlayerOut = false;

  std::string sError;
  std::string sData;
  if (!ReadCardData(sData)) {
    return false;
  }

  if (!ParseGrooveStatsCardData(
          sData, sApiKeyOut, sUsernameOut, bIsPadPlayerOut, sError)) {
    LockMut(m_Mutex);
    m_sLastCardIOError = sError;
    return false;
  }

  LockMut(m_Mutex);
  m_sLastCardIOError.clear();
  return true;
}

bool NFCManager::WriteGrooveStatsCardData(
    const std::string& sApiKey, const std::string& sUsername,
    bool bIsPadPlayer) {
  std::string sError;
  std::string sSerializedData;
  if (!SerializeGrooveStatsCardData(
          sApiKey, sUsername, bIsPadPlayer, sSerializedData, sError)) {
    LockMut(m_Mutex);
    m_sLastCardIOError = sError;
    return false;
  }

  if (static_cast<int>(sSerializedData.size()) > GetMaxCardDataBytes()) {
    LockMut(m_Mutex);
    m_sLastCardIOError =
        "GrooveStats payload exceeds maximum NFC card data size.";
    return false;
  }

  if (!WriteCardData(sSerializedData)) {
    return false;
  }

  {
    LockMut(m_Mutex);
    m_sLastCardIOError.clear();
  }

  return true;
}

std::string NFCManager::GetLastCardIOError() const {
  LockMut(m_Mutex);
  return m_sLastCardIOError;
}

// ---------------------------------------------------------------------------
// Lua bindings
// ---------------------------------------------------------------------------

class LunaNFCManager : public Luna<NFCManager> {
 public:
  static int IsEnabled(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->IsEnabled());
    return 1;
  }

  static int IsCardPresent(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->IsCardPresent());
    return 1;
  }

  static int GetCurrentCardUID(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetCurrentCardUID());
    return 1;
  }

  static int GetLastTappedUID(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetLastTappedUID());
    return 1;
  }

  static int GetReaderNames(T* p, lua_State* L) {
    auto names = p->GetReaderNames();
    lua_newtable(L);
    for (size_t i = 0; i < names.size(); ++i) {
      LuaHelpers::Push(L, names[i]);
      lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
  }

  static int SupportsCardDataIO(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->SupportsCardDataIO());
    return 1;
  }

  static int SupportsCardDataWrite(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->SupportsCardDataWrite());
    return 1;
  }

  static int GetMaxCardDataBytes(T* p, lua_State* L) {
    lua_pushnumber(L, p->GetMaxCardDataBytes());
    return 1;
  }

  static int ReadCardData(T* p, lua_State* L) {
    std::string sData;
    if (!p->ReadCardData(sData)) {
      lua_pushnil(L);
      return 1;
    }
    lua_pushlstring(L, sData.data(), sData.size());
    return 1;
  }

  static int GetLastCardIOError(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetLastCardIOError());
    return 1;
  }

  static int HasCardData(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->HasCardData());
    return 1;
  }

  static int ReadGrooveStatsCardData(T* p, lua_State* L) {
    std::string sApiKey;
    std::string sUsername;
    bool bIsPadPlayer = false;
    if (!p->ReadGrooveStatsCardData(sApiKey, sUsername, bIsPadPlayer)) {
      lua_pushnil(L);
      return 1;
    }

    lua_newtable(L);
    LuaHelpers::Push(L, sApiKey);
    lua_setfield(L, -2, "ApiKey");
    LuaHelpers::Push(L, sUsername);
    lua_setfield(L, -2, "Username");
    LuaHelpers::Push(L, bIsPadPlayer);
    lua_setfield(L, -2, "IsPadPlayer");
    return 1;
  }

  static int WriteGrooveStatsCardData(T* p, lua_State* L) {
    const std::string sApiKey = SArg(1);
    const std::string sUsername = SArg(2);
    const bool bIsPadPlayer = (lua_gettop(L) >= 3) ? BArg(3) : true;

    LuaHelpers::Push(
        L, p->WriteGrooveStatsCardData(sApiKey, sUsername, bIsPadPlayer));
    return 1;
  }

  LunaNFCManager() {
    ADD_METHOD(IsEnabled);
    ADD_METHOD(IsCardPresent);
    ADD_METHOD(GetCurrentCardUID);
    ADD_METHOD(GetLastTappedUID);
    ADD_METHOD(GetReaderNames);
    ADD_METHOD(SupportsCardDataIO);
    ADD_METHOD(SupportsCardDataWrite);
    ADD_METHOD(GetMaxCardDataBytes);
    ADD_METHOD(ReadCardData);
    ADD_METHOD(HasCardData);
    ADD_METHOD(ReadGrooveStatsCardData);
    ADD_METHOD(WriteGrooveStatsCardData);
    ADD_METHOD(GetLastCardIOError);
  }
};

LUA_REGISTER_CLASS(NFCManager)

/*
 * (c) 2024 ITGmania contributors
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
