#include "PgConnection.h"
#include "PgResult.h"

#include <libpq-fe.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include <asio.hpp>
#include <asio/experimental/parallel_group.hpp>

// Forward declaration
static std::vector<const char *> str_to_char_pointer(
    const std::vector<std::string> &string_vector);

PgConnection::PgConnection(asio::io_context &ctx,
                           const std::string &conn_string) {
    conn_ = PQconnectStart(conn_string.c_str());
    if (conn_ == nullptr) {
        throw std::runtime_error("Failed to allocate Postgres connection");
    }

    if (getStatus() == CONNECTION_BAD) {
        throw std::runtime_error(PQerrorMessage(conn_));
    }

    int conn_fd = PQsocket(conn_);
    if (conn_fd < 0) {
        throw std::runtime_error("Could not retrieve socket from Postgres");
    }

    socket_ = std::make_unique<asio::posix::stream_descriptor>(ctx, conn_fd);
}

PgConnection::PgConnection(PgConnection &&other) noexcept {
    conn_ = other.conn_;
    socket_ = std::move(other.socket_);
    other.conn_ = nullptr;
}

PgConnection &PgConnection::operator=(PgConnection &&other) noexcept {
    if (this != &other) {

        if (conn_ != nullptr) {
            PQfinish(conn_);
        }

        conn_ = other.conn_;
        other.conn_ = nullptr;
        socket_ = std::move(other.socket_);
    }

    return *this;
}

PgConnection::~PgConnection() {
    if (conn_ != nullptr) {
        PQfinish(conn_);
    }
}

bool PgConnection::is_open() const { return PQstatus(conn_) == CONNECTION_OK; }

ConnStatusType PgConnection::getStatus() const {
    if (conn_ == nullptr) {
        return CONNECTION_BAD;
    }

    return PQstatus(conn_);
}

asio::awaitable<PgResult> PgConnection::query(const std::string &sql) {
    if (PQsendQuery(conn_, sql.c_str()) == 0) {
        throw std::runtime_error(PQerrorMessage(conn_));
    }

    co_await flush_outgoing();
    co_return co_await read_results();
}

asio::awaitable<PgResult>
PgConnection::query_params(const std::string &sql,
                           const std::vector<std::string> &params) {
    co_return co_await query_params(sql, params, {});
}

asio::awaitable<PgResult>
PgConnection::query_params(const std::string &sql,
                           const std::vector<std::string> &params,
                           const std::vector<Oid> &param_types) {
                            
    std::vector<const char *> param_values = str_to_char_pointer(params);
    if (PQsendQueryParams(conn_, sql.c_str(), static_cast<int>(params.size()),
                          param_types.empty() ? nullptr : param_types.data(),
                          param_values.empty() ? nullptr : param_values.data(),
                          nullptr, nullptr, 0) == 0) {
        throw std::runtime_error(PQerrorMessage(conn_));
    }

    co_await flush_outgoing();
    co_return co_await read_results();
}

asio::awaitable<PgResult>
PgConnection::prepare(const std::string &statement_name,
                      const std::string &sql) {
    co_return co_await prepare(statement_name, sql, {});
}

asio::awaitable<PgResult>
PgConnection::prepare(const std::string &statement_name, const std::string &sql,
                      const std::vector<Oid> &param_types) {
    if (PQsendPrepare(conn_, statement_name.c_str(), sql.c_str(),
                      static_cast<int>(param_types.size()),
                      param_types.empty() ? nullptr : param_types.data()) ==
        0) {
        throw std::runtime_error(PQerrorMessage(conn_));
    }

    co_await flush_outgoing();
    co_return co_await read_results();
}

