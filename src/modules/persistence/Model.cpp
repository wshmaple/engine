/**
 * @file
 */

#include "Model.h"
#include "ConnectionPool.h"
#include "ScopedConnection.h"
#include "core/Log.h"
#include "core/String.h"
#include <algorithm>

namespace persistence {

Model::Model(const std::string& tableName) :
		_tableName(tableName) {
}

bool Model::isPrimaryKey(const std::string& fieldname) const {
	auto i = std::find_if(_fields.begin(), _fields.end(),
			[&fieldname](const Field& f) {return f.name == fieldname;}
	);
	if (i == _fields.end()) {
		return false;
	}

	return (i->contraintMask & Model::PRIMARYKEY) != 0;
}

Model::PreparedStatement Model::prepare(const std::string& name, const std::string& statement) {
	return PreparedStatement(this, name, statement);
}

bool Model::checkLastResult(State& state, Connection* connection) const {
	state.affectedRows = 0;
	if (state.res == nullptr) {
		return false;
	}

#ifdef PERSISTENCE_POSTGRES
	ExecStatusType lastState = PQresultStatus(state.res);

	switch (lastState) {
	case PGRES_BAD_RESPONSE:
	case PGRES_NONFATAL_ERROR:
	case PGRES_FATAL_ERROR:
		state.lastErrorMsg = PQerrorMessage(connection->connection());
		Log::error("Failed to execute sql: %s ", state.lastErrorMsg.c_str());
		PQclear(state.res);
		state.res = nullptr;
		return false;
	case PGRES_EMPTY_QUERY:
	case PGRES_COMMAND_OK:
		state.affectedRows = 0;
		break;
	case PGRES_TUPLES_OK:
		state.affectedRows = PQntuples(state.res);
		Log::trace("Affected rows %i", state.affectedRows);
		break;
	default:
		Log::error("not catched state: %s", PQresStatus(lastState));
		return false;
	}
#endif

	state.result = true;
	return true;
}

bool Model::exec(const char* query) {
	Log::debug("%s", query);
	ScopedConnection scoped(ConnectionPool::get().connection());
	if (scoped == false) {
		Log::error("Could not execute query '%s' - could not acquire connection", query);
		return false;
	}
	ConnectionType* conn = scoped.connection()->connection();
#ifdef PERSISTENCE_POSTGRES
	State s(PQexec(conn, query));
	checkLastResult(s, scoped);
	return s.result;
#elif defined PERSISTENCE_SQLITE
	char *zErrMsg = nullptr;
	const int rc = sqlite3_exec(conn, query, nullptr, nullptr, &zErrMsg);
	if (rc != SQLITE_OK) {
		if (zErrMsg != nullptr) {
			Log::error("SQL error: %s", zErrMsg);
		}
		sqlite3_free(zErrMsg);
		return false;
	}

	return true;
#else
	return false;
#endif
}

Model::Field Model::getField(const std::string& name) const {
	for (const Field& field : _fields) {
		if (field.name == name) {
			return field;
		}
	}
	return Field();
}

Model::PreparedStatement::PreparedStatement(Model* model, const std::string& name, const std::string& statement) :
		_model(model), _name(name), _statement(statement) {
}

Model::State Model::PreparedStatement::exec() {
	ScopedConnection scoped(ConnectionPool::get().connection());
	if (scoped == false) {
		Log::error("Could not prepare query '%s' - could not acquire connection", _statement.c_str());
		return State(nullptr);
	}

	ConnectionType* conn = scoped.connection()->connection();

#ifdef PERSISTENCE_POSTGRES
	State state(PQprepare(conn, _name.c_str(), _statement.c_str(), (int)_params.size(), nullptr));
	if (!_model->checkLastResult(state, scoped)) {
		return state;
	}
	const int size = _params.size();
	const char* paramValues[size];
	// TODO: handle the type
	for (int i = 0; i < size; ++i) {
		paramValues[i] = _params[i].first.c_str();
	}
	State prepState(PQexecPrepared(conn, _name.c_str(), size, paramValues, nullptr, nullptr, 0));
	if (!_model->checkLastResult(prepState, scoped)) {
		return prepState;
	}
	if (state.affectedRows == 1) {
		const int nFields = PQnfields(state.res);
		for (int i = 0; i < nFields; ++i) {
			const char* name = PQfname(state.res, i);
			const char* value = PQgetvalue(state.res, 0, i);
			const Field& f = _model->getField(name);
			if (f.name != name) {
				Log::error("Unkown field name for '%s'", name);
				prepState.result = false;
				return prepState;
			}
			Log::debug("Try to set '%s' to '%s'", name, value);
			switch (f.type) {
			case Model::STRING:
			case Model::PASSWORD:
				_model->setValue(f, std::string(value));
				break;
			case Model::INT:
				_model->setValue(f, core::string::toInt(value));
				break;
			case Model::LONG:
				_model->setValue(f, core::string::toLong(value));
				break;
			case Model::TIMESTAMP:
				// TODO: implement me
				break;
			}
		}
	}
	return prepState;
#elif defined PERSISTENCE_SQLITE
	ResultType *stmt;
	const char *pzTest;
	const int rcPrep = sqlite3_prepare_v2(conn, _statement.c_str(), _statement.size(), &stmt, &pzTest);
	if (rcPrep != SQLITE_OK) {
		Log::error("failed to prepare the insert statement: %s", sqlite3_errmsg(conn));
		return State(nullptr);
	}
	sqlite3_reset(stmt);

	const int size = _params.size();
	for (int i = 0; i < size; ++i) {
		const int retVal = sqlite3_bind_text(stmt, i, _params[i].c_str(), _params[i].size(), SQLITE_TRANSIENT);
		if (retVal != SQLITE_OK) {
			Log::error("SQL error: %s", sqlite3_errmsg(conn));
			return State(nullptr);
		}
	}

	char *zErrMsg = nullptr;
	const int rcExec = sqlite3_exec(conn, _statement.c_str(), nullptr, nullptr, &zErrMsg);
	if (rcExec != SQLITE_OK) {
		if (zErrMsg != nullptr) {
			Log::error("SQL error: %s", zErrMsg);
		}
		sqlite3_free(zErrMsg);
		return State(nullptr);
	}

	return State(stmt);
#else
	return State(nullptr);
#endif
}

Model::State::State(ResultType* _res) :
		res(_res) {
}

Model::State::~State() {
	if (res != nullptr) {
#ifdef PERSISTENCE_POSTGRES
		//PQclear(res);
#elif defined PERSISTENCE_SQLITE
		const int retVal = sqlite3_finalize(res);
		if (retVal != SQLITE_OK) {
			//const char *errMsg = sqlite3_errmsg(_pgConnection);
			Log::error("Could not finialize the statement");
		}
#endif
		res = nullptr;
	}
}

}
