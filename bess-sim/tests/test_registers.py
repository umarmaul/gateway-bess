import pytest
from bess_sim.registers import RegisterMap, ModbusError, ID_P_SET, ID_RATED, ID_SOC

def test_defaults_sesuai_pdf():
    r = RegisterMap()
    assert r.get(3050) == 50        # total active power default 50 (5.0%)
    assert r.get(3052) == 1000      # power factor default
    assert r.get(3062) == 2000      # active rate of change
    assert r.get(3146) == 500       # rated power 50.0 kW
    assert r.get(3147) == 230       # rated phase voltage
    assert r.get(3157) == 0         # on-grid
    assert r.get(3182) == 1         # module address
    assert r.get_signed(3081) == -100  # grid under-frequency level 1
    assert r.get(1000) >= 0         # versi ARM ada

def test_read_block_telemetri():
    r = RegisterMap()
    vals = r.read_block(1050, 59)   # 1050..1108 seluruh area analog
    assert len(vals) == 59

def test_read_undefined_id_error_2():
    r = RegisterMap()
    with pytest.raises(ModbusError) as e:
        r.read_block(1109, 1)
    assert e.value.code == 2

def test_read_meliputi_lubang_error_2():
    r = RegisterMap()
    with pytest.raises(ModbusError):
        r.read_block(1105, 10)      # 1109+ tidak terdefinisi

def test_write_dalam_range():
    r = RegisterMap()
    r.write_single(3050, 100)
    assert r.get(3050) == 100

def test_write_negatif_dua_komplemen():
    r = RegisterMap()
    r.write_single(3050, 0x10000 - 200)   # -20.0%
    assert r.get_signed(3050) == -200

def test_write_di_luar_range_error_3():
    r = RegisterMap()
    with pytest.raises(ModbusError) as e:
        r.write_single(3050, 1300)  # max 1200
    assert e.value.code == 3

def test_write_register_readonly_error_2():
    r = RegisterMap()
    with pytest.raises(ModbusError) as e:
        r.write_single(1060, 1)     # telemetri = read-only
    assert e.value.code == 2

def test_set_raw_boleh_tulis_readonly():
    r = RegisterMap()
    r.set_signed(1060, -50)
    assert r.get_signed(1060) == -50

def test_write_block_happy_path():
    """Happy path: tulis 2 register berurutan dalam range."""
    r = RegisterMap()
    r.write_block(3050, [100, 200])
    assert r.get(3050) == 100
    assert r.get(3051) == 200

def test_write_block_atomik_rollback_range_error():
    """Blok berisi nilai out-of-range → ModbusError(3), register pertama TIDAK berubah."""
    r = RegisterMap()
    initial_3050 = r.get(3050)
    initial_3051 = r.get(3051)

    # Coba tulis [100, 9999] ke 3050..3051; 3051 out of range (-1200..1200)
    with pytest.raises(ModbusError) as e:
        r.write_block(3050, [100, 9999])
    assert e.value.code == 3

    # Verifikasi tidak ada yang berubah (atomik)
    assert r.get(3050) == initial_3050
    assert r.get(3051) == initial_3051

def test_write_block_atomik_rollback_readonly_error():
    """Blok menyentuh register read-only → ModbusError(2), tidak ada yang berubah."""
    r = RegisterMap()
    initial_1050 = r.get(1050)

    # Coba tulis ke 1050 (read-only telemetri)
    with pytest.raises(ModbusError) as e:
        r.write_block(1050, [100])
    assert e.value.code == 2

    # Verifikasi tidak ada yang berubah
    assert r.get(1050) == initial_1050

def test_write_block_atomik_rollback_undefined_error():
    """Blok menyentuh register undefined → ModbusError(2), tidak ada yang berubah."""
    r = RegisterMap()
    initial_3050 = r.get(3050)

    # Coba tulis ke 1109 (undefined, right after telemetri)
    with pytest.raises(ModbusError) as e:
        r.write_block(1109, [100])
    assert e.value.code == 2

    # Verifikasi 3050 tidak berubah
    assert r.get(3050) == initial_3050
