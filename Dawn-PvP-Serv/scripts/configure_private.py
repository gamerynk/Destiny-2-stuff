import argparse
import ipaddress
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def unicast_ipv4(value):
    address = ipaddress.IPv4Address(value)
    if address.packed[0] == 0 or address.packed[0] >= 224:
        raise ValueError("Use a unicast IPv4 address, not a hostname or wildcard.")
    return str(address)


def cache_equipment_hash(path):
    with Path(path).open("rb") as stream:
        header = stream.read(28)
    if len(header) != 28 or header[:8] != b"RESTBLD1":
        raise ValueError("Not a Restoration build-data cache.")
    if struct.unpack_from("<I", header, 8)[0] != 44:
        raise ValueError("This source requires build-data cache format 44.")
    return f"0x{struct.unpack_from('<Q', header, 20)[0]:016X}"


def make_settings(role, address, cache=None):
    address = unicast_ipv4(address)
    seed = "universal_client_settings.json" if role == "client" else "default_settings.json"
    settings = json.loads((ROOT / "dawn/resources" / seed).read_text(encoding="utf-8-sig"))
    settings["client"]["external_server"] = {
        "enabled": True,
        "host": address,
        "origin": f"http://{address}:30977",
        "http_port": 30977,
        "config_url": f"http://{address}:30977/config/",
        "port_routes": [[30975, address, 30977]],
    }
    server = settings["server"]
    server.update(bap_port=30974, bap_advertised_address=address,
                  bap_advertised_port=30974, http_port=30975, public_http_port=30977)
    server["gameplay"].update(
        topology="embedded",
        bind_address=address if role == "server" else "127.0.0.1",
        advertised_address=address, transport_address=address, port=30976,
    )
    if cache is not None:
        server["build_data_equipment_hash"] = cache_equipment_hash(cache)
    return settings


def configure(role, install_root, address, cache=None):
    if role not in ("client", "server"):
        raise ValueError("Role must be client or server.")
    root = Path(install_root).resolve()
    if not root.is_dir():
        raise ValueError("The installation directory must already exist.")
    if role == "server" and cache is None:
        cache = root / "Restoration/cache/build_data.bin"
    settings = make_settings(role, address, cache)
    targets = [root / "Restoration/settings.json"]
    if role == "client":
        targets.append(root / "bin/x64/Restoration/settings.json")
    if any(path.exists() for path in targets):
        raise FileExistsError("Settings already exist. Back them up and edit them explicitly; nothing was overwritten.")
    payload = json.dumps(settings, indent=2) + "\n"
    created = []
    try:
        for path in targets:
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("x", encoding="utf-8", newline="\n") as stream:
                created.append(path)
                stream.write(payload)
    except Exception:
        for path in created:
            path.unlink()
        raise
    return targets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=("client", "server"), required=True)
    parser.add_argument("--install-root", type=Path, required=True)
    parser.add_argument("--server-address", required=True,
                        help="Server LAN/VPN IPv4 address; use 127.0.0.1 for a local-only test.")
    parser.add_argument("--build-data-cache", type=Path,
                        help="Matching format-44 cache; server defaults to INSTALL/Restoration/cache/build_data.bin.")
    args = parser.parse_args()
    try:
        paths = configure(args.role, args.install_root, args.server_address, args.build_data_cache)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Configuration failed: {error}\n")
    for path in paths:
        print(f"Created {path}")


if __name__ == "__main__":
    main()
