# -*- mode: python ; coding: utf-8 -*-
#
# robot_bridge.spec — PyInstaller 빌드 스펙
#
# 빌드 명령:
#   pyinstaller robot_bridge.spec
#
# 출력: dist/robot-bridge  (단일 실행파일)

import sys
from pathlib import Path

block_cipher = None

a = Analysis(
    ['bridge.py'],
    pathex=[str(Path('.').resolve())],
    binaries=[],
    datas=[
        # 웹 UI 파일을 실행파일에 포함
        ('index.html',    '.'),
        ('packet_defs.py','.' ),
        ('slip_codec.py', '.'),
    ],
    hiddenimports=[
        # asyncio 관련
        'asyncio',
        'asyncio.selector_events',
        'asyncio.unix_events',
        # serial 관련
        'serial',
        'serial.tools',
        'serial.tools.list_ports',
        'serial_asyncio',
        # websockets 관련
        'websockets',
        'websockets.server',
        'websockets.legacy',
        'websockets.legacy.server',
        # 기타
        'http.server',
        'logging.handlers',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'tkinter',
        'matplotlib',
        'numpy',
        'PIL',
        'PyQt5',
        'wx',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='robot-bridge',
    debug=False,
    bootloader_ignore_signals=False,
    strip=True,           # 심볼 제거 → 파일 크기 감소
    upx=False,            # UPX 압축 (설치 시 True 가능)
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,         # 터미널 콘솔 표시
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
