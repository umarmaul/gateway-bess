"""Bench: kirim OTA gateway-bess lewat MQTT dengan tanda tangan Ed25519.

Memerankan server cloud untuk sub-proyek G (lihat
firmware/lib/bess_core/ota_logic.h + firmware/README.md §OTA): menandatangani
digest SHA-256 32 byte (BUKAN manifest/image mentah) dengan Ed25519, lalu
mengirim manifest lalu chunk satu per satu ke topic
device/<gw>/ota/{manifest,chunk}, menunggu ack di device/<gw>/ota/ack sebelum
mengirim chunk berikutnya (flow-control: satu chunk in-flight, sama seperti
firmware). hardware string SENGAJA "bep-gateway-bess-v1" -- beda dari
"bep-gateway-v1" milik gateway DCON tim, supaya salah kirim tidak ter-flash
begitu saja walau tanda tangannya sah untuk kunci yang sama.

Jalankan lewat uv TANPA menambah dependensi proyek (paho-mqtt cuma dipakai
alat ini, bukan bess-sim sendiri):

    uv run --with paho-mqtt --with cryptography python tools/ota_publish.py \\
        --gw 58E6C5218C78 --host mqtt-dev.bepbatt.id --user USER --passwd PASS \\
        --key dev_key.pem --version bess-0.3.0 --firmware firmware.bin

Buat keypair dev (privat PEM lokal + publik base64 untuk secrets.h):

    uv run --with cryptography python tools/ota_publish.py --gen-key dev_key.pem
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import sys
import time
import uuid
from pathlib import Path

# Sinkron dengan firmware/lib/bess_core/ota_logic.h -- JANGAN diubah sendiri
# di sini tanpa mengubah firmware juga (kontrak dua sisi).
CHUNK_BYTES = 1152          # OTA_CHUNK_MAX_BYTES
HARDWARE_DEFAULT = "bep-gateway-bess-v1"   # OTA_HARDWARE_ID
IMAGE_TYPE = "gateway"                      # OTA_IMAGE_TYPE


# ---------------------------------------------------------------------------
# Fungsi murni -- diuji offline di tests/test_ota_publish.py tanpa broker/HW.
# ---------------------------------------------------------------------------

def chunk_firmware(data: bytes, chunk_size: int = CHUNK_BYTES) -> list[bytes]:
    """Bagi firmware jadi potongan <=chunk_size byte, berurutan dari 0.

    Potongan terakhir boleh lebih pendek (tidak di-pad) -- persis definisi
    chunk_count = ceil(image_size / chunk_size) di ota_logic.h.
    """
    return [data[i:i + chunk_size] for i in range(0, len(data), chunk_size)]


def sha256_digest(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def sign_digest(digest: bytes, private_key) -> bytes:
    """private_key: cryptography Ed25519PrivateKey. Tanda tangan Ed25519
    DETACHED atas 32 byte digest -- bukan atas manifest JSON atau image
    mentah (lihat ota_logic.h untuk alasan)."""
    return private_key.sign(digest)


def verify_signature(digest: bytes, signature: bytes, public_key) -> bool:
    """Verifikasi independen (dipakai test) -- firmware yang sebenarnya
    memverifikasi ini pakai libsodium, bukan modul ini."""
    from cryptography.exceptions import InvalidSignature
    try:
        public_key.verify(signature, digest)
        return True
    except InvalidSignature:
        return False


def build_manifest(data: bytes, *, ota_id: str, version: str, private_key,
                    hardware: str = HARDWARE_DEFAULT) -> dict:
    """Bangun manifest OTA sesuai kontrak ota_logic.h::otaParseManifest."""
    digest = sha256_digest(data)
    signature = sign_digest(digest, private_key)
    return {
        "id": ota_id,
        "image_type": IMAGE_TYPE,
        "hardware": hardware,
        "version": version,
        "encoding": "base64",
        "image_size": len(data),
        "sha256": digest.hex(),
        "signature": base64.b64encode(signature).decode(),
        "chunk_count": math.ceil(len(data) / CHUNK_BYTES) if data else 0,
    }


def build_chunk_payload(ota_id: str, index: int, chunk: bytes) -> dict:
    return {"id": ota_id, "index": index, "data": base64.b64encode(chunk).decode()}


def generate_keypair():
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    return Ed25519PrivateKey.generate()


def public_key_b64(private_key) -> str:
    from cryptography.hazmat.primitives import serialization
    raw = private_key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw, format=serialization.PublicFormat.Raw)
    return base64.b64encode(raw).decode()


def save_private_key_pem(private_key, path: Path) -> None:
    from cryptography.hazmat.primitives import serialization
    pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption())
    path.write_bytes(pem)


def load_private_key_pem(path: Path):
    from cryptography.hazmat.primitives import serialization
    return serialization.load_pem_private_key(path.read_bytes(), password=None)


# ---------------------------------------------------------------------------
# Jaringan (paho-mqtt di-import lokal supaya modul ini tetap bisa di-import
# untuk test tanpa paho-mqtt terpasang -- lihat catatan di docstring modul).
# ---------------------------------------------------------------------------

def _connect(args) -> "object":
    import paho.mqtt.client as mqtt
    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if args.user:
        cli.username_pw_set(args.user, args.passwd)
    cli.connect(args.host, args.port, 60)
    return cli


def run_ota(args) -> int:
    data = Path(args.firmware).read_bytes()
    if not data:
        print("firmware kosong -- tidak ada yang dikirim", file=sys.stderr)
        return 1
    private_key = load_private_key_pem(Path(args.key))
    ota_id = args.id or f"ota-{int(time.time())}-{uuid.uuid4().hex[:6]}"
    manifest = build_manifest(data, ota_id=ota_id, version=args.version,
                               private_key=private_key, hardware=args.hardware)
    chunks = chunk_firmware(data)
    assert len(chunks) == manifest["chunk_count"], "pembagian chunk tidak sinkron dgn manifest"

    cli = _connect(args)
    t_manifest = f"device/{args.gw}/ota/manifest"
    t_chunk = f"device/{args.gw}/ota/chunk"
    t_ack = f"device/{args.gw}/ota/ack"
    t_status = f"device/{args.gw}/ota/status"

    last_ack: dict = {}

    def on_message(_c, _u, msg):
        try:
            doc = json.loads(msg.payload)
        except json.JSONDecodeError:
            return
        if msg.topic == t_ack:
            last_ack["doc"] = doc
            print(f"ack: kind={doc.get('kind')} index={doc.get('index')} "
                  f"result={doc.get('result')} detail={doc.get('detail')!r}")
        elif msg.topic == t_status:
            print(f"status: {doc.get('state')} detail={doc.get('detail')!r} "
                  f"received_bytes={doc.get('received_bytes')}")

    cli.on_message = on_message
    cli.subscribe([(t_ack, 1), (t_status, 1)])
    cli.loop_start()

    def wait_ack(kind: str, index):
        last_ack.pop("doc", None)
        deadline = time.time() + args.timeout
        while time.time() < deadline:
            doc = last_ack.get("doc")
            if (doc and doc.get("id") == ota_id and doc.get("kind") == kind and
                    (index is None or doc.get("index") == index)):
                return doc
            time.sleep(0.05)
        return None

    print(f"mengirim manifest id={ota_id} image_size={manifest['image_size']} "
          f"chunk_count={manifest['chunk_count']} hardware={manifest['hardware']}")
    cli.publish(t_manifest, json.dumps(manifest), qos=1)
    ack = wait_ack("manifest", None)
    if not ack or ack.get("result") != "accepted":
        print(f"manifest ditolak/timeout: {ack}", file=sys.stderr)
        cli.loop_stop()
        return 1

    for index, chunk in enumerate(chunks):
        payload = build_chunk_payload(ota_id, index, chunk)
        tries_left = args.retries
        while True:
            cli.publish(t_chunk, json.dumps(payload), qos=1)
            ack = wait_ack("chunk", index)
            # "accepted" mencakup duplicate (idempotent) -- keduanya berarti
            # chunk ini sudah tertulis, lanjut ke berikutnya.
            if ack and ack.get("result") == "accepted":
                break
            tries_left -= 1
            if tries_left < 0:
                print(f"chunk {index} gagal setelah {args.retries} percobaan ulang: {ack}",
                      file=sys.stderr)
                cli.loop_stop()
                return 1
            print(f"chunk {index} timeout/ditolak ({ack}), mencoba ulang "
                  f"({tries_left} sisa)...")
        if (index + 1) % 50 == 0 or index == len(chunks) - 1:
            print(f"chunk {index + 1}/{len(chunks)} terkirim")

    print("selesai kirim semua chunk, menunggu status akhir (restarting/installed)...")
    time.sleep(min(5.0, args.timeout))
    cli.loop_stop()
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen-key", metavar="PATH",
                     help="buat keypair Ed25519 dev, simpan privat ke PATH (PEM), "
                          "cetak publik base64 (utk OTA_ED25519_PUBKEY_B64 di secrets.h)")
    ap.add_argument("--host", default="mqtt-dev.bepbatt.id")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user"); ap.add_argument("--passwd")
    ap.add_argument("--gw", help="MAC gateway (device id), mis. 58E6C5218C78")
    ap.add_argument("--hardware", default=HARDWARE_DEFAULT)
    ap.add_argument("--version", default="bess-0.3.0-dev")
    ap.add_argument("--key", help="path private key PEM Ed25519 (lihat --gen-key)")
    ap.add_argument("--firmware", help="path firmware.bin (image gateway-bess)")
    ap.add_argument("--id", help="id job OTA (default: dibuat otomatis dari waktu)")
    ap.add_argument("--timeout", type=float, default=15.0,
                     help="detik tunggu ack per manifest/chunk sebelum retry/menyerah")
    ap.add_argument("--retries", type=int, default=3,
                     help="percobaan ulang per chunk (index sama) sebelum menyerah")
    args = ap.parse_args()

    if args.gen_key:
        priv = generate_keypair()
        save_private_key_pem(priv, Path(args.gen_key))
        print(f"private key disimpan: {args.gen_key} (JANGAN commit)")
        print(f"public key (OTA_ED25519_PUBKEY_B64): {public_key_b64(priv)}")
        return 0

    if not args.gw or not args.key or not args.firmware:
        ap.error("--gw, --key, dan --firmware wajib kecuali memakai --gen-key")
    return run_ota(args)


if __name__ == "__main__":
    raise SystemExit(main())
