# Matched private releases

The matched updater installs one client DLL and the exact corrected native content set together. It does not launch the game, stop processes, change identity files, or contact a server. Windows PowerShell is sufficient on the player's machine; Python 3.11 or newer is used only to prepare and test the release.

The build plan supplies each final package's reviewed SHA256 and qualified source paths/hashes. `build_matched_release.py --plan PLAN --output OUTPUT` creates a compact copy/data recipe for exactly 40 distinct packages. It verifies both native metadata SHA1 spans before admitting a final package. The output directory must not already exist. This tool packages an authored content set supplied by a maintainer; it does not generate that set from an arbitrary stock installation.

## Private input formats

The plan is a JSON object with a `packages` array. Each row has:

- `name`: final `w64_...pkg` file name, with no directory components.
- `target`: path to the already authored final package.
- `sha256`: reviewed SHA256 of that final package.
- `sources`: array of recognized input variants, each with `path` and `sha256`.

All file paths are resolved from the builder's working directory. Use absolute
paths in private plans to avoid ambiguity. Keep these plans and input files out
of the public source repository. Each final package needs at least one distinct
recognized input variant from which an update can be reconstructed.

The build manifest is a JSON object containing `artifacts`, with a
`steam_api64.dll` SHA256 entry, and `pairedPackages`, mapping every final package
name to its reviewed SHA256. The builder verifies this pairing before sealing.
The supported game and networking DLL hashes are pinned by the builder's `GAME`
and `NETWORK` constants; they are compatibility fingerprints, not credentials.
Do not replace them to bypass an unsupported installation.

The endpoint settings are a private `settings.json` produced for your server.
The top-level setup guide includes a helper for creating one. Actual host
addresses belong in this file, not in the public source defaults.

## Seal and install

Seal the output with `--seal-only --client DLL --build-manifest MANIFEST --endpoint-settings SETTINGS --output OUTPUT`. The DLL and every final package must match the archived build manifest. The verified private server settings supply the entire `client.external_server` object and the required `server.bap_advertised_address`, `bap_port`, `bap_advertised_port`, and gameplay `topology`, `advertised_address`, `transport_address`, `port` fields. The updater changes only these endpoint fields. Bind addresses, entitlements, identity and other preferences remain intact. Do not substitute an unverified host or port.

Ship the complete generated folder, including `release-manifest.json`, `content.patch`, `steam_api64.dll`, `Install-Matched-Release.ps1`, `Install Update.cmd`, `Launch-Matched-Dawn.ps1`, `Launch Dawn.cmd`, and `README.txt`. The generated installer pins the manifest hash. It stages and verifies all outputs and backups before changing the installation. It changes only connection settings, archives stale native cache files when package content changes, and keeps verified backups under `Restoration/matched-update-backups`.

`-ContentOnly` preserves the existing connection for a local control installation. `-Restore RECEIPT` restores the exact files saved by that transaction, including prior DLLs/settings/cache. It refuses to overwrite files that changed afterward. A failed interrupted update can be restored with its receipt; an ordinary in-process commit failure attempts rollback automatically.

Use the new matched launcher after updating. An older installer can overwrite the new client DLL. The existing distribution launcher/updater must invoke this content transaction before launch rather than copy only the DLL.

Run `python tools/release/test_matched_release.py` for actual PowerShell transaction fixtures. Fixtures use synthetic game/package files, and the stopped-process check is replaced only in their harness. They do not install into a real game folder.

The `native_endpoint_contract_test` CTest target compiles the actual native-route contract with the merged endpoint values and negative cases. This checks the typed startup contract; it is not a live network or multiplayer test.
