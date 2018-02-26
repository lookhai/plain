#include "gtest/gtest.h"
#include "pf/engine/kernel.h"
#include "pf/db/query/grammars/grammar.h"
#include "pf/db/query/grammars/mysql_grammar.h"
#include "pf/db/connection.h"
#include "pf/db/query/builder.h"
#include "pf/support/helpers.h"

enum {
  kDBTypeODBC = 1,
};

using namespace pf_db::query;
using namespace pf_basic::type;

class DBQueryBuilder : public testing::Test {

 public:
   static void SetUpTestCase() {
     
     GLOBALS["log.print"] = false; //First forbid the log print.

     GLOBALS["default.db.open"] = true;
     GLOBALS["default.db.type"] = kDBTypeODBC;
     GLOBALS["default.db.name"] = "pf_test";
     GLOBALS["default.db.user"] = "root";
     GLOBALS["default.db.password"] = "mysql";

     engine_.add_libraryload("pf_plugin_odbc", {kDBTypeODBC});

     engine_.init();

     auto connection = new pf_db::Connection(engine_.get_db());
     unique_move(pf_db::Connection, connection, connection_);
     auto builder = new Builder(connection_.get(), nullptr);
     unique_move(Builder, builder, builder_);
     
     auto mysql_grammar = new grammars::MysqlGrammar();
     unique_move(grammars::Grammar, mysql_grammar, mysql_grammar_);
     auto mysql_builder = new Builder(connection_.get(), mysql_grammar_.get());
     unique_move(Builder, mysql_builder, mysql_builder_);
   }

   static void TearDownTestCase() {
     //std::cout << "TearDownTestCase" << std::endl;
   }

 public:
   virtual void SetUp() {
     builder_->clear();
     mysql_builder_->clear();
   }
   virtual void TearDown() {
   }

 protected:
   static pf_engine::Kernel engine_;
   static std::unique_ptr<pf_db::Connection> connection_;
   static std::unique_ptr<grammars::Grammar> mysql_grammar_;
   static std::unique_ptr<Builder> builder_;
   static std::unique_ptr<Builder> mysql_builder_;

};

pf_engine::Kernel DBQueryBuilder::engine_;
std::unique_ptr<pf_db::Connection> DBQueryBuilder::connection_{nullptr};
std::unique_ptr<Builder> DBQueryBuilder::builder_{nullptr};
std::unique_ptr<Builder> DBQueryBuilder::mysql_builder_{nullptr};
std::unique_ptr<grammars::Grammar> DBQueryBuilder::mysql_grammar_{nullptr};

TEST_F(DBQueryBuilder, construct) {
  Builder object(nullptr, nullptr);
  pf_db::Connection connection(engine_.get_db());
  Builder builder_test1(&connection, nullptr);
  grammars::Grammar grammar;
  Builder builder_test2(&connection, &grammar);
}

