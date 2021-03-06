/**
 * @file
 */

#include "AppCommand.h"
#include <vector>
#include "command/Command.h"
#include "Log.h"
#include "Var.h"

namespace core {

namespace AppCommand {

void init() {
	core::Command::registerCommand("varclearhistory", [] (const core::CmdArgs& args) {
		if (args.size() != 1) {
			Log::error("not enough arguments given. Expecting a variable name");
			return;
		}
		const VarPtr& st = core::Var::get(args[0]);
		if (st) {
			st->clearHistory();
		}
	}).setHelp("Clear the value history of a variable");

	core::Command::registerCommand("toggle", [] (const core::CmdArgs& args) {
		if (args.empty()) {
			Log::error("not enough arguments given. Expecting a variable name at least");
			return;
		}
		const core::VarPtr& var = core::Var::get(args[0]);
		if (!var) {
			Log::error("given var doesn't exist: %s", args[0].c_str());
			return;
		}
		const uint32_t index = var->getHistoryIndex();
		const uint32_t size = var->getHistorySize();
		if (size <= 1) {
			if (var->typeIsBool()) {
				var->setVal(var->boolVal() ? "false" : "true");
			} else {
				Log::error("Could not toggle %s", args[0].c_str());
			}
			return;
		}
		bool changed;
		if (index == size - 1) {
			changed = var->useHistory(size - 2);
		} else {
			changed = var->useHistory(size - 1);
		}
		if (!changed) {
			Log::error("Could not toggle %s", args[0].c_str());
		}
	}).setHelp("Toggle between true/false for a variable");

	core::Command::registerCommand("show", [] (const core::CmdArgs& args) {
		if (args.size() != 1) {
			Log::error("not enough arguments given. Expecting a variable name");
			return;
		}
		const VarPtr& st = core::Var::get(args[0]);
		if (st) {
			Log::info(" -> %s ", st->strVal().c_str());
		} else {
			Log::info("not found");
		}
	}).setHelp("Show the value of a variable");

	core::Command::registerCommand("logerror", [] (const core::CmdArgs& args) {
		if (args.empty()) {
			return;
		}
		Log::error("%s", args[0].c_str());
	}).setHelp("Log given message as error");

	core::Command::registerCommand("loginfo", [] (const core::CmdArgs& args) {
		if (args.empty()) {
			return;
		}
		Log::info("%s", args[0].c_str());
	}).setHelp("Log given message as info");

	core::Command::registerCommand("logdebug", [] (const core::CmdArgs& args) {
		if (args.empty()) {
			return;
		}
		Log::debug("%s", args[0].c_str());
	}).setHelp("Log given message as debug");

	core::Command::registerCommand("logwarn", [] (const core::CmdArgs& args) {
		if (args.empty()) {
			return;
		}
		Log::warn("%s", args[0].c_str());
	}).setHelp("Log given message as warn");

	core::Command::registerCommand("log", [] (const core::CmdArgs& args) {
		if (args.size() < 2) {
			return;
		}
		const std::string& id = args[0];
		const Log::Level level = Log::toLogLevel(args[1]);
		if (level == Log::Level::None) {
			const auto fourcc = Log::logid(id.c_str(), id.length());
			Log::disable(fourcc);
			Log::trace("Disabling logging for %s", id.c_str());
		} else {
			const auto fourcc = Log::logid(id.c_str(), id.length());
			Log::enable(fourcc, level);
			Log::trace("Set log level for %s to %s", id.c_str(), args[1].c_str());
		}
	}).setHelp("Change the log level on an id base (FourCC)").setArgumentCompleter([] (const std::string& str, std::vector<std::string>& matches) -> int {
		if (str[0] == 't') {
			matches.push_back("trace");
			return 1;
		}
		if (str[0] == 'd') {
			matches.push_back("debug");
			return 1;
		}
		if (str[0] == 'i') {
			matches.push_back("info");
			return 1;
		}
		if (str[0] == 'w') {
			matches.push_back("warn");
			return 1;
		}
		if (str[0] == 'e') {
			matches.push_back("error");
			return 1;
		}
		matches.push_back("trace");
		matches.push_back("debug");
		matches.push_back("info");
		matches.push_back("warn");
		matches.push_back("error");
		return 5;
	});

	core::Command::registerCommand("cvarlist", [] (const core::CmdArgs& args) {
		core::Var::visitSorted([&] (const core::VarPtr& var) {
			if (!args.empty() && !core::string::matches(args[0], var->name())) {
				return;
			}
			const uint32_t flags = var->getFlags();
			std::string flagsStr = "     ";
			const char *value = var->strVal().c_str();
			if ((flags & CV_READONLY) != 0) {
				flagsStr[0]  = 'R';
			}
			if ((flags & CV_NOPERSIST) != 0) {
				flagsStr[1]  = 'N';
			}
			if ((flags & CV_SHADER) != 0) {
				flagsStr[2]  = 'S';
			}
			if ((flags & CV_SECRET) != 0) {
				flagsStr[3]  = 'X';
				value = "***secret***";
			}
			if (var->isDirty()) {
				flagsStr[4]  = 'D';
			}
			const std::string& name = core::string::format("%-28s", var->name().c_str());
			const std::string& valueStr = core::string::format(R"("%s")", value);
			Log::info("* %s %s = %s (%u)", flagsStr.c_str(), name.c_str(), valueStr.c_str(), var->getHistorySize());
		});
	}).setHelp("Show the list of known variables (wildcards supported)");

	core::Command::registerCommand("cmdlist", [] (const core::CmdArgs& args) {
		core::Command::visitSorted([&] (const core::Command& cmd) {
			if (!args.empty() && !core::string::matches(args[0], cmd.name())) {
				return;
			}
			Log::info("* %s - %s", cmd.name().c_str(), cmd.help().c_str());
		});
	}).setHelp("Show the list of known commands (wildcards supported)");
}

}

}
