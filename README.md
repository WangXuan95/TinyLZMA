 ![language](https://img.shields.io/badge/language-C-green.svg) ![build](https://img.shields.io/badge/build-Windows-blue.svg) ![build](https://img.shields.io/badge/build-linux-FF1010.svg)

TinyLZMA
===========================

A minimal LZMA data compressor & decompressor. Only hundreds of lines of C.

　

LZMA is a lossless data compression method with a higher compression ratio than Deflate and BZIP. Several container formats supports LZMA:

- ".7z" and ".xz" format, whose default compression method is LZMA.
- ".zip" format also supports LZMA, although its default compression method is Deflate.
- ".lzma" is a very simple format for containing LZMA, which is legacy and gradually replaced by ".xz" format.

　

**This code, TinyLZMA, supports 3 modes**:

- compress a file into a ".zip" file (compress method=LZMA)
- compress a file into a ".lzma" file
- decompress a ".lzma" file

　

　

## Linux Build

On Linux, run command:

```bash
gcc src/*.c -static-libgcc -static-libstdc++ -O3 -Wall -o tlzma
```

The output Linux binary file is [tlzma](./tlzma)

　

　

## Windows Build

If you installed MinGW in Windows, run command to compile:

```powershell
gcc src\*.c -static-libgcc -static-libstdc++ -O3 -Wall -o tlzma.exe
```

The output executable file is [tlzma.exe](./tlzma.exe)

　

　

## Usage

Run TinyLZMA to show usage:

```
└─$ ./tlzma
|-----------------------------------------------------------------|
|  Tiny LZMA compressor & decompressor v0.2                       |
|  Source from https://github.com/WangXuan95/TinyLZMA             |
|-----------------------------------------------------------------|
|  Usage :                                                        |
|     mode1 : decompress .lzma file :                             |
|       tlzma  <input_file(.lzma)>  <output_file>                 |
|                                                                 |
|     mode2 : compress a file to .lzma file :                     |
|       tlzma  <input_file>  <output_file(.lzma)>                 |
|                                                                 |
|     mode3 : compress a file to .zip file (use lzma algorithm) : |
|       tlzma  <input_file>  <output_file(.zip)>                  |
|-----------------------------------------------------------------|
```

　

### Example Usage

**Example of mode3**: You can compress the file `data3.txt` in directory `testdata` to `data3.txt.zip` using command:

```bash
./tlzma testdata/data3.txt data3.txt.zip
```

The outputting ".zip" file can be extracted by other compression software, such as [7ZIP](https://www.7-zip.org), WinZip, WinRAR, etc.

**Example of mode2**: You can use following command to compress a file to a ".lzma" file :

```bash
./tlzma testdata/data3.txt data3.txt.lzma
```

Besides TinyLZMA itself, you can use other LZMA official softwares to decompress ".lzma" file. See [How to decompress .lzma file](#dec_en)

**Example of mode1**: You can use following command to decompress a ".lzma" file :

```bash
./tlzma data3.txt.lzma data3.txt
```

　

　

## Notice

- TinyLZMA is verified on hundreds of files using automatic scripts.
- To be simpler, TinyLZMA loads the whole file data to memory to perform compresses/decompresses, so it is limited by memory capacity and cannot handle files that are too large.
- The search strategy of TinyLZMA's compressor is a simple hash-chain.
- The compression ratio of TinyLZMA's compressor is mostly like the `-1` to `-4` level of XZ-Utils's LZMA compressor [2]. 
- The performance of TinyLZMA's compressor is mostly like the `-2` level of XZ-Utils's LZMA compressor. 

> :point_right: XZ-Utils's LZMA compressor has a total of 10 levels, from `-0` to `-9` . The larger, the higher the compression ratio, but the lower the performance. For example, if you want to use XZ-Utils to compress "a.txt" to "a.txt.lzma" using level 4, the command should be `lzma -zk -4 a.txt`

　

　

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

The official code of LZMA & 7ZIP & XZ:

- [1]  https://www.7-zip.org/sdk.html
- [2]  https://tukaani.org/xz/

To quickly understand the algorithm of LZMA, see:

- [3]  wikipedia : https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm

An FPGA-based hardware data compressor:

- [4]  FPGA-LZMA-compressor : https://github.com/WangXuan95/FPGA-LZMA-compressor
