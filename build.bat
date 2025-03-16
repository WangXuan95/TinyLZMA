del tinyZZZ
del tinyZZZ.exe
cls
gcc     src\*.c src\GZIP\*.c src\ZSTD\*.c src\LZMA\*.c -O2 -Wall -o tinyZZZ.exe
wsl gcc src/*.c src/GZIP/*.c src/ZSTD/*.c src/LZMA/*.c -O2 -Wall -o tinyZZZ