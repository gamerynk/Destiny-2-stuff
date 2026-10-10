# Dawn

Dawn is a Windows C++ client and private server for a specific supported 2019
Destiny 2 build. This repository contains source code, build scripts, tests, and
generic release tooling. It contains no hosted service configuration, player
database, credentials, game installation, or compiled release.

The native release profile keeps `curl_easy_setopt` as its one persistent
function detour during normal operation. Startup and compatibility guards,
native callbacks, window subclasses, and content replacements are described in
[the native baseline notes](docs/native-baseline-cleanup.md). Custom Mars and
Escalation Protocol workarounds and sword skating have been removed.

## Requirements and current limits

- Windows x64 for both the client and dedicated server.
- Visual Studio Build Tools with Desktop development with C++, MSVC **v145**,
  and Windows SDK **10.0.26100.0**. The project files target these versions.
- Python **3.11 or newer** for configuration and release tools. Their ordinary
  setup and packaging paths use the Python standard library.
- Your own compatible game installation, including its original compression
  runtime and networking DLL. Current retail game builds are not compatible.
- The **matching authored native content package set** and a **format-44
  `build_data.bin` cache** generated for that same game/content configuration.
  A release maintainer also needs the reviewed content build plan and manifest.
- A signed-in Steam client and a legitimate original Steam API DLL for each
  player. The matched launcher can locate it in an installed SteamVR runtime,
  or accept an explicit path. A VR headset is not required.

**This source release is not a turnkey conversion of an arbitrary stock game
installation.** The native replacements depend on matching content and cache
inputs. They are not included in the public repository. The release builder
verifies and packages supplied inputs; it does not recreate the entire authored
content set from this source alone. Obtain compatible inputs before attempting
to host. Building successfully does not prove live multiplayer compatibility.

## 1. Build the source

From PowerShell at the repository root:

```powershell
.\scripts\build-native-reduction.ps1
.\tools\dawn-friends\build.cmd
```

The first script locates MSBuild and builds both targets with the native release
options enabled. Outputs are:

- Client: `build\x64\Release\steam_api64.dll`
- Server: `build\x64\Release\restoration-server.exe`
- Optional friends tool: `build\tools\dawn-friends\DawnFriends.exe`

Keep the source checkout separate from server and game installations. Build
products and runtime data are ignored by Git.

## 2. Prepare a private server

Use a private LAN or VPN. The client-facing configuration service uses HTTP;
the setup below is intended for a trusted private network, not public exposure.
Choose a stable IPv4 address assigned to the server's LAN/VPN adapter. These
examples use **10.0.0.10**; replace it with your server's address everywhere.
For a local-only test, use **127.0.0.1**.

Create a server directory outside the source checkout, for example
`D:\DawnPrivateServer`, with this layout:

```text
DawnPrivateServer/
  restoration-server.exe
  destiny2.exe
  packages/                         matching game/content packages
  bin/x64/oo2core_3_win64.dll        matching original compression runtime
  Restoration/cache/build_data.bin matching format-44 cache
```

Copy `restoration-server.exe` from your build. Supply the other files from your
own compatible installation/release. The sibling `destiny2.exe` identifies the
game build when the server validates its cache. The server needs the packages
for its content manifest and the original compression runtime for compression.
Do not substitute another player's settings, identity keys, or account storage.

Generate a fresh configuration:

```powershell
python .\scripts\configure_private.py --role server --install-root D:\DawnPrivateServer --server-address 10.0.0.10
```

This creates `Restoration\settings.json` from the bundled defaults. It reads
the equipment fingerprint from the cache header, configures the matching HTTP,
BAP and gameplay endpoints, and binds client-facing listeners to the selected
server interface. It refuses to overwrite existing settings. It does not copy
assets, create credentials, or modify firewall rules. Reading the header alone
does not establish that a cache is valid.

Validate the complete cache, then start the server:

```powershell
Set-Location D:\DawnPrivateServer
.\restoration-server.exe --build-data-self-test
# Continue only if the cache self-test reports PASS.
.\restoration-server.exe
```

Keep its console open. To stop it normally, press Ctrl+C. Runtime logs, storage,
and identity material are created under its `Restoration` directory. Back up
that directory privately after a clean shutdown.

### Private firewall access

Allow only your trusted LAN/VPN peers to reach these server ports:

- TCP **30974**: BAP connection.
- TCP **30977**: client HTTP/configuration service.
- UDP **30976**: gameplay transport.
- UDP **3074–3075**: native discovery probes.

The owner/control HTTP listener on TCP **30975** binds to loopback. Do not
forward it or expose it through a proxy. Native player-to-player traffic also
needs permission on the private network; allow the game through each player's
firewall for that network. All players must be able to reach the server and
each other. The example uses fixed default ports to satisfy the native route
contract; changing only the HTTP host is insufficient.

From a player's machine, check the server before launching:

```powershell
Invoke-WebRequest http://10.0.0.10:30977/healthz
```

A successful health response verifies the HTTP listener only, not gameplay,
discovery, or player-to-player connectivity.

## 3. Configure and install each client

Each player needs their own compatible installation. For a fresh installation,
run from the source checkout, replacing the paths and server address:

```powershell
python .\scripts\configure_private.py --role client --install-root D:\DawnGame --server-address 10.0.0.10
```

Settings are written to both `D:\DawnGame\Restoration\settings.json` and
`D:\DawnGame\bin\x64\Restoration\settings.json`, because runtime paths are
relative to the loaded DLL's directory. Existing settings are never overwritten
by this helper. For an existing installation, use the matched updater described
below to merge connection fields while preserving player preferences.

