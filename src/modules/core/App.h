/**
 * @file
 */

#pragma once

#include <unordered_set>
#include <chrono>
#include "Common.h"
#include "Assert.h"
#include "Var.h"
#include "core/Trace.h"
#include "EventBus.h"
#include "TimeProvider.h"
#include "core/ThreadPool.h"

#define ORGANISATION "engine"

namespace io {
class Filesystem;
typedef std::shared_ptr<Filesystem> FilesystemPtr;
}

namespace core {

enum class AppState : uint8_t {
	Construct,
	Init,
	InitFailure,
	Running,
	Cleanup,
	Destroy,
	Blocked,
	NumAppStates,
	InvalidAppState,
};

/**
 * @brief The app class controls the main loop of every application.
 */
class App {
protected:
	class ProfilerCPU {
	private:
		double _min = 0.0;
		double _max = 0.0;
		double _avg = 0.0;
		std::string _name;
		std::vector<double> _samples;
		const int16_t _maxSampleCount;
		int16_t _sampleCount = 0;
		double _stamp = 0.0;
	public:
		ProfilerCPU(const std::string& name, uint16_t maxSamples = 1024u);
		const std::vector<double>& samples() const;
		void enter();
		void leave();
		double minimum() const;
		double maximum() const;
		double avg() const;
		const std::string& name() const;
	};

	template<class Profiler>
	struct ScopedProfiler {
		Profiler& _p;
		inline ScopedProfiler(Profiler& p) : _p(p) {
			p.enter();
		}
		inline ~ScopedProfiler() {
			_p.leave();
		}
	};

	core::Trace _trace;
	int _argc = 0;
	char **_argv = nullptr;

	std::string _organisation;
	std::string _appname;

	AppState _curState = AppState::Construct;
	AppState _nextState = AppState::InvalidAppState;
	std::unordered_set<AppState> _blockers;
	bool _suspendRequested = false;
	bool _syslog = false;
	bool _coredump = false;
	uint64_t _now;
	long _deltaFrame = 0l;
	uint64_t _initTime = 0l;
	double _nextFrame = 0.0;
	double _framesPerSecondsCap = 0.0;
	int _exitCode = 0;
	io::FilesystemPtr _filesystem;
	core::EventBusPtr _eventBus;
	static App* _staticInstance;
	core::ThreadPool _threadPool;
	core::TimeProviderPtr _timeProvider;
	core::VarPtr _logLevelVar;
	core::VarPtr _syslogVar;

	/**
	 * @brief There is no fps limit per default, but you set one on a per-app basis
	 * @param[in] framesPerSecondsCap The frames to cap the application loop at
	 */
	void setFramesPerSecondsCap(double framesPerSecondsCap) {
		_framesPerSecondsCap = framesPerSecondsCap;
	}

	void usage() const;

public:
	App(const io::FilesystemPtr& filesystem, const core::EventBusPtr& eventBus, const core::TimeProviderPtr& timeProvider, uint16_t traceport, size_t threadPoolSize = 1);
	virtual ~App();

	void init(const std::string& organisation, const std::string& appname);
	int startMainLoop(int argc, char *argv[]);

	/**
	 * e.g. register your commands here
	 * @return @c AppState::Init as next phase
	 */
	virtual AppState onConstruct();
	/**
	 * @brief Evaluates the command line parameters that the application was started with.
	 * @note Make sure your commands are already registered (@see onConstruct())
	 * @return @c AppState::Running if initialization succeeds, @c AppState::InitFailure if it failed.
	 */
	virtual AppState onInit();
	virtual void onBeforeRunning();
	/**
	 * @brief called every frame after the initialization was done
	 */
	virtual AppState onRunning();
	virtual void onAfterRunning();
	virtual AppState onCleanup();
	virtual AppState onDestroy();

	/**
	 * @brief Don't enter the given @c AppState before the blocker was removed. This can be used to implement
	 * e.g. long initialization phases.
	 * @see @c remBlocker()
	 */
	void addBlocker(AppState blockedState);
	/**
	 * @brief Indicate that the given @c AppState can now be entered.
	 * @see @c addBlocker()
	 */
	void remBlocker(AppState blockedState);

