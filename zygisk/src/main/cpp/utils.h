#pragma once

#include <jni.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct MemRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
    bool exec = false;
    bool write = false;
    std::string path;
};

std::string jstring_to_string(JNIEnv* env, jstring value);
std::vector<MemRange> parse_maps();
bool find_library_exec(const char* name, uintptr_t* start, uintptr_t* end);
bool is_address_writable(const void* ptr);
void sleep_ms(int ms);
int clamp_fps(int fps);
