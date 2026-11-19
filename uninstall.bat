@echo off
rem Disinstalla Texfix: chiede pezzo per pezzo che cosa togliere.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
pause
