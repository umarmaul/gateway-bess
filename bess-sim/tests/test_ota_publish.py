"""Test offline (tanpa broker/hardware) untuk tools/ota_publish.py: pembagian
chunk (1152 byte, base64 <=1536 char), dan tanda tangan Ed25519 yang benar-
benar terverifikasi -- lihat firmware/lib/bess_core/ota_logic.h untuk kontrak
yang harus disinkronkan."""
import base64
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import ota_publish  # noqa: E402


def test_chunk_firmware_ukuran_dan_urutan():
    data = bytes(range(256)) * 20   # 5120 byte
    chunks = ota_publish.chunk_firmware(data)
    assert len(chunks) == 5   # ceil(5120/1152) = 5
    assert all(len(c) <= ota_publish.CHUNK_BYTES for c in chunks)
    assert len(chunks[0]) == ota_publish.CHUNK_BYTES
    assert len(chunks[-1]) == 5120 - 4 * ota_publish.CHUNK_BYTES   # potongan terakhir lebih pendek
    assert b"".join(chunks) == data   # tak ada byte hilang/terduplikasi


def test_chunk_firmware_kelipatan_pas():
    data = b"\x42" * (ota_publish.CHUNK_BYTES * 3)
    chunks = ota_publish.chunk_firmware(data)
    assert len(chunks) == 3
    assert all(len(c) == ota_publish.CHUNK_BYTES for c in chunks)


def test_chunk_firmware_kosong():
    assert ota_publish.chunk_firmware(b"") == []


def test_chunk_data_base64_muat_batas_firmware():
    # base64 dari satu chunk PENUH (1152 byte) harus <=1536 char (OTA_CHUNK_MAX_BASE64)
    chunk = b"\xAB" * ota_publish.CHUNK_BYTES
    payload = ota_publish.build_chunk_payload("ota-1", 0, chunk)
    assert len(payload["data"]) <= 1536
    assert base64.b64decode(payload["data"]) == chunk
    assert payload["id"] == "ota-1"
    assert payload["index"] == 0


def test_build_manifest_field_dan_chunk_count():
    key = ota_publish.generate_keypair()
    data = bytes(range(256)) * 10   # 2560 byte
    m = ota_publish.build_manifest(data, ota_id="ota-x", version="bess-0.3.0-dev",
                                    private_key=key)
    assert m["id"] == "ota-x"
    assert m["image_type"] == "gateway"
    assert m["hardware"] == "bep-gateway-bess-v1"
    assert m["encoding"] == "base64"
    assert m["image_size"] == len(data)
    assert m["chunk_count"] == len(ota_publish.chunk_firmware(data))
    assert len(m["sha256"]) == 64
    bytes.fromhex(m["sha256"])   # tidak melempar -- hex valid
    sig = base64.b64decode(m["signature"])
    assert len(sig) == 64   # Ed25519 detached signature


def test_build_manifest_hardware_bisa_ditimpa_untuk_test_negatif():
    key = ota_publish.generate_keypair()
    m = ota_publish.build_manifest(b"x", ota_id="ota-y", version="v",
                                    private_key=key, hardware="bep-gateway-v1")
    assert m["hardware"] == "bep-gateway-v1"


def test_tanda_tangan_terverifikasi_dengan_kunci_publik_yang_benar():
    key = ota_publish.generate_keypair()
    data = b"firmware palsu untuk test" * 100
    digest = ota_publish.sha256_digest(data)
    sig = ota_publish.sign_digest(digest, key)
    assert ota_publish.verify_signature(digest, sig, key.public_key())


def test_tanda_tangan_gagal_dgn_kunci_publik_lain():
    key_a = ota_publish.generate_keypair()
    key_b = ota_publish.generate_keypair()
    digest = ota_publish.sha256_digest(b"data apa saja")
    sig = ota_publish.sign_digest(digest, key_a)
    assert not ota_publish.verify_signature(digest, sig, key_b.public_key())


def test_tanda_tangan_gagal_kalau_digest_berubah():
    # mendeteksi tampering: tanda tangan atas digest asli tidak boleh valid
    # untuk digest firmware yang sudah diubah walau cuma satu byte.
    key = ota_publish.generate_keypair()
    digest_asli = ota_publish.sha256_digest(b"firmware asli")
    digest_lain = ota_publish.sha256_digest(b"firmware asli diubah")
    sig = ota_publish.sign_digest(digest_asli, key)
    assert not ota_publish.verify_signature(digest_lain, sig, key.public_key())


def test_manifest_build_dan_verifikasi_end_to_end():
    # simulasikan sisi firmware: decode sha256 hex + signature base64, verifikasi
    # persis seperti crypto_sign_verify_detached(sig, sha256_raw, 32, pubkey).
    key = ota_publish.generate_keypair()
    data = bytes(range(200)) * 7
    m = ota_publish.build_manifest(data, ota_id="ota-z", version="v1", private_key=key)
    digest_raw = bytes.fromhex(m["sha256"])
    sig_raw = base64.b64decode(m["signature"])
    assert digest_raw == ota_publish.sha256_digest(data)
    assert ota_publish.verify_signature(digest_raw, sig_raw, key.public_key())


def test_gen_key_dan_muat_ulang_pem_konsisten(tmp_path):
    key = ota_publish.generate_keypair()
    path = tmp_path / "dev_key.pem"
    ota_publish.save_private_key_pem(key, path)
    pub_sebelum = ota_publish.public_key_b64(key)

    dimuat = ota_publish.load_private_key_pem(path)
    pub_sesudah = ota_publish.public_key_b64(dimuat)
    assert pub_sebelum == pub_sesudah

    # kunci privat yang dimuat ulang tetap bisa menandatangani & terverifikasi
    digest = ota_publish.sha256_digest(b"round-trip")
    sig = ota_publish.sign_digest(digest, dimuat)
    assert ota_publish.verify_signature(digest, sig, key.public_key())
