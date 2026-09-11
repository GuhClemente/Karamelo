#!/usr/bin/env python3
"""
Gerador de ROMs Sintéticas Válidas para Bateria de Stress de Cores do Karamelo
Cria ROMs mínimas com headers válidos para que os cores Libretro consigam carregar e inicializar.
"""
import os
import struct
import zipfile

OUT_DIR = "test_roms"
os.makedirs(OUT_DIR, exist_ok=True)

def make_nes():
    # Header iNES (16 bytes): 'NES\x1a', 1x16k PRG, 1x8k CHR, Mapper 0
    header = b"NES\x1a\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    prg = bytearray(16384)
    # Loop infinito 6502 no reset vector: JMP $8000 (4C 00 80)
    prg[0] = 0x4C
    prg[1] = 0x00
    prg[2] = 0x80
    # Reset vector em 0xFFFC (offset 0x3FFC no PRG de 16k) -> 0x8000
    struct.pack_into("<H", prg, 0x3FFC, 0x8000)
    struct.pack_into("<H", prg, 0x3FFE, 0x8000)
    chr_rom = bytearray(8192)
    with open(os.path.join(OUT_DIR, "nes.nes"), "wb") as f:
        f.write(header + prg + chr_rom)

def make_gb():
    # Game Boy (32KB ROM)
    rom = bytearray(32768)
    # Entry point em 0x0100: NOP, JP 0x0150
    rom[0x0100] = 0x00
    rom[0x0101] = 0xC3
    rom[0x0102] = 0x50
    rom[0x0103] = 0x01
    # Nintendo Logo (0x0104 - 0x0133)
    logo = bytes([
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
    ])
    rom[0x0104:0x0104+len(logo)] = logo
    # Title "KARAMELO"
    title = b"KARAMELO\x00\x00\x00\x00\x00\x00\x00"
    rom[0x0134:0x0134+16] = title
    rom[0x0147] = 0x00 # ROM ONLY
    rom[0x0148] = 0x00 # 32KB
    rom[0x0149] = 0x00 # RAM none
    rom[0x014A] = 0x01 # Non-Japanese
    rom[0x014B] = 0x33 # Licensee
    # Checksum
    chk = 0
    for b in rom[0x0134:0x014D]:
        chk = (chk - b - 1) & 0xFF
    rom[0x014D] = chk
    with open(os.path.join(OUT_DIR, "gb.gb"), "wb") as f:
        f.write(rom)

def make_gba():
    # GBA ROM (64KB)
    rom = bytearray(65536)
    # ARM branch no entry point: b 0x080000C0
    struct.pack_into("<I", rom, 0, 0xEA00002E)
    # Nintendo Logo GBA (156 bytes)
    gba_logo = bytes([
        0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21, 0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
        0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21, 0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20,
        0x10, 0x46, 0x4A, 0x4A, 0xF8, 0x27, 0x31, 0xEC, 0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF,
        0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0x2B, 0x94, 0x58, 0xAC, 0x00, 0xE4, 0x90, 0x19, 0xB0,
        0x0F, 0x0A, 0x26, 0xBE, 0x6A, 0xC0, 0x2E, 0x7B, 0xC1, 0x38, 0x96, 0x3A, 0x1A, 0x0B, 0x16, 0xD4,
        0x56, 0xDF, 0xBE, 0x1E, 0x99, 0x80, 0xD8, 0xE8, 0xBC, 0xA4, 0xBE, 0xC5, 0x90, 0x48, 0x6E, 0x63,
        0x89, 0xB4, 0x59, 0x99, 0xD9, 0x97, 0x24, 0x11, 0x26, 0xC7, 0x05, 0x0C, 0xA4, 0x1E, 0x77, 0x05,
        0xDF, 0x20, 0x64, 0xFE, 0xBF, 0x8F, 0x49, 0xD8, 0x28, 0x37, 0x19, 0x76, 0x1B, 0x7B, 0x9A, 0x66,
        0x2B, 0x28, 0x43, 0x06, 0x24, 0xB8, 0x94, 0xE2, 0xB7, 0xA8, 0x83, 0x50, 0x89, 0x00, 0xC4, 0x5A,
        0x8A, 0x8E, 0x06, 0x72, 0x76, 0xFD, 0x5C, 0x74, 0x29, 0x28, 0x5C, 0x66
    ])
    rom[0x04:0x04+len(gba_logo)] = gba_logo
    rom[0xA0:0xA0+12] = b"KARAMELO\x00\x00\x00\x00"
    rom[0xAC:0xAC+4] = b"AKME" # Game Code
    rom[0xB0:0xB0+2] = b"01"   # Maker Code
    rom[0xB2] = 0x96          # Fixed value
    rom[0xBD] = 0x00          # Checksum placeholder
    chk = 0
    for b in rom[0xA0:0xBD]:
        chk = (chk - b) & 0xFF
    chk = (chk - 0x19) & 0xFF
    rom[0xBD] = chk
    with open(os.path.join(OUT_DIR, "gba.gba"), "wb") as f:
        f.write(rom)

