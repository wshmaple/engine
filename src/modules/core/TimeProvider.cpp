/**
 * @file
 */

#include "TimeProvider.h"
#include <chrono>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <ctime>

namespace core {

TimeProvider::TimeProvider() :
		_tickMillis(uint64_t(0)) {
}

uint64_t TimeProvider::systemMillis() const {
	const uint64_t millis = (uint64_t)(std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1));
	return millis;
}

double TimeProvider::systemNanos() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() / 1e9;
}

std::string TimeProvider::toString(unsigned long millis, const char *format) {
	std::time_t t(millis / 1000UL);
	std::tm tm = *std::gmtime(&t);
	std::stringstream ss;
	ss << std::put_time(&tm, format);
	return ss.str();
}

}
