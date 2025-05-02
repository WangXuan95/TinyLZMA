 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

TinyZZZ
===========================

TinyZZZ is a simple, standalone data compressor/decompressor which supports several popular data compression algorithms, including [GZIP](https://www.rfc-editor.org/rfc/rfc1952), [LZ4](https://github.com/lz4/lz4), [ZSTD](https://github.com/facebook/zstd), [LZMA](https://www.7-zip.org/sdk.html) . These algorithms are written in C language, unlike the official code implementation, this code mainly focuses on simplicity and easy to understand.

TinyZZZ currently supports:

|                       format                       | file suffix | compress                               | decompress                      |
| :------------------------------------------------: | :---------: | :------------------------------------: | :-----------------------------: |
| **[GZIP](https://www.rfc-editor.org/rfc/rfc1952)** |    .gz      | [510 lines of C](./src/gzipC.c)        | :x: not yet supported           |
| **[LZ4](https://github.com/lz4/lz4)**              |    .lz4     | [170 lines of C](./src/lz4C.c)         | [190 lines of C](./src/lz4D.c)  |
| **[ZSTD](https://github.com/facebook/zstd)**       |    .zst     | :x: not yet supported                  | [760 lines of C](./src/zstdD.c) |
| **[LZMA](https://www.7-zip.org/sdk.html)**         |    .lzma    | [780 lines of C](./src/lzmaC.c)        | [480 lines of C](./src/lzmaD.c) |


#### About GZIP

[GZIP](https://www.rfc-editor.org/rfc/rfc1952) is an old, famous lossless data compression algorithm which has excellent compatibility. The core compression algorithm of GZIP is [Deflate](https://www.rfc-editor.org/rfc/rfc1951). The file name suffix of compressed GZIP file is ".gz"

#### About LZ4

[LZ4](https://github.com/lz4/lz4) is a new, lightweight lossless data compression algorithm with very high decompression speed. The file name suffix of compressed LZ4 file is ".lz4"

#### About ZSTD

[ZSTD](https://github.com/facebook/zstd) (Zstandard) is a new lossless data compression algorithm with high compression ratio and high decompression speed. The file name suffix of compressed ZSTD file is ".zstd"

#### About LZMA

[LZMA](https://www.7-zip.org/sdk.html) is a lossless data compression algorithm with higher compression ratio than LZ4, GZIP, BZIP, and ZSTD. Several archive container formats supports LZMA:

- ".lzma" is a very simple format to contain LZMA, which is legacy and gradually replaced by ".xz" format.
- ".7z" and ".xz" format, whose default compression method is LZMA.

#### About ZIP

[ZIP](https://docs.fileformat.com/compression/zip/) is not a data compression algorithm, but a container format that supports file packaging and compressing by many compression algorithms.

This code supports compress a file to ZIP container by deflate algorithm or LZMA algorithm.

　

　

## Linux Build

On Linux, run following command to compile. The output Linux binary file is [tinyZZZ](./tinyZZZ)

```bash
gcc src/*.c -O2 -Wall -o tinyZZZ
```

　

　

## Windows Build (MinGW)

If you installed MinGW in Windows, run following command to compile. The output executable file is [tinyZZZ.exe](./tinyZZZ.exe)

```powershell
gcc src\*.c -O2 -Wall -o tinyZZZ.exe
```

　

　

## Windows Build (MSVC)

If you added MSVC compiler (cl.exe) to environment, run following command to compile. The output executable file is [tinyZZZ.exe](./tinyZZZ.exe)

```powershell
cl src\*.c /Ox /FetinyZZZ.exe
```

　

　

## Usage

Run TinyZZZ to show usage:

```
└─$ ./tinyZZZ
|-------------------------------------------------------------------------------------------|
|  Usage :                                                                                  |
|   - decompress a GZIP file       :  *** not yet supported! ***                            |
|   - compress a file to GZIP file :  tinyZZZ -c --gzip <input_file> <output_file(.gz)>     |
|   - decompress a LZ4 file        :  tinyZZZ -d --lz4  <input_file(.lz4)> <output_file>    |
|   - compress a file to LZ4 file  :  tinyZZZ -c --lz4  <input_file> <output_file(.lz4)>    |
|   - decompress a ZSTD file       :  tinyZZZ -d --zstd <input_file(.zst)> <output_file>    |
|   - compress a file to ZSTD file :  *** not yet supported! ***                            |
|   - decompress a LZMA file       :  tinyZZZ -d --lzma <input_file(.lzma)> <output_file>   |
|   - compress a file to LZMA file :  tinyZZZ -c --lzma <input_file> <output_file(.lzma)>   |
|-------------------------------------------------------------------------------------------|
|  Usage (ZIP) :                                                                            |
|   - compress a file to ZIP container file using deflate (GZIP) method                     |
|       tinyZZZ -c --gzip --zip <input_file> <output_file(.zip)>                            |
|   - compress a file to ZIP container file using LZMA method                               |
|       tinyZZZ -c --lzma --zip <input_file> <output_file(.zip)>                            |
|-------------------------------------------------------------------------------------------|
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

**Example5**: compress `example.txt` to `example.txt.lz4` use following command.

```bash
./tinyZZZ -c --lz4 example.txt example.txt.lz4
```

**Example6**: decompress `example.txt.lz4` to `example.txt` use following command.

```bash
./tinyZZZ -d --lz4 example.txt.lz4 example.txt
```

**Example7**: compress `example.txt` to `example.zip` use following command (method=deflate). The outputting ".zip" file can be extracted by many other software, such as [7ZIP](https://www.7-zip.org), [WinRAR](https://www.rarlab.com/), etc.

```bash
./tinyZZZ -c --gzip --zip example.txt example.zip
```

**Example8**: compress `example.txt` to `example.zip` use following command (method=LZMA). The outputting ".zip" file can be extracted by many other software, such as [7ZIP](https://www.7-zip.org), [WinRAR](https://www.rarlab.com/), etc.

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

- LZ4 official code: https://github.com/lz4/lz4

- LZ4 specification: https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md , https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md

- ZSTD specification: https://www.rfc-editor.org/rfc/rfc8878

- ZSTD official code: https://github.com/facebook/zstd

- ZSTD official lightweight decompressor: https://github.com/facebook/zstd/tree/dev/doc/educational_decoder

- LZMA official code and the 7ZIP software: https://www.7-zip.org/sdk.html
- another LZMA official code and the XZ software: https://tukaani.org/xz/

- An introduction to LZMA algorithm: https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm

- An FPGA-based hardware GZIP data compressor: https://github.com/WangXuan95/FPGA-Gzip-compressor

- An FPGA-based hardware LZMA data compressor: https://github.com/WangXuan95/FPGA-LZMA-compressor