TEST_F(DBQueryBuilder, testBasicSelect) {
  builder_->select({"*"}).from("users");
  ASSERT_STREQ("select * from \"users\"", builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testBasicSelectWithGetColumns) {
  builder_->from("users").get();
  ASSERT_TRUE(builder_->columns_.empty());
  
  ASSERT_STREQ("select * from \"users\"", builder_->to_sql().c_str());
  ASSERT_TRUE(builder_->columns_.empty());
}

TEST_F(DBQueryBuilder, testBasicSelectUseWritePdo) {

}

TEST_F(DBQueryBuilder, testBasicTableWrappingProtectsQuotationMarks) {
  builder_->select({"*"}).from("some\"table");
  ASSERT_STREQ("select * from \"some\"\"table\"", builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testAliasWrappingAsWholeConstant) {
  builder_->select({"x.y as foo.bar"}).from("baz");
  ASSERT_STREQ("select \"x\".\"y\" as \"foo.bar\" from \"baz\"", 
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testAliasWrappingWithSpacesInDatabaseName) {
  builder_->select({"w x.y.z as foo.bar"}).from("baz");
  ASSERT_STREQ("select \"w x\".\"y\".\"z\" as \"foo.bar\" from \"baz\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testAddingSelects) {
  builder_->select({"foo"}).
            add_select({"bar"}).add_select({"baz", "boom"}).from("users");
  ASSERT_STREQ("select \"foo\", \"bar\", \"baz\", \"boom\" from \"users\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testBasicSelectWithPrefix) {
  builder_->get_grammar()->set_table_prefix("prefix_");
  builder_->select({"*"}).from("users");
  ASSERT_STREQ("select * from \"prefix_users\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testBasicSelectDistinct) {
  builder_->distinct().select({"foo", "bar"}).from("users");
  ASSERT_STREQ("select distinct \"foo\", \"bar\" from \"users\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testBasicAlias) {
  builder_->select({"foo as bar"}).from("users");
  ASSERT_STREQ("select \"foo\" as \"bar\" from \"users\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testAliasWithPrefix) {
  builder_->get_grammar()->set_table_prefix("prefix_");
  builder_->select({"*"}).from("users as people");
  ASSERT_STREQ("select * from \"prefix_users\" as \"prefix_people\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testJoinAliasesWithPrefix) {
  builder_->get_grammar()->set_table_prefix("prefix_");
  builder_->select({"*"}).from("services").join(
      "translations AS t", "t.item_id", "=", "services.id");
   ASSERT_STREQ(
       "select * from \"prefix_services\" inner join \"prefix_translations\" \
as \"prefix_t\" on \"prefix_t\".\"item_id\" = \"prefix_services\".\"id\"",
       builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testBasicTableWrapping) {
  builder_->select({"*"}).from("public.users");
  ASSERT_STREQ("select * from \"public\".\"users\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testWhenCallback) {
  auto callback = [](Builder *query, const variable_t &condition) {
    ASSERT_TRUE(condition.get<bool>());
    query->where("id", "=", 1);
  };
  builder_->select({"*"}).from("users").when(true, callback).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? and \"email\" = ?",
               builder_->to_sql().c_str());

  builder_->clear();
  builder_->select({"*"}).from("users").when(false, callback).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"email\" = ?",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testWhenCallbackWithReturn) {

}

void assertEquals(
    const variable_array_t &a, const variable_array_t &b, int32_t line = -1) {
  if (line != -1)
    std::cout << "assertEquals: " << line << std::endl;
  ASSERT_TRUE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i)
    ASSERT_STREQ(a[i].data.c_str(), b[i].data.c_str());
}

TEST_F(DBQueryBuilder, testWhenCallbackWithDefault) {
  auto callback = [](Builder *query, const variable_t &condition) {
    ASSERT_STREQ(condition.c_str(), "truthy");
    query->where("id", "=", 1);
  };
  auto def = [](Builder *query, const variable_t &condition) {
    ASSERT_TRUE(condition == 0);
    query->where("id", "=", 2);
  };

  builder_->select({"*"}).
            from("users").when("truthy", callback, def).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? and \"email\" = ?",
               builder_->to_sql().c_str());

  assertEquals({1, "foo"}, builder_->get_bindings(), __LINE__);

  builder_->clear();

  builder_->select({"*"}).
            from("users").when(0, callback, def).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? and \"email\" = ?",
               builder_->to_sql().c_str());

  assertEquals({2, "foo"}, builder_->get_bindings(), __LINE__);
}

TEST_F(DBQueryBuilder, testUnlessCallback) {
  auto callback = [](Builder *query, const variable_t &condition) {
    ASSERT_FALSE(condition.get<bool>());
    query->where("id", "=", 1);
  };

  builder_->select({"*"}).
            from("users").unless(false, callback).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? and \"email\" = ?",
               builder_->to_sql().c_str());


  builder_->clear();
  builder_->select({"*"}).
            from("users").unless(true, callback).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"email\" = ?",
               builder_->to_sql().c_str());

}

TEST_F(DBQueryBuilder, testUnlessCallbackWithReturn) {

}

TEST_F(DBQueryBuilder, testUnlessCallbackWithDefault) {
  auto callback = [](Builder *query, const variable_t &condition) {
    ASSERT_TRUE(condition == 0);
    query->where("id", "=", 1);
  };
  auto def = [](Builder *query, const variable_t &condition) {
    ASSERT_STREQ(condition.c_str(), "truthy");
    query->where("id", "=", 2);
  };

  builder_->select({"*"}).
            from("users").unless(0, callback, def).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? and \"email\" = ?",
               builder_->to_sql().c_str());

  assertEquals({1, "foo"}, builder_->get_bindings(), __LINE__);

  builder_->clear();

  builder_->select({"*"}).
            from("users").unless("truthy", callback, def).where("email", "foo");
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? and \"email\" = ?",
               builder_->to_sql().c_str());

  assertEquals({2, "foo"}, builder_->get_bindings(), __LINE__);
}

TEST_F(DBQueryBuilder, testTapCallback) {
  auto callback = [](Builder *query) {
    query->where("id", "=", 1);
  };

  builder_->select({"*"}).from("users").tap(callback).where("email", "foo"); 
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? and \"email\" = ?",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testBasicWheres) {
  builder_->select({"*"}).from("users").where("id", "=", 1);
  ASSERT_STREQ("select * from \"users\" where \"id\" = ?",
               builder_->to_sql().c_str());
  assertEquals({1}, builder_->get_bindings());
}

TEST_F(DBQueryBuilder, testMySqlWrappingProtectsQuotationMarks) {
/**
  builder_->select({"*"}).from("some`table");
  ASSERT_STREQ("select * from `some``table`",
               builder_->to_sql().c_str());
**/
}

TEST_F(DBQueryBuilder, testDateBasedWheresAcceptsTwoArguments) {
  auto builder = mysql_builder_.get();
  builder->select({"*"}).from("users").where_date("created_at", "1");
  ASSERT_STREQ("select * from `users` where date(`created_at`) = ?",
               builder->to_sql().c_str());

  builder->clear();
  builder->select({"*"}).from("users").where_day("created_at", "1");
  ASSERT_STREQ("select * from `users` where day(`created_at`) = ?",
               builder->to_sql().c_str());

  builder->clear();
  builder->select({"*"}).from("users").where_month("created_at", "1");
  ASSERT_STREQ("select * from `users` where month(`created_at`) = ?",
               builder->to_sql().c_str());

  builder->clear();
  builder->select({"*"}).from("users").where_year("created_at", "1");
  ASSERT_STREQ("select * from `users` where year(`created_at`) = ?",
               builder->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testWhereDayMySql) {
  auto builder = mysql_builder_.get();
  builder->select({"*"}).from("users").where_day("created_at", "=", 1);
  ASSERT_STREQ("select * from `users` where day(`created_at`) = ?",
               builder->to_sql().c_str());
  assertEquals({1}, builder->get_bindings());
}

TEST_F(DBQueryBuilder, testWhereMonthMySql) {
  auto builder = mysql_builder_.get();
  builder->select({"*"}).from("users").where_month("created_at", "=", 5);
  ASSERT_STREQ("select * from `users` where month(`created_at`) = ?",
               builder->to_sql().c_str());
  assertEquals({5}, builder->get_bindings());
}

TEST_F(DBQueryBuilder, testWhereYearMySql) {
  auto builder = mysql_builder_.get();
  builder->select({"*"}).from("users").where_year("created_at", "=", 2018);
  ASSERT_STREQ("select * from `users` where year(`created_at`) = ?",
               builder->to_sql().c_str());
  assertEquals({2018}, builder->get_bindings());
}

TEST_F(DBQueryBuilder, testWhereTimeMySql) {
  auto builder = mysql_builder_.get();
  builder->select({"*"}).from("users").where_time("created_at", "=", "22:00");
  ASSERT_STREQ("select * from `users` where time(`created_at`) = ?",
               builder->to_sql().c_str());
  assertEquals({"22:00"}, builder->get_bindings());
}

TEST_F(DBQueryBuilder, testWhereDatePostgres) {

}

TEST_F(DBQueryBuilder, testWhereDayPostgres) {

}

TEST_F(DBQueryBuilder, testWhereMonthPostgres) {

}

TEST_F(DBQueryBuilder, testWhereYearPostgres) {

}

TEST_F(DBQueryBuilder, testWhereDaySqlite) {

}

TEST_F(DBQueryBuilder, testWhereMonthSqlite) {

}

TEST_F(DBQueryBuilder, testWhereYearSqlite) {

}

TEST_F(DBQueryBuilder, testWhereDaySqlServer) {

}

TEST_F(DBQueryBuilder, testWhereMonthSqlServer) {

}

TEST_F(DBQueryBuilder, testWhereYearSqlServer) {

}

TEST_F(DBQueryBuilder, testWhereBetweens) {
  builder_->select({"*"}).from("users").where_between("id", {1, 2});
  ASSERT_STREQ("select * from \"users\" where \"id\" between ? and ?",
               builder_->to_sql().c_str());
  assertEquals({1, 2}, builder_->get_bindings());

  builder_->clear();
  builder_->select({"*"}).from("users").where_notbetween("id", {1, 2});
  ASSERT_STREQ("select * from \"users\" where \"id\" not between ? and ?",
               builder_->to_sql().c_str());
  assertEquals({1, 2}, builder_->get_bindings());
}

TEST_F(DBQueryBuilder, testBasicOrWheres) {
  builder_->select({"*"}).
            from("users").where("id", "=", 1).or_where("email", "=", "foo");
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? or \"email\" = ?",
               builder_->to_sql().c_str());
  assertEquals({1, "foo"}, builder_->get_bindings());
}

TEST_F(DBQueryBuilder, testRawWheres) {
  builder_->select({"*"}).
            from("users").where_raw("id = ? or email = ?", {1, "foo"});
  ASSERT_STREQ("select * from \"users\" where id = ? or email = ?",
               builder_->to_sql().c_str());
  assertEquals({1, "foo"}, builder_->get_bindings());
}

TEST_F(DBQueryBuilder, testRawOrWheres) {
  builder_->select({"*"}).
            from("users").where("id", "=", 1).or_where_raw("email = ?", {"foo"});
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? or email = ?",
               builder_->to_sql().c_str());
  assertEquals({1, "foo"}, builder_->get_bindings());
}

TEST_F(DBQueryBuilder, testBasicWhereIns) {
  builder_->select({"*"}).from("users").where_in("id", {1, 2, 3});
  ASSERT_STREQ("select * from \"users\" where \"id\" in (?, ?, ?)",
               builder_->to_sql().c_str());
  assertEquals({1, 2, 3}, builder_->get_bindings(), __LINE__);

  builder_->clear();
  builder_->select({"*"}).
            from("users").where("id", "=", 1).or_where_in("id", {1, 2, 3});
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? or \"id\" in (?, ?, ?)",
               builder_->to_sql().c_str());
  assertEquals({1, 1, 2, 3}, builder_->get_bindings(), __LINE__);
}

TEST_F(DBQueryBuilder, testBasicWhereNotIns) {
  builder_->select({"*"}).from("users").where_notin("id", {1, 2, 3});
  ASSERT_STREQ("select * from \"users\" where \"id\" not in (?, ?, ?)",
               builder_->to_sql().c_str());
  assertEquals({1, 2, 3}, builder_->get_bindings(), __LINE__);

  builder_->clear();
  builder_->select({"*"}).
            from("users").where("id", "=", 1).or_where_notin("id", {1, 2, 3});
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? or \"id\" not in (?, ?, ?)",
               builder_->to_sql().c_str());
  assertEquals({1, 1, 2, 3}, builder_->get_bindings(), __LINE__);
}


