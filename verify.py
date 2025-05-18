import sys
import os
import shutil

import gzip         # pip install zipp==3.8.0
import lzma         # pip install zipp==3.8.0
import zipfile      # pip install zipp==3.8.0
import lz4.frame    # pip install lz4==3.1.3
import zstandard    # pip install zstandard==0.23.0


LZMA_OFFICIAL_PATH  = 'lzma_official.exe'
LPAQ8_OFFICIAL_PATH = 'lpaq8_official.exe'
TINYZZZ_PATH        = 'tinyZZZ.exe'
TEMP_FILE_PATH      = os.path.join('verify_tmp', 'testfile.hex')


RED_MARK   = '\033[31m'
GREEN_MARK = '\033[32m'
YELLOW_MARK= '\033[36m'
RESET_MARK = '\033[0m'


def runCommand (command) :
    print(f'{GREEN_MARK}{command} {RESET_MARK}')
    ret = os.system(command)
    if ret != 0 :
        print(f'{RED_MARK}***Error: command exit with error code = {ret} ! {RESET_MARK}')
        exit(1)


def runTinyZZZ (args) :
    runCommand(f'{TINYZZZ_PATH} {args}')


def official_compress_LPAQ8 (input_path, output_path, compress_level) :
    runCommand(f'{LPAQ8_OFFICIAL_PATH} {compress_level} {input_path} {output_path}')


def official_decompress_LPAQ8 (input_path, output_path) :
    runCommand(f'{LPAQ8_OFFICIAL_PATH} d {input_path} {output_path}')


def official_decompress_LZMA (input_path, output_path) :
    runCommand(f'{LZMA_OFFICIAL_PATH} d {input_path} {output_path}')


def official_compress (input_path, output_path, compress_level) :
    print(f'{GREEN_MARK}official_compress {input_path} -> {output_path}{RESET_MARK}')
    _, suffix = os.path.splitext(output_path)
    with     open(input_path , 'rb') as fpin :
        with open(output_path, 'wb') as fpout :
            data_in = fpin.read()
            if   suffix == '.gz'   :  data_out = gzip.compress(data_in)
            elif suffix == '.lzma' :  data_out = lzma.compress(data_in, format=lzma.FORMAT_ALONE, preset=compress_level, filters=None)
            elif suffix == '.lz4'  :  data_out = lz4.frame.compress(data_in, compression_level=compress_level)
            elif suffix == '.zst'  :  data_out = zstandard.compress(data_in, level=compress_level)
            else : 
                print(f'{RED_MARK}***Error {RESET_MARK}')
                exit(1)
            fpout.write(data_out)


def official_decompress (input_path, output_path) :
    print(f'{GREEN_MARK}official_decompress {input_path} -> {output_path}{RESET_MARK}')
    _, suffix = os.path.splitext(input_path)
    with     open(input_path , 'rb') as fpin :
        with open(output_path, 'wb') as fpout :
            data_in = fpin.read()
            if   suffix == '.gz'   :  data_out = gzip.decompress(data_in)
            elif suffix == '.lzma' :  data_out = lzma.decompress(data_in)
            elif suffix == '.lz4'  :  data_out = lz4.frame.decompress(data_in)
            elif suffix == '.zst'  :  data_out = zstandard.decompress(data_in)
            else : 
                print(f'{RED_MARK}***Error {RESET_MARK}')
                exit(1)
            fpout.write(data_out)


def offical_check_zip (input_path) :
    print(f'{GREEN_MARK}offical_check_zip {input_path}{RESET_MARK}')
    with zipfile.ZipFile(input_path, mode='r') as zipf:
        if not zipf.testzip() is None :
            print(f'{RED_MARK}***Error: offical_check_zip {input_path} failed ! {RESET_MARK}')
            exit(1)


def assert_file_content_same (file_path1, file_path2) :  
    with     open(file_path1, "rb") as fp1:
        with open(file_path2, "rb") as fp2:
            data1 = fp1.read()
            data2 = fp2.read()
            if data1 != data2 :
                print(f'{RED_MARK}***Error: content mismatch between {file_path1} and {file_path2} ! {RESET_MARK}')
                exit(1)


