/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/util/util.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <string>
#include <vector>
#include <stdint.h>

namespace util {

class Timer {

private:
    uint64_t _startTime;

public:
    Timer();
    void start();
    uint64_t getTime();
};

uint64_t getSystemTime();
void sleep(int seconds);

std::string formatThousands(uint64_t x);
std::string formatSeconds(unsigned int seconds);

uint32_t parseUInt32(std::string s);
uint64_t parseUInt64(std::string s);
bool isHex(const std::string &s);
bool appendToFile(const std::string &fileName, const std::string &s);
bool readLinesFromStream(std::istream &in, std::vector<std::string> &lines);
bool readLinesFromStream(const std::string &fileName, std::vector<std::string> &lines);

std::string format(const char *formatStr, double value);
std::string format(uint32_t value);
std::string format(uint64_t value);
std::string format(int value);
void removeNewline(std::string &s);
unsigned int endian(unsigned int x);

std::string toLower(const std::string &s);
std::string trim(const std::string &s, char c=' ');

}

#endif