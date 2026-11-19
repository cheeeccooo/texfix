@echo off
rem Installer di Texfix per Windows: doppio clic. Dice che cosa installerebbe e chiede conferma prima di farlo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
pause
