#include "response.h"
#include <fstream>
#include <sstream>
#include<string>
#include <cassert>
#include <cstring>

// char * load_file_to_mem(std::string fileName)
// {
// 	FILE *file = fopen(fileName, "rb");
// 	if (!file)
// 		return NULL;

// 	struct stat st;
// 	// fileno 可以把 FILE* 转换成底层系统调用需要的 fd
// 	if (fstat(fileno(file), &st) != 0){
// 		fclose(file);
// 		return NULL;
// 	}

// 	long fsize = st.st_size;
// 	char *buffer = new char[fsize + 1];
// 	if (buffer){
// 		fread(buffer, 1, fsize, file);
// 		buffer[fsize] = '\0';
// 	}

// 	fclose(file);
// 	return buffer;
// }
std::string load_file_to_mem(std::string fileName) {
    std::ifstream file(fileName, std::ios::binary);
    assert(file.is_open() && "The template file MUST exist in the directory!");
    // 利用流缓冲区直接读取整个文件
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
