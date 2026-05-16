#pragma once

#include "PgResult.h"

#include <libpq-fe.h>

#include <memory>
#include <string>

#include <asio.hpp>
#include <asio/experimental/parallel_group.hpp>

class PgConnection {

  private:
    PGconn *conn_ = nullptr;
    std::unique_ptr<asio::posix::stream_descriptor> socket_;

    ConnStatusType getStatus() const;

    asio::awaitable<void> flush_outgoing();

    asio::awaitable<PgResult> read_results();

    asio::awaitable<void> wait_readable();

    asio::awaitable<void> wait_writeable();

    asio::awaitable<bool> wait_read_or_write();

  public:
    PgConnection(asio::io_context &ctx, const std::string &conn_string);
    PgConnection(const PgConnection &other) = delete;
    PgConnection &operator=(const PgConnection &other) = delete;
    PgConnection(PgConnection &&other) noexcept;
    PgConnection &operator=(PgConnection &&other) noexcept;
    ~PgConnection();

    bool is_open() const;
    asio::awaitable<PgResult> query(const std::string &sql);

    asio::awaitable<PgResult>
    query_params(const std::string &sql,
                 const std::vector<std::string> &params);

    // overloading query_params for optional param_types
    asio::awaitable<PgResult>
    query_params(const std::string &sql, const std::vector<std::string> &params,
                 const std::vector<Oid> &param_types);

    asio::awaitable<PgResult> prepare(const std::string &statement_name,
                                      const std::string &sql);

    // overloading prepare for optional param_types
    asio::awaitable<PgResult> prepare(const std::string &statement_name,
                                      const std::string &sql,
                                      const std::vector<Oid> &param_types);

    asio::awaitable<PgResult>
    query_prepared(const std::string &statement_name,
                   const std::vector<std::string> &params);

    asio::awaitable<void> connect();
};