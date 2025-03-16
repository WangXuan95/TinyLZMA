import sys
import os
import shutil
import zstandard


COMP_SUFFIX = '.zst'


def run_compress (input_file, output_file, level) :
    with open(input_file, "rb") as inf:
        with open(output_file, "wb") as outf:
            outf.write(zstandard.ZstdCompressor(level=level).compress(inf.read()))


def run_decompress(input_file, output_file) :  
    command_format = r'..\tinyZZZ.exe -d --zstd %s %s'
    command = command_format % (input_file, output_file)
    return os.system(command)


def file_content_same (file1, file2) :  
    with open(file1, "rb") as fp1:
        with open(file2, "rb") as fp2:
            data1 = fp1.read()
            data2 = fp2.read()
            return (data1==data2), min(len(data1), len(data2))


if __name__ == '__main__' :
    TMP_DIR   = 'verify_tmp'
    INPUT_DIR = sys.argv[1]
    try :
        LEVEL = int(sys.argv[2])
    except :
        LEVEL = 9
    
    if not os.path.isdir(INPUT_DIR) :
        print(f'***Error: {INPUT_DIR} do not exist')
        exit(1)
    
    if os.path.isdir(TMP_DIR) :
        if (input(f'{TMP_DIR} exist! remove it? (y|n)> ') in ['y', 'Y']) :
            shutil.rmtree(TMP_DIR)
        else :
            exit(0)
    
    os.mkdir(TMP_DIR)
        
    if os.path.realpath(INPUT_DIR) == os.path.realpath(TMP_DIR) :
        print(f'***Error: {INPUT_DIR} and {TMP_DIR} are same!')
        exit(1)
    
    for in_fname in os.listdir(INPUT_DIR) :
        in_fname_full  = os.path.join(INPUT_DIR, in_fname)
        cmp_fname_full = os.path.join(TMP_DIR, (in_fname+COMP_SUFFIX))
        out_fname_full = os.path.join(TMP_DIR, in_fname)
        
        if os.path.isfile(in_fname_full) :
        
            run_compress(in_fname_full, cmp_fname_full, level=LEVEL)
            ret_code = run_decompress(cmp_fname_full, out_fname_full)
            
            same, length = file_content_same(in_fname_full, out_fname_full)
            
            if ret_code == 0 :
                print(f'{in_fname_full} -> {cmp_fname_full} -> {out_fname_full}     length={length}')
            else :
                print(f'***Error: {in_fname_full} -> {cmp_fname_full} -> {out_fname_full} failed!')
                exit(1)
            
            if not same :
                print(f'***Error: content mismatch!   length={length}')
                exit(1)
    
    print('\n=== all test passed ===')
