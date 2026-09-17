@echo off
chcp 65001 >nul
echo ==========================================
echo   Fn Key Capture Diagnostic (60 seconds)
echo ==========================================
echo.
echo   After "listening" appears, press:
echo     1) letter  A   twice  (control test)
echo     2) right   Fn  key    five times
echo.
echo ------------------------------------------
"C:\Users\weilantianhai\.workbuddy\binaries\python\versions\3.13.12\python.exe" "D:\program\keyboardrecord\tools\fn-diag.py" 60
echo ------------------------------------------
echo.
echo Done. Result saved to fn-diag.log
echo Send the log content to the assistant.
pause
