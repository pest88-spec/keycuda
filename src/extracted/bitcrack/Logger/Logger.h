/**
 * Extracted from BitCrack by brichard19
 * @origin       https://github.com/brichard19/BitCrack
 * @origin_path  third_party/BitCrack/Logger/Logger.h
 * @origin_commit de3c15bcbe5d36e31d7ac969784773af1cd81a84
 * @origin_license MIT
 * @extracted_date 2025-10-06
 * @extracted_by Puzzle71Solver Team
 * @modifications Relocated to src/extracted/bitcrack/ for direct integration
 * @spdx_license_identifier MIT
 */

#ifndef _LOGGER_H
#define _LOGGER_H

#include <string>


namespace LogLevel {
	enum Level {
		Info = 1,
		Error = 2,
		Debug = 4,
        Warning = 8
	};

	bool isValid(int level);

	std::string toString(int level);
};


class Logger {

private:
	static std::string _logFile;

	static std::string formatLog(int logLevel, std::string msg);

	static std::string getDateTimeString();

public:

	Logger()
	{
	}

	static void log(int logLevel, std::string msg);

	static void setLogFile(std::string path);
};

#endif