TEST_F(DBQueryBuilder, testRawWhereIns) {
  using namespace pf_db;
  variable_array_t a{raw(1)};
  builder_->select({"*"}).from("users").where_in("id", a);
  ASSERT_STREQ("select * from \"users\" where \"id\" in (1)",
               builder_->to_sql().c_str());

  builder_->clear();
  builder_->select({"*"}).
            from("users").where("id", "=", 1).or_where_in("id", a);
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? or \"id\" in (1)",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testEmptyWhereIns) {
  using namespace pf_db;
  variable_array_t a;
  builder_->select({"*"}).from("users").where_in("id", a);
  ASSERT_STREQ("select * from \"users\" where 0 = 1",
               builder_->to_sql().c_str());

  builder_->clear();
  builder_->select({"*"}).
            from("users").where("id", "=", 1).or_where_in("id", a);
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? or 0 = 1",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testEmptyWhereNotIns) {
  using namespace pf_db;
  variable_array_t a;
  builder_->select({"*"}).from("users").where_notin("id", a);
  ASSERT_STREQ("select * from \"users\" where 1 = 1",
               builder_->to_sql().c_str());

  builder_->clear();
  builder_->select({"*"}).
            from("users").where("id", "=", 1).or_where_notin("id", a);
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? or 1 = 1",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testBasicWhereColumn) {
  builder_->select({"*"}).
            from("users").where_column("first_name", "last_name").
            or_where_column("first_name", "middle_name");
  ASSERT_STREQ("select * from \"users\" where \"first_name\" = \"last_name\" \
or \"first_name\" = \"middle_name\"",
               builder_->to_sql().c_str());

  builder_->clear();
  builder_->select({"*"}).
            from("users").where_column("updated_at", ">", "created_at");
  ASSERT_STREQ("select * from \"users\" where \"updated_at\" > \"created_at\"",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testArrayWhereColumn) {
  std::vector<variable_array_t> conditions = {
    {"first_name", "last_name"},
    {"updated_at", ">", "created_at"},
  };
  builder_->select({"*"}).from("users").where_column(conditions);
  ASSERT_STREQ("select * from \"users\" where (\"first_name\" = \"last_name\" \
and \"updated_at\" > \"created_at\")",
               builder_->to_sql().c_str());
}

TEST_F(DBQueryBuilder, testUnions) {

  auto builder = mysql_builder_.get();

  builder_->select({"*"}).from("users").where("id", "=", 1);
  auto union_builder = new Builder(connection_.get(), nullptr);
  union_builder->select({"*"}).from("users").where("id", "=", 2);
  builder_->_union(union_builder);
  ASSERT_STREQ("select * from \"users\" where \"id\" = ? union select * from \
\"users\" where \"id\" = ?",
               builder_->to_sql().c_str());

  builder->select({"*"}).from("users").where("id", "=", 1);
  auto union_builder1 = new Builder(connection_.get(), mysql_grammar_.get());
  union_builder1->select({"*"}).from("users").where("id", "=", 2);
  builder->_union(union_builder1);
  ASSERT_STREQ("(select * from `users` where `id` = ?) union (select * from \
`users` where `id` = ?)",
               builder->to_sql().c_str());
 
  builder->clear();
  std::string expected_sql{"(select `a` from `t1` where `a` = ? and `b` = ?) "
  "union (select `a` from `t2` where `a` = ? and `b` = ?) order by `a` "
  "asc limit 10"};
  auto union_builder2 = new Builder(connection_.get(), mysql_grammar_.get());
  union_builder2->select({"a"}).from("t2").where("a", 11).where("b", 2);
  builder->select({"a"}).
           from("t1").where("a", 10).where("b", 1).
           _union(union_builder2).order_by("a").limit(10);
  ASSERT_STREQ(expected_sql.c_str(),
               builder->to_sql().c_str());
  assertEquals({10, 1, 11, 2}, builder->get_bindings());
  
  //SQLite...
}
