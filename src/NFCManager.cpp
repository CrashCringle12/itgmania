#include "NFCManager.h"

#include <chrono>
#include <memory>
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
#if defined(HAS_NFC_PCSC)
#include "arch/NFC/NFCDriver_PCSC.h"
#endif
#if defined(HAS_NFC_LIBNFC)
#include "arch/NFC/NFCDriver_LibNFC.h"
#endif
#endif
#if !defined(HAS_NFC)
#include "arch/NFC/NFCDriver_Null.h"
#endif

namespace {
const size_t kLogCardDataPreviewBytes = 64;

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

  bool bDriverInitialized = false;

#if defined(HAS_NFC_PCSC)
  m_pDriver = std::make_unique<NFCDriver_PCSC>();
  bDriverInitialized = m_pDriver->Init();
  if (!bDriverInitialized) {
#if defined(HAS_NFC_LIBNFC)
    LOG->Warn("NFCManager: PC/SC NFC driver failed; trying libnfc fallback.");
    m_pDriver = std::make_unique<NFCDriver_LibNFC>();
    bDriverInitialized = m_pDriver->Init();
#else
    m_pDriver.reset();
#endif
  }
#elif defined(HAS_NFC_LIBNFC)
  m_pDriver = std::make_unique<NFCDriver_LibNFC>();
  bDriverInitialized = m_pDriver->Init();
#elif !defined(HAS_NFC)
  m_pDriver = std::make_unique<NFCDriver_Null>();
  bDriverInitialized = m_pDriver->Init();
#endif

  if (!m_pDriver || !bDriverInitialized) {
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

bool NFCManager::WriteCardData(const std::string& sData) {
  std::string sError;
  if (!SupportsCardDataIO()) {
    sError = "Card data I/O is unavailable.";
  } else if (!m_pDriver->WriteCardData(sData, sError)) {
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

  static int WriteCardData(T* p, lua_State* L) {
    size_t iLen = 0;
    const char* pData = luaL_checklstring(L, 1, &iLen);
    LuaHelpers::Push(L, p->WriteCardData(std::string(pData, iLen)));
    return 1;
  }

  static int GetLastCardIOError(T* p, lua_State* L) {
    LuaHelpers::Push(L, p->GetLastCardIOError());
    return 1;
  }

  LunaNFCManager() {
    ADD_METHOD(IsEnabled);
    ADD_METHOD(IsCardPresent);
    ADD_METHOD(GetCurrentCardUID);
    ADD_METHOD(GetLastTappedUID);
    ADD_METHOD(GetReaderNames);
    ADD_METHOD(SupportsCardDataIO);
    ADD_METHOD(GetMaxCardDataBytes);
    ADD_METHOD(ReadCardData);
    ADD_METHOD(WriteCardData);
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