	const std::string& appname() const;

	class Argument {
	private:
		std::string _longArg;
		std::string _shortArg;
		std::string _description;
		std::string _defaultValue;
		bool _mandatory = false;

	public:
		Argument(const std::string& longArg) :
				_longArg(longArg) {
		}

		Argument& setShort(const std::string& shortArg) {
			_shortArg = shortArg;
			return *this;
		}

		Argument& setMandatory() {
			_mandatory = true;
			return *this;
		}

		Argument& setDescription(const std::string& description) {
			_description = description;
			return *this;
		}

		Argument& setDefaultValue(const std::string& defaultValue) {
			_defaultValue = defaultValue;
			return *this;
		}

		inline const std::string& defaultValue() const {
			return _defaultValue;
		}

		inline const std::string& description() const {
			return _description;
		}

		inline const std::string& longArg() const {
			return _longArg;
		}

		inline bool mandatory() const {
			return _mandatory;
		}

		inline const std::string& shortArg() const {
			return _shortArg;
		}
	};

	/**
	 * @note Only valid after
	 */
	bool hasArg(const std::string& arg) const;
	std::string getArgVal(const std::string& arg, const std::string& defaultVal = "");
	Argument& registerArg(const std::string& arg);

	// handle the app state changes here
	virtual void onFrame();
	void readyForInit();
	void requestQuit();
	void requestSuspend();

	long deltaFrame() const;
	uint64_t lifetimeInSeconds() const;

	/**
	 * @return the millis since the epoch
	 */
	uint64_t systemMillis() const;

	/**
	 * @brief Access to the FileSystem
	 */
	io::FilesystemPtr filesystem() const;

	core::ThreadPool& threadPool();

	/**
	 * @brief Access to the global TimeProvider
	 */
	core::TimeProviderPtr timeProvider() const;

	/**
	 * @brief Access to the global EventBus
	 */
	core::EventBusPtr eventBus() const;

	const std::string& currentWorkingDir() const;

	static App* getInstance() {
		core_assert(_staticInstance != nullptr);
		return _staticInstance;
	}

private:
	std::list<Argument> _arguments;
};

inline App::ProfilerCPU::ProfilerCPU(const std::string& name, uint16_t maxSamples) :
		_name(name), _maxSampleCount(maxSamples) {
	core_assert(maxSamples > 0);
	_samples.reserve(_maxSampleCount);
}

inline const std::vector<double>& App::ProfilerCPU::samples() const {
	return _samples;
}

inline void App::ProfilerCPU::enter() {
	_stamp = core::TimeProvider::systemNanos();
}

inline void App::ProfilerCPU::leave() {
	const double time = core::TimeProvider::systemNanos() - _stamp;
	_max = (std::max)(_max, time);
	_min = (std::min)(_min, time);
	_avg = _avg * 0.5 + time * 0.5;
	_samples[_sampleCount & (_maxSampleCount - 1)] = time;
	++_sampleCount;
}

inline const std::string& App::ProfilerCPU::name() const {
	return _name;
}

inline double App::ProfilerCPU::avg() const {
	return _avg;
}

inline double App::ProfilerCPU::minimum() const {
	return _min;
}

inline double App::ProfilerCPU::maximum() const {
	return _max;
}

inline uint64_t App::lifetimeInSeconds() const {
	return (_now - _initTime) / uint64_t(1000);
}

inline long App::deltaFrame() const {
	return _deltaFrame;
}

inline uint64_t App::systemMillis() const {
	return _timeProvider->systemMillis();
}

inline io::FilesystemPtr App::filesystem() const {
	return _filesystem;
}

inline core::TimeProviderPtr App::timeProvider() const {
	return _timeProvider;
}

inline core::ThreadPool& App::threadPool() {
	return _threadPool;
}

inline core::EventBusPtr App::eventBus() const {
	return _eventBus;
}

inline const std::string& App::appname() const {
	return _appname;
}

}