def make_genesis():
    # Sega Mega Drive / Genesis (64KB)
    rom = bytearray(65536)
    # Vectors: SSP = 0x00FFFE00, PC = 0x00000200
    struct.pack_into(">I", rom, 0x00, 0x00FFFE00)
    struct.pack_into(">I", rom, 0x04, 0x00000200)
    # Loop em 0x0200: BRA.S * (60 FE)
    rom[0x0200] = 0x60
    rom[0x0201] = 0xFE
    # Header em 0x0100
    rom[0x0100:0x0110] = b"SEGA MEGA DRIVE "
    rom[0x0110:0x0120] = b"(C)GUHF 2026.SEP"
    rom[0x0120:0x0150] = b"KARAMELO TEST ROM                              "
    rom[0x0150:0x0180] = b"KARAMELO TEST ROM                              "
    rom[0x0180:0x018E] = b"GM 00000000-00"
    # ROM Start and End
    struct.pack_into(">I", rom, 0x01A0, 0x00000000)
    struct.pack_into(">I", rom, 0x01A4, 0x0000FFFF)
    # RAM Start and End
    rom[0x01B0:0x01B2] = b"RA"
    struct.pack_into(">I", rom, 0x01B4, 0x00FF0000)
    struct.pack_into(">I", rom, 0x01B8, 0x00FFFFFF)
    rom[0x01F0:0x01F3] = b"JUE"
    with open(os.path.join(OUT_DIR, "genesis.gen"), "wb") as f:
        f.write(rom)
    with open(os.path.join(OUT_DIR, "32x.32x"), "wb") as f:
        f.write(rom)

def make_sms():
    # Sega Master System (32KB)
    rom = bytearray(32768)
    # Reset em 0x0000: JP 0x0000 (C3 00 00)
    rom[0x0000] = 0xC3
    rom[0x0001] = 0x00
    rom[0x0002] = 0x00
    # Header SMS em 0x7FF0
    rom[0x7FF0:0x7FF8] = b"TMR SEGA"
    rom[0x7FFA] = 0x00 # Checksum
    rom[0x7FFB] = 0x00
    rom[0x7FFC] = 0x00
    rom[0x7FFD] = 0x00
    rom[0x7FFE] = 0x00
    rom[0x7FFF] = 0x4C # Region Export 32KB
    with open(os.path.join(OUT_DIR, "sms.sms"), "wb") as f:
        f.write(rom)

def make_atari2600():
    # Atari 2600 (4096 bytes)
    rom = bytearray(4096)
    # JMP $F000 em 0x0000
    rom[0x00] = 0x4C
    rom[0x01] = 0x00
    rom[0x02] = 0xF0
    # Reset vector em 0x0FFC -> 0xF000
    struct.pack_into("<H", rom, 0x0FFC, 0xF000)
    with open(os.path.join(OUT_DIR, "atari2600.a26"), "wb") as f:
        f.write(rom)

def make_atari7800():
    # Atari 7800 (128 bytes header + 32KB)
    header = bytearray(128)
    header[0x01:0x0A] = b"ATARI7800"
    header[0x11:0x31] = b"KARAMELO TEST ROM               "
    struct.pack_into(">I", header, 0x31, 32768)
    rom = bytearray(32768)
    # 6502 reset vector em 0x7FFC
    struct.pack_into("<H", rom, 0x7FFC, 0x8000)
    with open(os.path.join(OUT_DIR, "atari7800.a78"), "wb") as f:
        f.write(header + rom)

def make_c64():
    # C64 PRG (loading address 0x0801 + simple BASIC stub)
    prg = bytearray([0x01, 0x08, 0x0B, 0x08, 0x0A, 0x00, 0x9E, 0x32, 0x30, 0x36, 0x34, 0x00, 0x00, 0x00])
    with open(os.path.join(OUT_DIR, "c64.prg"), "wb") as f:
        f.write(prg)

def make_coleco():
    # ColecoVision (32KB)
    rom = bytearray(32768)
    rom[0x00] = 0xAA
    rom[0x01] = 0x55
    with open(os.path.join(OUT_DIR, "coleco.col"), "wb") as f:
        f.write(rom)

