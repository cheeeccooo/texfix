@echo off
rem Aggiorna Texfix: dice che cosa cambia e chiede conferma prima di farlo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update.ps1"
pause
