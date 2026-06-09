@echo off
REM build_docker.bat
REM Windows에서 Docker를 이용해 Linux 실행파일 빌드
REM
REM 사전 요건: Docker Desktop 설치 및 실행 중
REM 사용법:    build_docker.bat

echo ========================================
echo   Robot Bridge - Linux 크로스빌드
echo   (Docker 사용)
echo ========================================

cd /d "%~dp0"

if not exist dist mkdir dist

echo [1/3] Docker 이미지 빌드 중...
docker build -f Dockerfile.build -t robot-bridge-builder . || goto :error

echo [2/3] Linux 실행파일 생성 중...
docker run --rm -v "%CD%\dist:/out" robot-bridge-builder || goto :error

echo [3/3] 완료 확인...
if exist "dist\robot-bridge" (
    echo.
    echo ^=^= 빌드 성공! ^=^=
    echo    파일: dist\robot-bridge
    echo    Linux 머신으로 복사 후 실행:
    echo      chmod +x robot-bridge
    echo      ./robot-bridge
) else (
    echo 빌드 실패 - dist\robot-bridge 없음
    goto :error
)

pause
exit /b 0

:error
echo.
echo 오류 발생! Docker가 실행 중인지 확인하세요.
pause
exit /b 1
