@echo off
rem =============================================================
rem Karamelo - Compilacao Rapida (C++20 otimizado + testes de unidade)
rem Pula a bateria demorada de 44 cores para iteracao agil no desenvolvimento
rem =============================================================
cd /d "%~dp0"
call "%~dp0compile.bat" --fast %*

