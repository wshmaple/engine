/**
 * @file
 */

#include "Metric.h"
#include "core/Log.h"
#include "core/Var.h"
#include "core/Assert.h"
#include <stdio.h>
#include <string.h>
#include <SDL.h>

namespace metric {

Metric::Metric(const char* prefix) :
		_prefix(prefix) {
}

Metric::~Metric() {
	shutdown();
}

bool Metric::init(const IMetricSenderPtr& messageSender) {
	const std::string& flavor = core::Var::getSafe(cfg::MetricFlavor)->strVal();
	if (flavor == "telegraf") {
		_flavor = Flavor::Telegraf;
		Log::debug("Using metric flavor 'telegraf'");
	} else if (flavor == "etsy") {
		_flavor = Flavor::Etsy;
		Log::debug("Using metric flavor 'etsy'");
	} else if (flavor == "datadog") {
		_flavor = Flavor::Datadog;
		Log::debug("Using metric flavor 'datadog'");
	} else {
		Log::warn("Invalid %s given - using telegraf", cfg::MetricFlavor);
	}
	_messageSender = messageSender;
	return true;
}

void Metric::shutdown() {
}

bool Metric::createTags(char* buffer, size_t len, const TagMap& tags, const char* sep, const char* preamble, const char *split) const {
	if (tags.empty()) {
		return true;
	}
	const size_t preambleLen = SDL_strlen(preamble);
	if (preambleLen >= len) {
		return false;
	}
	strncpy(buffer, preamble, preambleLen);
	buffer += preambleLen;
	int remainingLen = len - preambleLen;
	bool first = true;
	const size_t splitLen = SDL_strlen(split);
	for (const auto& e : tags) {
		if (remainingLen <= 0) {
			return false;
		}
		size_t keyValueLen = e.first.size() + strlen(sep) + e.second.size();
		int written;
		if (first) {
			written = SDL_snprintf(buffer, remainingLen, "%s%s%s", e.first.c_str(), sep, e.second.c_str());
		} else {
			keyValueLen += splitLen;
			written = SDL_snprintf(buffer, remainingLen, "%s%s%s%s", split, e.first.c_str(), sep, e.second.c_str());
		}
		if (written >= remainingLen) {
			return false;
		}
		buffer += keyValueLen;
		remainingLen -= keyValueLen;
		first = false;
	}
	return true;
}

bool Metric::assemble(const char* key, int value, const char* type, const TagMap& tags) const {
	constexpr int metricSize = 256;
	char buffer[metricSize];
	constexpr int tagsSize = 256;
	char tagsBuffer[tagsSize] = "";
	int written;
	switch (_flavor) {
	case Flavor::Etsy:
		written = SDL_snprintf(buffer, sizeof(buffer), "%s%s:%i|%s", _prefix.c_str(), key, value, type);
		break;
	case Flavor::Datadog:
		if (!createTags(tagsBuffer, sizeof(tagsBuffer), tags, ":", "|#", ",")) {
			return false;
		}
		written = SDL_snprintf(buffer, sizeof(buffer), "%s%s:%i|%s%s", _prefix.c_str(), key, value, type, tagsBuffer);
		break;
	case Flavor::Telegraf:
	default:
		if (!createTags(tagsBuffer, sizeof(tagsBuffer), tags, "=", ",", ",")) {
			return false;
		}
		written = SDL_snprintf(buffer, sizeof(buffer), "%s%s%s:%i|%s", _prefix.c_str(), key, tagsBuffer, value, type);
		break;
	}
	if (written >= metricSize) {
		return false;
	}
	core_assert_always(_messageSender);
	return _messageSender->send(buffer);
}

}
