# -*- coding: utf-8 -*-
"""SSH 키 등록 및 빌드 실행 스크립트"""
import paramiko
import sys
import os
import io

# Windows 콘솔 UTF-8 출력 강제
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

HOST = '192.168.49.7'
USER = 'bs'
PASS = '4321'
KEY_FILE = os.path.expanduser('~/.ssh/id_rsa_robot')
REMOTE_DIR = '/home/bs/robot-hmi'
BRIDGE_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'PC_GUI', 'bridge')

FILES_TO_UPLOAD = [
    'bridge.py',
    'packet_defs.py',
    'slip_codec.py',
    'index.html',
    'requirements.txt',
    'robot_bridge.spec',
    'build_linux.sh',
    'package_linux.sh',
]


def run(ssh, cmd, show=True):
    _, stdout, stderr = ssh.exec_command(cmd, get_pty=False)
    exit_code = stdout.channel.recv_exit_status()
    out = stdout.read().decode('utf-8', errors='replace')
    err = stderr.read().decode('utf-8', errors='replace')
    if show and out.strip():
        print(out, end='')
    if show and err.strip():
        print(err, end='', file=sys.stderr)
    return exit_code, out, err


def sep(title):
    print(f'\n{"="*52}')
    print(f'  {title}')
    print('='*52)


if __name__ == '__main__':
    # ── Step 1: 비밀번호로 접속 → SSH 키 등록 ─────────────────────
    sep('1/5  SSH 키 등록')
    try:
        ssh = paramiko.SSHClient()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        ssh.connect(HOST, username=USER, password=PASS, timeout=15)
        print(f'  [OK] {USER}@{HOST} 접속 성공')

        with open(KEY_FILE + '.pub', encoding='utf-8') as f:
            pubkey = f.read().strip()

        _, out, _ = run(ssh,
            'mkdir -p ~/.ssh && chmod 700 ~/.ssh && '
            f'grep -qF "{pubkey}" ~/.ssh/authorized_keys 2>/dev/null || '
            f'echo "{pubkey}" >> ~/.ssh/authorized_keys && '
            'chmod 600 ~/.ssh/authorized_keys && echo OK', False)
        print('  [OK] 공개키 등록 완료')

        _, info, _ = run(ssh, 'uname -srm && python3 --version 2>/dev/null || echo "python3 없음"', False)
        print(f'  OS: {info.strip()}')
        ssh.close()
    except Exception as e:
        print(f'  [FAIL] 오류: {e}')
        sys.exit(1)

    # ── Step 2: 키 인증으로 재접속 ───────────────────────────────────
    sep('2/5  SSH 키 인증 접속')
    try:
        ssh = paramiko.SSHClient()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        ssh.connect(HOST, username=USER, key_filename=KEY_FILE, timeout=15)
        print('  [OK] 키 인증 성공')
    except Exception as e:
        print(f'  [FAIL] {e}')
        sys.exit(1)

    # ── Step 3: 파일 업로드 ──────────────────────────────────────────
    sep('3/5  파일 업로드')
    run(ssh, f'mkdir -p {REMOTE_DIR}')
    sftp = ssh.open_sftp()
    for fname in FILES_TO_UPLOAD:
        local  = os.path.join(BRIDGE_DIR, fname)
        remote = f'{REMOTE_DIR}/{fname}'
        if os.path.exists(local):
            sftp.put(local, remote)
            print(f'  [OK] {fname}  ({os.path.getsize(local):,} bytes)')
        else:
            print(f'  [-]  {fname}  (없음)')
    sftp.close()

    # ── Step 4: 빌드 실행 ────────────────────────────────────────────
    sep('4/5  빌드 실행 (수 분 소요)')
    print()
    cmd = (
        f'cd {REMOTE_DIR} && '
        'chmod +x build_linux.sh && '
        'bash build_linux.sh 2>&1'
    )
    code, out, _ = run(ssh, cmd)
    if code == 0:
        print('\n  [OK] 빌드 성공!')
    else:
        print(f'\n  [FAIL] 빌드 실패 (exit={code})')
        ssh.close()
        sys.exit(1)

    # ── Step 5: 결과 확인 ────────────────────────────────────────────
    sep('5/5  결과 확인')
    _, ls, _ = run(ssh, f'ls -lh {REMOTE_DIR}/dist/robot-bridge 2>/dev/null || echo NOT_FOUND', False)
    if 'NOT_FOUND' not in ls:
        print(f'  [OK] {ls.strip()}')
        _, ft, _ = run(ssh, f'file {REMOTE_DIR}/dist/robot-bridge', False)
        print(f'  형식: {ft.strip()}')

        print('\n  패키지(tar.gz) 생성 중...')
        run(ssh, f'cd {REMOTE_DIR} && chmod +x package_linux.sh && bash package_linux.sh 2>&1')
        _, pkg, _ = run(ssh, f'ls -lh {REMOTE_DIR}/robot-hmi-linux-*.tar.gz 2>/dev/null', False)
        if pkg.strip():
            print(f'  [OK] 패키지: {pkg.strip()}')
    else:
        print('  [FAIL] 실행파일 없음')

    ssh.close()
    print('\n' + '='*52)
    print('  완료!')
    print(f'  실행파일: {REMOTE_DIR}/dist/robot-bridge')
    print(f'  바로 실행: ssh {USER}@{HOST} {REMOTE_DIR}/dist/robot-bridge')
    print('='*52)
