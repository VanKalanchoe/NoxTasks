@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"
set "VENDORS_DIR=%ROOT_DIR%\vendors"

if not exist "%VENDORS_DIR%" mkdir "%VENDORS_DIR%"

echo ============================================================================
echo Installing NoxTasks dependencies into:
echo   %VENDORS_DIR%
echo ============================================================================

:: ----------------------------------------------------------------------------
:: 1. Taskflow (v4.1.0 header-only library)
:: ----------------------------------------------------------------------------
set "TASKFLOW_DIR=%VENDORS_DIR%\taskflow"

if not exist "%TASKFLOW_DIR%\taskflow\taskflow.hpp" (
    echo [1/1] Cloning Taskflow into "%TASKFLOW_DIR%"...
    if exist "%TASKFLOW_DIR%" rmdir /s /q "%TASKFLOW_DIR%"
    git clone --depth 1 https://github.com/taskflow/taskflow.git "%TASKFLOW_DIR%"
    if errorlevel 1 (
        echo ERROR: Failed to clone Taskflow from https://github.com/taskflow/taskflow.git.
        goto :fail
    )
) else (
    echo [1/1] Taskflow already present in "%TASKFLOW_DIR%", skipping clone.
)

echo.
echo All NoxTasks dependencies are ready in "%VENDORS_DIR%".
if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXTASKS_NO_PAUSE%"=="1" pause
exit /b 0

:fail
echo.
echo Dependency installation failed.
if /i not "%NRI_NO_PAUSE%"=="1" if /i not "%NOXTASKS_NO_PAUSE%"=="1" pause
exit /b 1
