#include "orbwvr/database.h"
#include "postgres/PgConnection.h"
#include "postgres/PgResult.h"

#include <asio.hpp>

#include <exception>
#include <memory>
#include <string>
#include <thread>

namespace orbwvr {
class database::impl {
    friend class database;

  private:
    asio::io_context ctx_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    postgres::detail::PgConnection conn_;
    std::thread worker_;
    asio::steady_timer connect_waiter_;
    bool connected_ = false;
    std::exception_ptr conn_error_;

  public:
    impl(const std::string &conn_string)
        : ctx_(), work_guard_(asio::make_work_guard(ctx_)),
          conn_(ctx_, conn_string), worker_([this] { ctx_.run(); }),
          connect_waiter_(ctx_, asio::steady_timer::time_point::max()) {
        asio::co_spawn(ctx_, conn_.connect(), [this](std::exception_ptr err) {
            if (err) {
                conn_error_ = std::move(err);
            } else {
                connected_ = true;
            }
            connect_waiter_.cancel();
        });
    }

    asio::awaitable<void> ensure_connected() {
        while (true) {
            if (conn_error_) {
                std::rethrow_exception(conn_error_);
            }
            if (connected_) {
                co_return;
            }
            asio::error_code ec;
            co_await connect_waiter_.async_wait(
                asio::redirect_error(asio::use_awaitable, ec));
        }
    };
    ~impl() {
        work_guard_.reset();
        ctx_.stop();
        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                worker_.detach();
            } else {
                worker_.join();
            }
        }
    }
    impl(const impl &) = delete;
    impl &operator=(const impl &other) = delete;
    impl(impl &&other) = delete;
    impl &operator=(impl &&other) = delete;
};

template <typename StartOperation>
async<result>
database::launch_result_operation(StartOperation start_operation) {
    auto [async_op, producer] = async<result>::make_pending();
    auto impl = impl_;
    asio::co_spawn(
        impl->ctx_,
        [impl, start_operation = std::move(start_operation),
         producer = std::move(producer)]() mutable -> asio::awaitable<void> {
            try {
                co_await impl->ensure_connected();
                postgres::detail::PgResult pg_result =
                    co_await start_operation();
                result wrapped_result(std::move(pg_result));
                producer.set_value(std::move(wrapped_result));
            } catch (...) {
                producer.set_error(std::current_exception());
            }
            co_return;
        },
        asio::detached);
    return std::move(async_op);
}

database::database(const std::string &conn_string)
    : impl_(std::make_shared<impl>(conn_string)) {}

database::database(database &&other) noexcept = default;
database &database::operator=(database &&other) noexcept = default;
database::~database() = default;

async<result> database::query(std::string sql) {
    auto impl = impl_;
    return launch_result_operation(
        [impl, sql = std::move(
                   sql)]() -> asio::awaitable<postgres::detail::PgResult> {
            co_return co_await impl->conn_.query(sql);
        });
}

async<result> database::query_params(std::string sql,
                                     std::vector<std::string> params) {
    auto impl = impl_;
    return launch_result_operation(
        [impl, sql = std::move(sql), params = std::move(params)]()
            -> asio::awaitable<postgres::detail::PgResult> {
            co_return co_await impl->conn_.query_params(sql, params);
        });
}

async<result> database::prepare(std::string statement_name, std::string sql) {
    auto impl = impl_;
    return launch_result_operation(
        [impl, statement_name = std::move(statement_name),
         sql =
             std::move(sql)]() -> asio::awaitable<postgres::detail::PgResult> {
            co_return co_await impl->conn_.prepare(statement_name, sql);
        });
}

async<result> database::query_prepared(std::string statement_name,
                                       std::vector<std::string> params) {
    auto impl = impl_;
    return launch_result_operation(
        [impl, statement_name = std::move(statement_name),
         params = std::move(
             params)]() -> asio::awaitable<postgres::detail::PgResult> {
            co_return co_await impl->conn_.query_prepared(statement_name,
                                                          params);
        });
}
} // namespace orbwvr
