import os

def collect_cpp_and_h_files(output_file='combined_code.txt'):
    """
    Recursively read all .cpp and .h files in current directory
    and copy their content with filenames to one .txt file
    """
    
    # Get current directory
    current_dir = os.getcwd()
    
    # Open output file
    with open(output_file, 'w', encoding='utf-8') as outfile:
        # Walk through all directories and subdirectories
        for root, dirs, files in os.walk(current_dir):
            for filename in files:
                # Check if file has .cpp or .h extension
                if filename.endswith('.cpp') or filename.endswith('.h'):
                    file_path = os.path.join(root, filename)
                    
                    # Get relative path for cleaner output
                    relative_path = os.path.relpath(file_path, current_dir)
                    
                    # Write separator and filename
                    outfile.write('=' * 80 + '\n')
                    outfile.write(f'FILE: {relative_path}\n')
                    outfile.write('=' * 80 + '\n\n')
                    
                    # Read and write file content
                    try:
                        with open(file_path, 'r', encoding='utf-8') as infile:
                            content = infile.read()
                            outfile.write(content)
                            outfile.write('\n\n')
                    except Exception as e:
                        outfile.write(f'Error reading file: {e}\n\n')
    
    print(f'All .cpp and .h files have been combined into {output_file}')

if __name__ == '__main__':
    collect_cpp_and_h_files()
