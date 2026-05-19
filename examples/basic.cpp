#include "orbwvr/database.h"

#include <exception>
#include <iostream>
#include <vector>

orbwvr::async<void> run_database_test() {
    try {
        orbwvr::database db(
            "postgresql://postgres:DBNAME@localhost:PORT/postgres");
        std::vector<std::string> params{"1"};
        co_await db.query("SELECT 1");
        std::cout << "SUCCESSFULLY QUERIED DB" << std::endl;
        co_await db.query_params("SELECT $1::int", params);
        std::cout << "SUCCESSFULLY QUERIED DB WITH PARAMS" << std::endl;
        co_await db.prepare("s1", "SELECT $1::int");
        std::cout << "SUCCESSFULLY PREPARED STATEMENT" << std::endl;
        co_await db.query_prepared("s1", params);
        std::cout << "SUCCESSFULLY QUERIED PREPARED STATEMENT" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
    co_return;
}
int main() {
    try {
        orbwvr::sync_wait(run_database_test());
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }
}
