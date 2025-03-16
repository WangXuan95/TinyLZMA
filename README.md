 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

TinyZZZ
===========================

TinyZZZ is a simple, standalone data compressor/decompressor which supports several popular data compression algorithms, including GZIP, ZSTD, and LZMA. These algorithms are written in C language, unlike the official code implementation, this code mainly focuses on simplicity and easy to understand.

TinyZZZ currently supports:
  - GZIP compress: compress a file into a ".gz" file
  - ZSTD decompress: decompress a ".zst" file to a file
  - LZMA decompress: decompress a ".lzma" file to a file
  - LZMA compress: compress a file into a ".lzma" file
  - LZMA compress: compress a file into a ".zip" file (compress method=LZMA)

　

### About GZIP

GZIP is an old lossless data compression algorithm which has excellent compatibility. The file name suffix of compressed GZIP file is ".gz"

### About ZSTD

ZSTD (Zstandard) is an new lossless data compression algorithm with high compression ratio and high decompression speed. The file name suffix of compressed ZSTD file is ".zstd"

### About LZMA

LZMA is a lossless data compression algorithm with higher compression ratio than GZIP, BZIP, and ZSTD. Several archive container formats supports LZMA:

- ".7z" and ".xz" format, whose default compression method is LZMA.
- ".zip" format also supports LZMA, although its default compression method is Deflate.
- ".lzma" is a very simple format for containing LZMA, which is legacy and gradually replaced by ".xz" format.

　

　

## Linux Build

On Linux, run command:

```bash
gcc src/*.c src/GZIP/*.c src/ZSTD/*.c src/LZMA/*.c -O2 -Wall -o tinyZZZ
```

The output Linux binary file is [tinyZZZ](./tinyZZZ)

　

　

## Windows Build

If you installed MinGW in Windows, run command to compile:

```powershell
gcc src\*.c src\GZIP\*.c src\ZSTD\*.c src\LZMA\*.c -O2 -Wall -o tinyZZZ.exe
```

The output executable file is [tinyZZZ.exe](./tinyZZZ.exe)

　

　

## Usage

Run TinyZZZ to show usage:

```
└─$ ./tinyZZZ
|----------------------------------------------------------------------|
|  Usage :                                                             |
|   1. decompress a GZIP file                                          |
|        (not yet supported!)                                          |
|   2. compress a file to GZIP file                                    |
|        tinyZZZ.exe -c --gzip <input_file> <output_file(.gz)>         |
|   3. decompress a ZSTD file                                          |
|        tinyZZZ.exe -d --zstd <input_file(.zst)> <output_file>        |
|   4. compress a file to ZSTD file                                    |
|        (not yet supported!)                                          |
|   5. decompress a LZMA file                                          |
|        tinyZZZ.exe -d --lzma <input_file(.lzma)> <output_file>       |
|   6. compress a file to LZMA file                                    |
|        tinyZZZ.exe -c --lzma <input_file> <output_file(.lzma)>       |
|   7. compress a file to LZMA and pack to a .zip container file       |
|        tinyZZZ.exe -c --lzma --zip <input_file> <output_file(.zip)>  |
|----------------------------------------------------------------------|
```

　

### Example Usage

**Example1**: compress the file `data3.txt` in directory `testdata` to `data3.zip` using command:

```bash
./tinyZZZ -c --lzma --zip testdata/data3.txt data3.zip
```

The outputting ".zip" file can be extracted by other compression software, such as [7ZIP](https://www.7-zip.org), WinZip, WinRAR, etc.

**Example2**: Use following command to compress a file to a ".lzma" file :

```bash
./tinyZZZ -c --lzma testdata/data3.txt data3.txt.lzma
```

Besides TinyZZZ itself, you can use other LZMA official softwares to decompress ".lzma" file. See [How to decompress .lzma file](#dec_en)

**Example of mode1**: You can use following command to decompress a ".lzma" file :

```bash
./tinyZZZ -d --lzma data3.txt.lzma data3.txt
```

　

　

## <span id="dec_en">Appendix: How to decompress ".lzma" file</span>

### on Windows

On Windows, you can use the [official 7ZIP/LZMA software](https://www.7-zip.org/sdk.html) to decompress the generated ".lzma" file. To get it, download the "LZMA SDK", extract it. In the "bin" directory, you can see "lzma.exe".

To decompress a ".lzma" file, run command as format:

```powershell
.\lzma.exe d [input_lzma_file] [output_file]
```

### on Linux

On Linux, you can decompress ".lzma" file using the official "p7zip" software. You should firstly install it:

```bash
apt-get install p7zip
```

Then use following command to decompress the ".lzma" file.

```bash
7z x [input_lzma_file]
```

It may report a error : *"ERROR: There are some data after the end of the payload data"* . Just ignore it, because there may be a extra "0x00" at the end of ".lzma" file. It won't affect the normal data decompression.

　

　

## Related Links

The specification of GZIP:

- https://www.rfc-editor.org/rfc/rfc1951
- https://www.rfc-editor.org/rfc/rfc1952

The specification of ZSTD:

- https://www.rfc-editor.org/rfc/rfc8878

The official code of ZSTD:

- https://github.com/facebook/zstd

The official code of LZMA & 7ZIP & XZ:

- https://www.7-zip.org/sdk.html
- https://tukaani.org/xz/

To quickly understand the algorithm of LZMA algorithm, see:

- wikipedia : https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm

An FPGA-based hardware GZIP data compressor:

- FPGA-GZIP-compressor : https://github.com/WangXuan95/FPGA-Gzip-compressor

An FPGA-based hardware LZMA data compressor:

- FPGA-LZMA-compressor : https://github.com/WangXuan95/FPGA-LZMA-compressor
