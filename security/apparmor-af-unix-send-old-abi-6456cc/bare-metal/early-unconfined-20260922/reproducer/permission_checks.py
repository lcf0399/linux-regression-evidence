#!/usr/bin/env python3
"""Bounded live-kernel permission controls; only own temporary profiles are touched.

All sockets are created after entering their labels. No timing is taken. These
tests cover named cases, not a general security proof or concurrent policy loads.
"""
import argparse
import ctypes
import errno
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import time

PREFIX = "ks_aa_early_20260921_"
PAYLOAD = b"apparmor-early-unconfined-permission-control"


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def policy(name, deny=None, unconfined=False, old=False, file_deny=False):
    abi = "kernel-5.4-vanilla" if old else "5.0"
    flags = "unconfined" if unconfined else "attach_disconnected"
    rules = "network unix," if old else "unix,"
    if deny:
        rules += f"\n  deny unix ({deny}),"
    if file_deny:
        rules += f"\n  deny /tmp/{PREFIX}*.sock rw,"
    transition = "" if old else f"  change_profile -> &{PREFIX}*,\n"
    return (f"abi <abi/{abi}>,\nprofile {PREFIX}{name} flags=({flags}) {{\n"
            f"  file,\n  {rules}\n{transition}}}\n")


def enter(chain):
    lib = ctypes.CDLL("libapparmor.so.1", use_errno=True)
    for i, name in enumerate(chain.split("+") if chain != "none" else []):
        fn = lib.aa_change_profile if i == 0 else lib.aa_stack_profile
        fn.argtypes = [ctypes.c_char_p]
        fn.restype = ctypes.c_int
        if fn((PREFIX + name).encode()) != 0:
            raise OSError(ctypes.get_errno(), "profile transition failed: " + name)
    context = Path("/proc/self/attr/current").read_text().strip()
    if chain == "none":
        require(context == "unconfined", "expected unconfined child")
    else:
        require(all(PREFIX + n in context for n in chain.split("+")), "incorrect child label")
    return context


def child(args):
    result = dict(role=args.child, profile=args.profile, status="incomplete", stage="profile")
    sock = conn = None
    try:
        result["context"] = enter(args.profile)
        result["stage"] = "create"
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM if args.kind == "dgram" else socket.SOCK_STREAM)
        sock.settimeout(2)
        address = args.address if args.address.startswith("/") else "\0" + args.address
        if args.child == "receiver":
            result["stage"] = "bind"
            sock.bind(address)
            if args.kind == "stream":
                result["stage"] = "listen"
                sock.listen(1)
            print(json.dumps(dict(status="ready", context=result["context"])), flush=True)
            command = sys.stdin.readline().strip()
            if command == "receive":
                result["stage"] = "receive"
                if args.kind == "stream":
                    conn, _ = sock.accept()
                    conn.settimeout(2)
                data = (conn or sock).recv(256)
                require(data == PAYLOAD, "payload mismatch")
                result["received_bytes"] = len(data)
            elif command != "stop":
                raise RuntimeError("missing receiver command")
        else:
            result["stage"] = "connect" if args.kind == "stream" else "send"
            if args.kind == "stream":
                sock.connect(address)
                result["stage"] = "send"
                require(sock.send(PAYLOAD) == len(PAYLOAD), "short stream send")
            else:
                require(sock.sendto(PAYLOAD, address) == len(PAYLOAD), "short datagram send")
        result["status"] = "allowed"
    except OSError as error:
        result.update(status="denied" if error.errno in (errno.EACCES, errno.EPERM) else "error",
                      errno=error.errno, error=str(error))
    except BaseException as error:
        result.update(status="error", error=repr(error))
    finally:
        if conn:
            conn.close()
        if sock:
            sock.close()
    print(json.dumps(result), flush=True)