if __name__ == '__main__' :
    try :
        INPUT_DIR = sys.argv[1]
    except :
        print('Usage: python verify.py <path_to_test>')
        exit(1)

    os.system('')
    
    if not os.path.isdir(INPUT_DIR) :
        print(f'{RED_MARK}***Error: {INPUT_DIR} do not exist {RESET_MARK}')
        exit(1)
    
    temp_dir_path, _ = os.path.split(TEMP_FILE_PATH)
    if os.path.isdir(temp_dir_path) :
        shutil.rmtree(temp_dir_path)
    os.mkdir(temp_dir_path)
    
    for orig_file_name in os.listdir(INPUT_DIR) :
        orig_file_path = os.path.join(INPUT_DIR, orig_file_name)

        if os.path.isfile(orig_file_path) :
            shutil.copy(orig_file_path, TEMP_FILE_PATH)

            # GZIP : tinyZZZ -> offical ------------------------------------------------------------------
            runTinyZZZ(f'-c --gzip  {TEMP_FILE_PATH}       {TEMP_FILE_PATH}.gz')
            official_decompress(  f'{TEMP_FILE_PATH}.gz',   TEMP_FILE_PATH)
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)
            
            # ZSTD : offical -> tinyZZZ ------------------------------------------------------------------
            official_compress(       TEMP_FILE_PATH,      f'{TEMP_FILE_PATH}.zst', compress_level=9)
            runTinyZZZ(f'-d --zstd  {TEMP_FILE_PATH}.zst   {TEMP_FILE_PATH}')
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)

            # LZMA : offical -> tinyZZZ ------------------------------------------------------------------
            official_compress(       TEMP_FILE_PATH,     f'{TEMP_FILE_PATH}.lzma', compress_level=4)
            runTinyZZZ(f'-d --lzma  {TEMP_FILE_PATH}.lzma  {TEMP_FILE_PATH}')
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)
            
            # LZMA : tinyZZZ -> tinyZZZ ------------------------------------------------------------------
            runTinyZZZ(f'-c --lzma  {TEMP_FILE_PATH}       {TEMP_FILE_PATH}.lzma')
            runTinyZZZ(f'-d --lzma  {TEMP_FILE_PATH}.lzma  {TEMP_FILE_PATH}')
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)

            # LZMA : tinyZZZ -> offical ------------------------------------------------------------------
            official_decompress_LZMA(f'{TEMP_FILE_PATH}.lzma', TEMP_FILE_PATH)
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)

            # LZ4  : offical -> tinyZZZ ------------------------------------------------------------------
            official_compress(       TEMP_FILE_PATH,      f'{TEMP_FILE_PATH}.lz4', compress_level=5)
            runTinyZZZ(f'-d --lz4   {TEMP_FILE_PATH}.lz4   {TEMP_FILE_PATH}')
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)
            
            # LZ4  : tinyZZZ -> tinyZZZ ------------------------------------------------------------------
            runTinyZZZ(f'-c --lz4   {TEMP_FILE_PATH}       {TEMP_FILE_PATH}.lz4')
            runTinyZZZ(f'-d --lz4   {TEMP_FILE_PATH}.lz4   {TEMP_FILE_PATH}')
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)

            # LZ4  : tinyZZZ -> offical ------------------------------------------------------------------
            official_decompress(  f'{TEMP_FILE_PATH}.lz4',  TEMP_FILE_PATH)
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)
            
            # LPAQ8: offical -> tinyZZZ ------------------------------------------------------------------
            official_compress_LPAQ8( TEMP_FILE_PATH,     f'{TEMP_FILE_PATH}.lpaq8', compress_level=3)
            runTinyZZZ(f'-d --lpaq8 {TEMP_FILE_PATH}.lpaq8 {TEMP_FILE_PATH}')
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)
            
            # LPAQ8: tinyZZZ -> tinyZZZ ------------------------------------------------------------------
            runTinyZZZ(f'-c --lpaq8 {TEMP_FILE_PATH}       {TEMP_FILE_PATH}.lpaq8')
            runTinyZZZ(f'-d --lpaq8 {TEMP_FILE_PATH}.lpaq8 {TEMP_FILE_PATH}')
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)

            # LPAQ8: tinyZZZ -> official -----------------------------------------------------------------
            official_decompress_LPAQ8(f'{TEMP_FILE_PATH}.lpaq8', TEMP_FILE_PATH)
            assert_file_content_same(orig_file_path,        TEMP_FILE_PATH)
            
            # ZIP (deflate) : tinyZZZ -> official --------------------------------------------------------
            runTinyZZZ(f'-c --gzip --zip {TEMP_FILE_PATH}  {TEMP_FILE_PATH}.zip')
            offical_check_zip(f'{TEMP_FILE_PATH}.zip')
            
            # ZIP (LZMA) : tinyZZZ -> official -----------------------------------------------------------
            runTinyZZZ(f'-c --lzma --zip {TEMP_FILE_PATH}  {TEMP_FILE_PATH}.zip')
            offical_check_zip(f'{TEMP_FILE_PATH}.zip')
    
            print(f'\n{YELLOW_MARK} === {orig_file_path} test passed ===\n {RESET_MARK}')
    
    print(f'\n{YELLOW_MARK} === all test passed ===\n {RESET_MARK}')