Install the **matched client DLL and content together**, using the generated
`Install Update.cmd` from your private release package. Do not distribute only
a rebuilt DLL to players with unverified content. Keep the server on the same
release and close the game before updating it.

Sign in to Steam and launch using the generated package's launcher:

```powershell
.\Launch-Matched-Dawn.ps1 -GameFolder D:\DawnGame
```

If automatic Steam API discovery fails, pass `-SteamApi` with the full path to
the legitimate original Steam API DLL, or set `RESTORATION_STEAM_API_PATH` for
the process. Do not point it at Dawn's replacement DLL. The generated launcher
checks the matched game, networking DLL, client DLL, and content before launch.
The templates in `tools/release` must be sealed into a package before use.

Every player signs in with their own Steam identity. Let the client create its
own local identity material. Never share another installation's `identity.key`,
device keys, account database, or runtime `Restoration` folder.

### Optional friend management

After first sign-in, place `DawnFriends.exe` in the game's `bin\x64` directory
so it uses that installation's identity files. From that directory:

```powershell
.\DawnFriends.exe whoami --server 10.0.0.10 30977
.\DawnFriends.exe add ExamplePlayer --server 10.0.0.10 30977
.\DawnFriends.exe accept ExamplePlayer --server 10.0.0.10 30977
```

The recipient runs `accept` on their own installation. The tool defaults to
localhost; specify your private server explicitly.

## 4. Prepare a matched private update

Keep build plans, input assets, host-specific settings and generated packages
outside the public repository, or under the ignored `private` directory.
The reviewed plan supplies exact package names, source hashes and final hashes.
The reviewed build manifest pairs the matching client and content hashes; the
builder pins the supported game and networking DLL hashes. See
[release tooling](tools/release/README.md) for the format and safety
checks implemented by the builder and installer.

```powershell
python .\tools\release\build_matched_release.py --plan D:\PrivateInputs\plan.json --output D:\PrivateRelease
python .\tools\release\build_matched_release.py --seal-only --client .\build\x64\Release\steam_api64.dll --build-manifest D:\PrivateInputs\build-manifest.json --endpoint-settings D:\DawnPrivateServer\Restoration\settings.json --output D:\PrivateRelease
```

Distribute the entire generated folder privately. `content.patch` is a compact
recipe for reconstructing the required native packages from recognized input
files. It is part of a matched update, not a credential or a server database.
The installer verifies outputs, creates backups, and records a rollback receipt
under the game's `Restoration\matched-update-backups`. Instructions for using
`-Restore` are included with the generated package.

## Tests and troubleshooting

The unit tests build with CMake and run under CTest. From the repository root:

```powershell
cmake -S . -B build\tests
cmake --build build\tests --config Release --parallel
ctest --test-dir build\tests -C Release
.\build\x64\Release\restoration-server.exe --private-startup-self-test
```

Tests that need files outside the repo only run when you point CMake at them,
e.g. `-DDAWN_BUILD_DATA=<build_data.bin>` or `-DDAWN_NATIVE_TEXT=<native-text.bin>`.

The startup self-test exercises isolated private-session state without starting
network listeners or reading player storage. Use the bundled server defaults
or server-role settings for this test: client templates intentionally omit its
starter-account and character fixtures. Packaging tests use synthetic
temporary installations. Native replay/reverse-engineering tests are separate:
some need private inputs through `DAWN_NATIVE_TEXT`, `DAWN_NATIVE_FIXTURES`, or
`DAWN_GAME_ROOT`, plus the Python modules imported by those tests.

- **Cache test fails:** check format, game executable, content set, and equipment
  fingerprint. The dedicated server cannot extract every missing cache domain
  itself. Obtain a matching complete cache; do not bypass validation.
- **Cannot reach health:** check the selected interface, server process, VPN
  routing and TCP 30977 firewall rule.
- **Health works but login/loading fails:** check BAP, discovery and gameplay
  ports, consistent endpoint settings, matching releases on every participant,
  and the appropriate installation's `Restoration\logs` directory.
- **Configuration helper refuses existing settings:** this is intentional. Back
  up and edit them explicitly, or use the matched updater for endpoint changes.
- **Installer rejects a package:** provide its recognized source files and the
  reviewed matching manifest. Do not disable hash checks.

Logs can contain player identifiers, addresses and diagnostic data. Redact them
before posting an issue. Neither offline tests nor this source cleanup confirms
that a fresh multiplayer session or a particular activity works correctly.

## Publishing this source

Publish this sanitized checkout or its source archive as a **new repository**.
Do not attach the private development repository's old history. If importing the
ZIP, initialize Git inside the extracted `Dawn` folder and review the staged
files before committing. Use a Git author identity you intend to make public.

The ignore rules exclude runtime data, local configuration, private inputs,
builds, packages, credentials and logs. They do not protect files already tracked
by Git. Run a secret scanner before publishing and after adding new files.
`.gitleaksignore` contains only reviewed synthetic test constants and an upstream
enum comparison; it does not exclude entire source directories.

## Credits

- [GeorgeRatington](https://github.com/GeorgeRatington) — Contributor.

## License

Dawn source is licensed under **GNU GPL version 3 only** (`GPL-3.0-only`). See
[LICENSE](LICENSE). Retained third-party code and assets carry their original
licenses and attribution; see [third_party_notices.md](third_party_notices.md)
and the notices under `dawn/vendor`.

Game assets, proprietary game/Steam/compression binaries, personal data, and
private deployment inputs are not included in this source release. This license
does not grant rights to those separate materials.
