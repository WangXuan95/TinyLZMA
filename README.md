 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

TinyLZMA
===========================

A minimal LZMA data compressor & decompressor. Only hundreds of lines of C.

　

LZMA is a lossless data compression method with a higher compression ratio than Deflate and BZIP. Several container formats supports LZMA:

- ".7z" and ".xz" format, whose default compression method is LZMA.
- ".zip" format also supports LZMA, although its default compression method is Deflate.
- ".lzma" is a very simple format for containing LZMA, which is legacy and gradually replaced by ".xz" format.

　

This code, TinyLZMA, supports 3 modes:

- compress a file into a ".zip" file (compress method = LZMA)
- compress a file into a ".lzma" file
- decompress a ".lzma" file

　

## Linux Build

On Linux, run command:

```bash
gcc src/*.c -o tlzma -O3 -Wall
```

or just run the script I provide:

```bash
sh build.sh
```

The output executable file is `tlzma`

　

## Windows Build

First, you should add the Microsoft C compiler `cl.exe` (from Visual Studio or Visual C++) to environment variables. Then run command:

```powershell
cl.exe  src\*.c  /Fetlzma.exe  /Ox
```

or just run the script I provide:

```powershell
.\build.bat
```

The output executable file is `tlzma.exe`

　

## Usage

Run TinyLZMA to show usage:

```
└─$ ./tlzma
  Tiny LZMA compressor & decompressor v0.2
  Source from https://github.com/WangXuan95/TinyLzma

  Usage :
     mode1 : decompress .lzma file :
       tlzma  <input_file(.lzma)>  <output_file>

     mode2 : compress a file to .lzma file :
       tlzma  <input_file>  <output_file(.lzma)>

     mode3 : compress a file to .zip file (use lzma algorithm) :
       tlzma  <input_file>  <output_file(.zip)>

  Note : on Windows, use 'tlzma.exe' instead of 'tlzma'
```

　

For example, you can compress the file `data3.txt` in directory `testdata` to `data3.txt.zip` using command:

```bash
./tlzma testdata/data3.txt data3.txt.zip
```

The outputting ".zip" file can be extracted by other compression software, such as 7ZIP, WinZip, WinRAR, etc.

　

You can also use following command to compress a file to a ".lzma" file :

```bash
./tlzma testdata/data3.txt data3.txt.lzma
```

To verify the outputting ".lzma" file, you can decompress it using the official "XZ-Utils" on Linux. You should firstly install it:

```bash
apt-get install xz-utils
```

Then use following command to decompress the ".lzma" file.

```bash
lzma -dk data3.txt.lzma
```

The decompressed `data3.txt` should be same as the original one.

　

You can also use following command to decompress a ".lzma" file :

```bash
./tlzma data3.txt.lzma data3.txt
```

　

## Notice

- TinyLZMA is verified on hundreds of files using automatic scripts.
- To be simpler, TinyLZMA loads the whole file data to memory to perform compresses/decompresses, so it is limited by memory capacity and cannot handle files that are too large.
- The search strategy of TinyLZMA's compressor is a simple hash-chain.
- The compression ratio of TinyLZMA's compressor is mostly like the `-1` to `-4` level of XZ-Utils's LZMA compressor. 
- The performance of TinyLZMA's compressor is mostly like the `-2` level of XZ-Utils's LZMA compressor. 

> :point_right: XZ-Utils's LZMA compressor has a total of 10 levels, from `-0` to `-9` . The larger, the higher the compression ratio, but the lower the performance. For example, if you want to use XZ-Utils to compress "a.txt" to "a.txt.lzma" using level 4, the command should be `lzma -zk -4 a.txt`

　

## Related Links

- wikipedia : https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm
- LZMA SDK : LZMA official code and specification : https://www.7-zip.org/sdk.html