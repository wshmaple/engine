/**
 * @file
 */

#include "core/tests/AbstractTest.h"
#include "UserModel.h"
#include "persistence/ConnectionPool.h"
#include "persistence/DBHandler.h"
#include "core/GameConfig.h"
#include "config.h"

namespace backend {

class DatabaseModelTest: public core::AbstractTest {
private:
	using Super = core::AbstractTest;
protected:
	persistence::DBHandler _dbHandler;
public:
	void SetUp() override {
		Super::SetUp();
		core::Var::get(cfg::DatabaseMinConnections, "1");
		core::Var::get(cfg::DatabaseMaxConnections, "2");
		// TODO: use a different database for the tests
		core::Var::get(cfg::DatabaseName, "engine");
		core::Var::get(cfg::DatabaseHost, "localhost");
		core::Var::get(cfg::DatabaseUser, "engine");
		core::Var::get(cfg::DatabasePassword, "engine");

		ASSERT_TRUE(_dbHandler.init());
		ASSERT_TRUE(_dbHandler.createTable(db::UserModel())) << "Could not create table";
		_dbHandler.truncate(db::UserModel());
	}

	void TearDown() override {
		Super::TearDown();
		_dbHandler.shutdown();
	}

	void createUser(const std::string& email, const std::string& password, int64_t& id) {
		const persistence::Timestamp ts = persistence::Timestamp::now();
		db::UserModel u;
		EXPECT_EQ(0, u.id());
		ASSERT_TRUE(u.insert(email, password, ts));
		EXPECT_NE(0, u.id());

		db::UserModel u2nd;
		ASSERT_TRUE(u2nd.select(email.c_str(), password.c_str(), nullptr));
		EXPECT_GT(u2nd.registrationdate().time(), uint64_t(0));
		EXPECT_EQ(u2nd.email(), email);
		ASSERT_EQ(u2nd.id(), u.id());

		db::UserModel u3nd;
		ASSERT_TRUE(u3nd.selectById(u.id()));
		EXPECT_GT(u3nd.registrationdate().time(), uint64_t(0));
		EXPECT_EQ(u3nd.email(), email);
		ASSERT_EQ(u3nd.id(), u.id());

		id = u.id();
	}
};

TEST_F(DatabaseModelTest, testCreateUser) {
	int64_t id = -1L;
	createUser("a@b.c.d", "secret", id);
}

TEST_F(DatabaseModelTest, testCreateUsers) {
	int64_t id = -1L;
	for (int i = 0; i < 5; ++i) {
		createUser(core::string::format("a%i@b.c.d", i), "secret", id);
	}
}

TEST_F(DatabaseModelTest, testSelectAll) {
	int64_t id = -1L;
	const int expected = 5;
	for (int i = 0; i < expected; ++i) {
		createUser(core::string::format("a%i@b.c.d", i), "secret", id);
	}
	int count = 0;
	EXPECT_TRUE(_dbHandler.select(db::UserModel(), [&] (db::UserModel&& model) {
		++count;
	}));
	EXPECT_EQ(count, expected);
}

TEST_F(DatabaseModelTest, testSelectByEmail) {
	int64_t id = -1L;
	const int expected = 5;
	for (int i = 0; i < expected; ++i) {
		createUser(core::string::format("a%i@b.c.d", i), "secret", id);
	}
	int count = 0;
	const db::DBConditionUserEmail condition("a1@b.c.d");
	EXPECT_TRUE(_dbHandler.select(db::UserModel(), [&] (db::UserModel&& model) {
		++count;
		ASSERT_EQ(std::string(condition.value()), model.email());
	}, condition));
	EXPECT_EQ(count, 1);
}

TEST_F(DatabaseModelTest, testSelectById) {
	const int expected = 5;
	int64_t id = -1L;
	for (int i = 0; i < expected; ++i) {
		createUser(core::string::format("a%i@b.c.d", i), "secret", id);
	}
	int count = 0;
	const db::DBConditionUserId condition(id);
	EXPECT_TRUE(_dbHandler.select(db::UserModel(), [&] (db::UserModel&& model) {
		++count;
		ASSERT_EQ(id, model.id());
	}, condition));
	EXPECT_EQ(count, 1);
}

TEST_F(DatabaseModelTest, testTruncate) {
	int64_t id = -1L;
	createUser("a@b.c.d", "secret", id);
	EXPECT_TRUE(_dbHandler.truncate(db::UserModel()));
	int count = 0;
	_dbHandler.select(db::UserModel(), [&] (db::UserModel&& model) {
		++count;
	});
	EXPECT_EQ(count, 0);
}

TEST_F(DatabaseModelTest, testDelete) {
	int64_t id = -1L;
	createUser("a@b.c.d", "secret", id);
	EXPECT_TRUE(_dbHandler.deleteModel(db::UserModel(), db::DBConditionUserId(id)));
	int count = 0;
	_dbHandler.select(db::UserModel(), [&] (db::UserModel&& model) {
		++count;
	});
	EXPECT_EQ(count, 0);
}

}