asio::awaitable<PgResult>
PgConnection::query_prepared(const std::string &statement_name,
                             const std::vector<std::string> &params) {

    std::vector<const char *> param_values = str_to_char_pointer(params);

    if (PQsendQueryPrepared(conn_, statement_name.c_str(), static_cast<int>(params.size()),
                            param_values.empty() ? nullptr
                                                 : param_values.data(),
                            nullptr, nullptr, 0) == 0) {
        throw std::runtime_error(PQerrorMessage(conn_));
    }

    co_await flush_outgoing();
    co_return co_await read_results();
}

asio::awaitable<void> PgConnection::connect() {
    while (true) {
        PostgresPollingStatusType status = PQconnectPoll(conn_);
        if (status == PGRES_POLLING_OK) {
            if (PQsetnonblocking(conn_, 1) == -1) {
                throw std::runtime_error(
                    "Failed to set Postgres connection to non-blocking mode");
            }
            co_return;
        }

        if (status == PGRES_POLLING_FAILED) {
            throw std::runtime_error(PQerrorMessage(conn_));
        }

        if (status == PGRES_POLLING_READING) {
            co_await wait_readable();
        }

        if (status == PGRES_POLLING_WRITING) {
            co_await wait_writeable();
        }
    }
}

asio::awaitable<void> PgConnection::flush_outgoing() {
    while (true) {
        int flush_status = PQflush(conn_);
        if (flush_status == 0) {
            co_return;
        }

        if (flush_status == -1) {
            throw std::runtime_error(PQerrorMessage(conn_));
        }

        if (co_await wait_read_or_write() == true) {
            if (PQconsumeInput(conn_) == 0) {
                throw std::runtime_error(PQerrorMessage(conn_));
            }
        }
    }
}

asio::awaitable<PgResult> PgConnection::read_results() {
    std::optional<PgResult> latest_result;
    bool query_failed = false;
    std::string error_message;
    while (true) {
        co_await wait_readable();
        // fill buffer with network data from socket
        if (PQconsumeInput(conn_) == 0) {
            throw std::runtime_error(PQerrorMessage(conn_));
        }

        while (PQisBusy(conn_) == 0) {
            PGresult *result = PQgetResult(conn_);
            if (result == nullptr) {

                if (query_failed) {
                    throw std::runtime_error(error_message);
                }

                if (!latest_result.has_value()) {
                    throw std::runtime_error(
                        "Query complete but didn't return PGresult");
                }

                co_return std::move(latest_result.value());
            }

            PgResult current_result(result);
            if (!PgResult::is_success(current_result.getResponseStatus())) {
                query_failed = true;
                error_message = PQresultErrorMessage(result);
            }

            latest_result = std::move(current_result);
        }
    }
}

asio::awaitable<void> PgConnection::wait_readable() {
    co_await socket_->async_wait(asio::posix::descriptor_base::wait_read,
                                 asio::use_awaitable);
}

asio::awaitable<void> PgConnection::wait_writeable() {
    co_await socket_->async_wait(asio::posix::descriptor_base::wait_write,
                                 asio::use_awaitable);
}

asio::awaitable<bool> PgConnection::wait_read_or_write() {
    auto [completion_order, read_err, write_err] =
        co_await asio::experimental::make_parallel_group(
            socket_->async_wait(asio::posix::descriptor_base::wait_read,
                                asio::deferred),
            socket_->async_wait(asio::posix::descriptor_base::wait_write,
                                asio::deferred))
            .async_wait(asio::experimental::wait_for_one(),
                        asio::use_awaitable);
    if (completion_order[0] == 0) {
        if (read_err) {
            throw std::runtime_error(read_err.message());
        }
        co_return true;
    }

    if (write_err) {
        throw std::runtime_error(write_err.message());
    }
    co_return false;
}

static std::vector<const char *> str_to_char_pointer(
    const std::vector<std::string> &string_vector) {
    std::vector<const char *> char_pointer_vector;
    char_pointer_vector.reserve(string_vector.size());

    for (const std::string &param : string_vector) {
        char_pointer_vector.push_back(param.c_str());
    }
    return char_pointer_vector;
}
