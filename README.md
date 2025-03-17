 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

TinyZZZ
===========================

TinyZZZ is a simple, standalone data compressor/decompressor which supports several popular data compression algorithms, including [GZIP](https://www.rfc-editor.org/rfc/rfc1952), [ZSTD](https://github.com/facebook/zstd), [LZMA](https://www.7-zip.org/sdk.html) (and maybe more in future). These algorithms are written in C language, unlike the official code implementation, this code mainly focuses on simplicity and easy to understand.

#### TinyZZZ currently supports:

  - GZIP compress: compress a file into a ".gz" file
  - ZSTD decompress: decompress a ".zst" file to a file
  - LZMA decompress: decompress a ".lzma" file to a file
  - LZMA compress: compress a file into a ".lzma" file
  - LZMA compress: compress a file into a ".zip" container file (compress method=LZMA)

#### About GZIP

[GZIP](https://www.rfc-editor.org/rfc/rfc1952) is an old lossless data compression algorithm which has excellent compatibility. The core compression algorithm of GZIP is [Deflate](https://www.rfc-editor.org/rfc/rfc1951). The file name suffix of compressed GZIP file is ".gz"

#### About ZSTD

[ZSTD](https://github.com/facebook/zstd) (Zstandard) is a new lossless data compression algorithm with high compression ratio and high decompression speed. The file name suffix of compressed ZSTD file is ".zstd"

#### About LZMA

[LZMA](https://www.7-zip.org/sdk.html) is a lossless data compression algorithm with higher compression ratio than GZIP, BZIP, and ZSTD. Several archive container formats supports LZMA:

- ".lzma" is a very simple format to contain LZMA, which is legacy and gradually replaced by ".xz" format.
- ".7z" and ".xz" format, whose default compression method is LZMA.
- ".zip" format also supports LZMA, although the default compression method of ".zip" is [Deflate](https://www.rfc-editor.org/rfc/rfc1951).

　

　

## Linux Build

On Linux, run following command to compile. The output Linux binary file is [tinyZZZ](./tinyZZZ)

```bash
gcc src/*.c src/GZIP/*.c src/ZSTD/*.c src/LZMA/*.c -O2 -Wall -o tinyZZZ
```

　

　

## Windows Build

If you installed MinGW in Windows, run following command to compile. The output executable file is [tinyZZZ.exe](./tinyZZZ.exe)

```powershell
gcc src\*.c src\GZIP\*.c src\ZSTD\*.c src\LZMA\*.c -O2 -Wall -o tinyZZZ.exe
```

　

　

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

**Example1**: decompress the file `example.txt.zst` to `example.txt` use following command.

```bash
./tinyZZZ -d --zstd example.txt.zst example.txt
```

**Example2**: compress `example.txt` to `example.txt.gz` use following command. The outputting ".gz" file can be extracted by many other software, such as [7ZIP](https://www.7-zip.org), [WinRAR](https://www.rarlab.com/), etc.

```bash
./tinyZZZ -c --gzip example.txt example.txt.gz
```

**Example3**: compress `example.txt` to `example.txt.lzma` use following command.

```bash
./tinyZZZ -c --lzma example.txt example.txt.lzma
```

**Example4**: decompress `example.txt.lzma` to `example.txt` use following command.

```bash
./tinyZZZ -d --lzma example.txt.lzma example.txt
```

**Example5**: compress `example.txt` to `example.zip` use following command. The outputting ".zip" file can be extracted by many other software, such as [7ZIP](https://www.7-zip.org), [WinRAR](https://www.rarlab.com/), etc.

```bash
./tinyZZZ -c --lzma --zip example.txt example.zip
```

　

　

## <span id="dec_en">Appendix: How to decompress ".lzma" file</span>

#### on Windows

On Windows, you can use the [official 7ZIP/LZMA software](https://www.7-zip.org/sdk.html) to decompress the generated ".lzma" file. To get it, download the "LZMA SDK", extract it. In the "bin" directory, you can see "lzma.exe".

To decompress a ".lzma" file, run command as format:

```powershell
.\lzma.exe d [input_lzma_file] [output_file]
```

#### on Linux

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

- GZIP specification: https://www.rfc-editor.org/rfc/rfc1951
- Deflate algorithm specification: https://www.rfc-editor.org/rfc/rfc1952

- ZSTD specification: https://www.rfc-editor.org/rfc/rfc8878

- ZSTD official code: https://github.com/facebook/zstd

- ZSTD official lightweight decompressor: https://github.com/facebook/zstd/tree/dev/doc/educational_decoder

- LZMA official code and the 7ZIP software: https://www.7-zip.org/sdk.html
- another LZMA official code and the XZ software: https://tukaani.org/xz/

- An introduction to LZMA algorithm: https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm

- An FPGA-based hardware GZIP data compressor: https://github.com/WangXuan95/FPGA-Gzip-compressor

- An FPGA-based hardware LZMA data compressor: https://github.com/WangXuan95/FPGA-LZMA-compressor
