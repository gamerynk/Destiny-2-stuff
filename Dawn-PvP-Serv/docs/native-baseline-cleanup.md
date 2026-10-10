# Native baseline cleanup

The custom Mars and Escalation Protocol runtime has been removed from the client
and dedicated server. This includes their roster overrides, population and AI
workarounds, event progression, crystal and boss handling, native entity
publication experiments, and associated capture tools and tests. Mars is no
longer enrolled in the custom public-destination policy.

Sword skating and its physics detour have also been removed. The movement/player
cheat panel, unlimited ammo, teleport, and fly implementations are absent. The
read-only native player-position observer remains.

Shared multiplayer behavior remains: peer acknowledgements and fragments,
connection identity checks, fireteam membership, public allocation, and capacity
for 64 gameplay peers, 64 protected associations, and 256 DTLS associations.

## Hook count

The native release profile retains `curl_easy_setopt` as its one persistent
function detour on the supported game build. Native callbacks, window subclasses,
data changes, and matched content replacements are separate mechanisms.

This preserves the existing overlay compatibility behavior: `ClipCursor` has a
temporary startup guard that retires once the native cursor path is ready, and
input/graphics guards can return if their native contracts fail. Therefore this
is a one-hook **normal-operation** baseline, not a guarantee of one attachment
during startup or on an unsupported native layout. No sword-skating or Mars/EP
hook can be installed by this source.

## Build and validation

`scripts/build-native-reduction.ps1` defaults to the matched native release
options in `build-config/one-hook.props` and builds both x64 Release targets.
The existing matched content required by the native replacements remains
necessary; this cleanup does not generate or install a new content patch.

The binaries require a fresh gameplay check before release. Compiling and
offline tests do not verify a live multiplayer session, native overlay startup,
or stock Mars behavior after removal of its custom implementation.
