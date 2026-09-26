@echo off
cd /d D:\Devin\ui-visual-overhaul\build-native\devin
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" -j8 --output-on-failure %*