def make_pce():
    # PC Engine (256KB)
    rom = bytearray(262144)
    # Reset vector 6502 em 0x1FFFC
    struct.pack_into("<H", rom, 0x3FFFC, 0xE000)
    with open(os.path.join(OUT_DIR, "pce.pce"), "wb") as f:
        f.write(rom)

def make_wswan():
    # WonderSwan (64KB)
    rom = bytearray(65536)
    # Reset vector V30 em 0xFFF0: EA 00 00 00 F0 (JMP F000:0000)
    rom[0xFFF0] = 0xEA
    rom[0xFFF1] = 0x00
    rom[0xFFF2] = 0x00
    rom[0xFFF3] = 0x00
    rom[0xFFF4] = 0xF0
    with open(os.path.join(OUT_DIR, "wswan.ws"), "wb") as f:
        f.write(rom)

def make_ngp():
    # Neo Geo Pocket (64KB)
    rom = bytearray(65536)
    rom[0x00:0x0C] = b"COPYRIGHT BY SNK CORPORATION"
    with open(os.path.join(OUT_DIR, "ngp.ngp"), "wb") as f:
        f.write(rom)

def make_n64():
    # N64 ROM (4KB header)
    rom = bytearray(4096 * 4)
    # Magic bytes 0x80371240
    struct.pack_into(">I", rom, 0x00, 0x80371240)
    rom[0x20:0x34] = b"KARAMELO TEST N64   "
    with open(os.path.join(OUT_DIR, "n64.z64"), "wb") as f:
        f.write(rom)

def make_nds():
    # NDS ROM (minimal 512-byte header)
    rom = bytearray(512 * 4)
    rom[0x00:0x0C] = b"KARAMELO\x00\x00\x00\x00"
    rom[0x0C:0x10] = b"AKME"
    rom[0x10:0x12] = b"01"
    struct.pack_into("<I", rom, 0x20, 0x00000800) # ARM9 ROM offset
    struct.pack_into("<I", rom, 0x28, 0x02000000) # ARM9 RAM address
    struct.pack_into("<I", rom, 0x2C, 0x00000800) # ARM9 size
    struct.pack_into("<I", rom, 0x30, 0x00001000) # ARM7 ROM offset
    struct.pack_into("<I", rom, 0x38, 0x02380000) # ARM7 RAM address
    struct.pack_into("<I", rom, 0x3C, 0x00000800) # ARM7 size
    with open(os.path.join(OUT_DIR, "nds.nds"), "wb") as f:
        f.write(rom)

def make_zip_dummy(name):
    # Zip dummy com um arquivo texto interno
    path = os.path.join(OUT_DIR, name)
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("test.txt", "Karamelo test payload\n")

def make_cue_bin(base_name):
    bin_path = os.path.join(OUT_DIR, f"{base_name}.bin")
    cue_path = os.path.join(OUT_DIR, f"{base_name}.cue")
    # 16 setores de 2352 bytes (áudio/dados dummy)
    with open(bin_path, "wb") as f:
        f.write(bytearray(2352 * 16))
    cue_content = f'FILE "{base_name}.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
    with open(cue_path, "w") as f:
        f.write(cue_content)

def make_iso(name):
    # ISO-9660 dummy (32KB de zeros + Primary Volume Descriptor)
    iso_path = os.path.join(OUT_DIR, name)
    buf = bytearray(2048 * 20) # 40KB
    # Setor 16 (offset 32768): PVD
    pvd = 16 * 2048
    buf[pvd] = 0x01
    buf[pvd+1:pvd+6] = b"CD001"
    buf[pvd+6] = 0x01
    buf[pvd+40:pvd+72] = b"KARAMELO_TEST_DISC              "
    with open(iso_path, "wb") as f:
        f.write(buf)

def main():
    print("Gerando ROMs sintéticas em test_roms/...")
    make_nes()
    make_gb()
    make_gba()
    make_genesis()
    make_sms()
    make_atari2600()
    make_atari7800()
    make_c64()
    make_coleco()
    make_pce()
    make_wswan()
    make_ngp()
    make_n64()
    make_nds()
    
    # Dummies para arcade e outros
    make_zip_dummy("arcade.zip")
    make_zip_dummy("neogeo.zip")
    make_zip_dummy("dosbox.zip")
    make_cue_bin("disc")
    make_iso("game.iso")
    print("ROMs geradas com sucesso!")

if __name__ == "__main__":
    main()
