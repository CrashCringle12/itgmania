# NFC support

ITGmania can use PC/SC-compatible NFC readers to:

- link an NFC card UID to a local profile
- auto-select linked profiles on the profile selection screen
- expose NFC events and card reads to themes through Lua

The current implementation is built around PC/SC readers such as the ACR122U.

## OS requirements

### Windows

- NFC support uses the built-in `WinSCard` API from the Windows SDK
- no extra NFC library is required to build ITGmania
- you still need a PC/SC-compatible NFC reader and its driver installed

### macOS

- NFC support uses the built-in `PCSC` framework
- no extra NFC library is required to build ITGmania
- you still need a PC/SC-compatible NFC reader that is recognized by macOS

### Linux and BSD

- build with `WITH_NFC` enabled (it is `ON` by default)
- install pcsclite development files before building
- install and run the `pcscd` daemon at runtime
- connect a PC/SC-compatible NFC reader

On Debian/Ubuntu, the CMake configuration expects the equivalent of:

```bash
sudo apt install libpcsclite-dev pcscd
```

## Building with NFC enabled

NFC support is controlled by the CMake option:

```cmake
WITH_NFC
```

If the required PC/SC support is available for your platform, ITGmania builds
with NFC support enabled. If the dependency is missing, the build falls back to
NFC-disabled behavior.

## Runtime requirements

- `NFCEnabled` must remain enabled in preferences
- the reader must be visible to the OS through PC/SC
- the game must be able to initialize the NFC manager at startup

If initialization fails, NFC login and card reads stay unavailable for that run.

## Linking a card to a profile

1. Start ITGmania with a supported NFC reader connected.
2. Open **Manage Profiles**.
3. Select the local profile you want to use.
4. Choose **Link NFC Card**.
5. Tap the NFC card on the reader.
6. Press **Start** to confirm the link.

Notes:

- pressing **Back** cancels the pending link
- if the card is already linked to another profile, ITGmania prompts you to move it
- the linked UID is saved in the local profile data

## Logging in with a linked card

On `ScreenSelectProfile`, tapping a linked card automatically selects the
matching local profile for the first human player who has not chosen one yet.
If every human player has already been assigned a profile, normal screen flow
continues.

If a card is not linked to any local profile, ITGmania leaves handling to the
theme or the usual profile selection flow.

## Theme and Lua usage

ITGmania exposes a global `NFCMAN` object to Lua when NFC support initializes
successfully.

Useful Lua methods include:

- `NFCMAN:IsEnabled()`
- `NFCMAN:IsCardPresent()`
- `NFCMAN:GetCurrentCardUID()`
- `NFCMAN:GetLastTappedUID()`
- `NFCMAN:GetReaderNames()`
- `NFCMAN:SupportsCardDataIO()`
- `NFCMAN:SupportsCardDataWrite()`
- `NFCMAN:GetMaxCardDataBytes()`
- `NFCMAN:ReadCardData()`
- `NFCMAN:HasCardData()`
- `NFCMAN:ReadGrooveStatsCardData()`
- `NFCMAN:WriteGrooveStatsCardData(apiKey, username[, isPadPlayer])`
- `NFCMAN:GetLastCardIOError()`

Themes can also subscribe to these broadcast messages:

- `NFCCardTapped`
- `NFCCardRemoved`

`NFCCardTapped` includes the tapped card UID in the `UID` parameter. This makes
it possible to build custom login or card-reactive theme flows without polling
Lua every frame.

## Card data storage format

The PC/SC NFC driver reads and writes raw NTAG-style user memory pages, using an
ITG-specific envelope:

- Header starts at page 4
- Header bytes 0-3 must be the magic `ITGN`
- Header byte 4 is the format version (`1`)
- Header bytes 6-7 store payload length (big-endian)
- Payload bytes are stored in pages 6 through 129 (max 496 bytes)

`NFCMAN:ReadCardData()` returns this stored payload only when the header matches
the `ITGN` format.

Data written by generic NFC phone tools (for example NDEF text records) usually
does not match this structure, so ITGmania will not surface it through
`ReadCardData()`.

## GrooveStats payload writes from Lua

Lua write access is intentionally restricted to a GrooveStats payload, not a
generic card key-value API. Use:

- `NFCMAN:HasCardData()`
- `NFCMAN:ReadGrooveStatsCardData()`
- `NFCMAN:WriteGrooveStatsCardData(apiKey, username[, isPadPlayer])`

Payload format written to the card:

```ini
[GrooveStats]
ApiKey=<64-character key>
IsPadPlayer=<0 or 1>
Username=<name>
```
