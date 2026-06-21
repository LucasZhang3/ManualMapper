@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

set "GIT=C:\Program Files\Git\bin\git.exe"
set GIT_AUTHOR_NAME=Lucas Zhang
set GIT_AUTHOR_EMAIL=lucaszhang1118@gmail.com
set GIT_COMMITTER_NAME=Lucas Zhang
set GIT_COMMITTER_EMAIL=lucaszhang1118@gmail.com

"%GIT%" checkout --orphan temp-initial
if errorlevel 1 exit /b 1

"%GIT%" add -A
if errorlevel 1 exit /b 1

for /f "delims=" %%T in ('""%GIT%" write-tree"') do set "TREE=%%T"
if not defined TREE exit /b 1

for /f "delims=" %%C in ('""%GIT%" commit-tree %TREE% -m "initial commit"') do set "COMMIT=%%C"
if not defined COMMIT exit /b 1

"%GIT%" reset --hard %COMMIT%
if errorlevel 1 exit /b 1

"%GIT%" branch -M main
if errorlevel 1 exit /b 1

"%GIT%" log -1 --format=fuller
"%GIT%" push --force origin main
if errorlevel 1 exit /b 1

echo History reset complete.
exit /b 0
