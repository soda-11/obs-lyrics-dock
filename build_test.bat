@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >/dev/null 2>&1
cl test_qr.cpp /I"src" /std:c++17 /EHsc /Fe:test_qr.exe /nologo