def scenario(name, sender, receiver, allowed, kind="dgram", pathname=False):
    # Root controller and unique pathname are test-only; never delete another file.
    address = f"{PREFIX}{os.getpid()}_{name}"
    if pathname:
        address = "/tmp/" + address + ".sock"
        require(not Path(address).exists(), "socket path exists")
    common = [sys.executable, "-B", str(Path(__file__).resolve()), "--kind", kind, "--address", address]
    proc = subprocess.Popen(common + ["--child", "receiver", "--profile", receiver],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    result = dict(name=name, sender=sender, receiver=receiver, expected_allow=allowed,
                  kind=kind, address_kind="pathname" if pathname else "abstract")
    try:
        require(bool(select.select([proc.stdout], [], [], 8)[0]), "receiver ready timeout")
        ready = json.loads(proc.stdout.readline())
        result["ready"] = ready
        require(ready["status"] == "ready", "receiver setup failed: " + str(ready))
        sent = subprocess.run(common + ["--child", "sender", "--profile", sender], capture_output=True, text=True, timeout=8)
        result["sender_stderr"] = sent.stderr
        require(sent.returncode == 0, "sender process failed")
        sending = json.loads(sent.stdout)
        result["send"] = sending
        text, errors = proc.communicate("receive\n" if sending["status"] == "allowed" else "stop\n", timeout=8)
        result.update(receive=json.loads(text), receiver_stderr=errors)
        require(proc.returncode == 0 and result["receive"]["status"] == "allowed", "receiver completion failed")
        require(sending["status"] == ("allowed" if allowed else "denied"), "unexpected permission decision")
        require(sending["stage"] in ("send", "connect"), "denial happened before peer permission operation")
        result["status"] = "pass"
    except BaseException as error:
        result.update(status="fail", error=repr(error))
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.communicate(timeout=3)
        if pathname and Path(address).is_socket():
            Path(address).unlink()
    return result


def run(out, compile_only):
    require(os.geteuid() == 0, "run bounded controller via sudo")
    require(Path("/proc/self/attr/current").read_text().strip() == "unconfined", "controller must be unconfined")
    out.mkdir(parents=True, exist_ok=False)
    profiles = Path("/sys/kernel/security/apparmor/profiles")
    before = sorted(profiles.read_text().splitlines())
    require(not any(PREFIX in p for p in before), "test profile prefix already exists")
    (out / "profiles-before.txt").write_text("\n".join(before) + "\n")
    definitions = dict(allow=policy("allow"), txdeny=policy("txdeny", "send"),
        rxdeny=policy("rxdeny", "receive"), connectdeny=policy("connectdeny", "connect"),
        acceptdeny=policy("acceptdeny", "accept"), explicit=policy("explicit", unconfined=True),
        oldallow=policy("oldallow", old=True), reload=policy("reload"),
        pathdeny=policy("pathdeny", file_deny=True), oldpathdeny=policy("oldpathdeny", old=True, file_deny=True))
    loaded = []
    state = dict(status="incomplete", kernel=os.uname().release, boot_id=Path("/proc/sys/kernel/random/boot_id").read_text().strip(),
                 tests=[], compile_only=compile_only, limitations=["no concurrent policy replacement", "no general security proof", "root test processes"])
    feature_root = Path("/sys/kernel/security/apparmor/features")
    state["kernel_features"] = {str(p.relative_to(feature_root)):p.read_text().strip()
                                for p in sorted(feature_root.rglob("*")) if p.is_file()}
    require(state["kernel_features"].get("network_v9/af_unix") == "yes", "fine UNIX v9 ABI is not advertised")
    for abi in ("5.0", "kernel-5.4-vanilla"):
        (out / ("abi-" + abi + ".txt")).write_bytes((Path("/etc/apparmor.d/abi")/abi).read_bytes())

    def parser(name, contents=None, action="-a"):
        path = out / (name + ".profile")
        if contents is not None:
            path.write_text(contents)
        argv = ["/usr/sbin/apparmor_parser", "-K", "-j", "1", action, str(path)]
        if compile_only:
            argv.insert(1, "-Q")
        p = subprocess.run(argv, capture_output=True, text=True, timeout=20)
        with (out / "parser.jsonl").open("a") as f:
            f.write(json.dumps(dict(argv=argv, returncode=p.returncode, stdout=p.stdout, stderr=p.stderr)) + "\n")
        require(p.returncode == 0, "parser failed for " + name + ": " + p.stderr)

    def case(*args, **kwargs):
        result = scenario(*args, **kwargs)
        state["tests"].append(result)
        (out / "result.json").write_text(json.dumps(state, indent=2) + "\n")
        require(result["status"] == "pass", "semantic case failed: " + result["name"])

    try:
        for name, text in definitions.items():
            # Include failed additions in cleanup: parser errors can be partial.
            if not compile_only:
                loaded.append(name)
            parser(name, text)
        if not compile_only:
            for a, b in (("none", "none"), ("allow", "none"), ("none", "allow"), ("allow", "allow"),
                         ("oldallow", "none"), ("none", "oldallow"), ("explicit", "none")):
                case("dgram-" + a + "-" + b, a, b, True)
            for a, b in (("txdeny", "none"), ("none", "rxdeny"), ("txdeny", "allow"), ("allow", "rxdeny"),
                         ("explicit+txdeny", "none"), ("txdeny+explicit", "none"), ("none", "explicit+rxdeny")):
                case("deny-" + a + "-" + b, a, b, False)
            for a, b, allow in (("none", "none", True), ("allow", "none", True), ("none", "allow", True),
                                ("connectdeny", "none", False), ("none", "acceptdeny", False)):
                case("stream-" + a + "-" + b, a, b, allow, kind="stream")
            for a, b in (("none", "none"), ("allow", "none"), ("none", "allow"), ("oldallow", "none"), ("none", "oldallow")):
                case("path-" + a + "-" + b, a, b, True, pathname=True)
            case("path-deny-new-abi", "pathdeny", "none", False, pathname=True)
            # Without RULE_MEDIATES_UNIX, profile_peer_perm() uses the coarse
            # AF/socket rule, not unix_fs_perm(). network unix therefore allows
            # this old-ABI send even though the pathname file rule denies rw.
            # Keep it as an explicit legacy-compatibility ALLOW control.
            case("path-legacy-coarse-allows", "oldpathdeny", "none", True, pathname=True)
            case("reload-initial", "reload", "none", True)
            parser("reload", policy("reload", "send"), "-r")
            case("reload-deny", "reload", "none", False)
            parser("reload", policy("reload"), "-r")
            case("reload-allow", "reload", "none", True)
        state["status"] = "pass"
    except BaseException as error:
        state.update(status="fail", error=repr(error))
    finally:
        cleanup_errors = []
        for name in reversed(loaded):
            # Remove only a currently present profile with our exact name.
            if any(line.startswith(PREFIX + name + " (") for line in profiles.read_text().splitlines()):
                try:
                    parser(name, action="-R")
                except BaseException as error:
                    cleanup_errors.append(repr(error))
        after = sorted(profiles.read_text().splitlines())
        (out / "profiles-after.txt").write_text("\n".join(after) + "\n")
        state["profiles_restored"] = after == before
        if after != before or cleanup_errors:
            state.update(status="fail", cleanup_errors=cleanup_errors)
        (out / "result.json").write_text(json.dumps(state, indent=2) + "\n")
    print(json.dumps(dict(status=state["status"], cases=len(state["tests"]), profiles_restored=state["profiles_restored"])), flush=True)
    return state["status"] == "pass"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path)
    p.add_argument("--compile-only", action="store_true")
    p.add_argument("--child", choices=("sender", "receiver"))
    p.add_argument("--profile")
    p.add_argument("--kind", choices=("dgram", "stream"))
    p.add_argument("--address")
    a = p.parse_args()
    if a.child:
        child(a)
        return
    require(a.output is not None, "output required")
    sys.exit(0 if run(a.output.resolve(), a.compile_only) else 1)


if __name__ == "__main__":
    main()
