#pragma once

#include "async.h"
#include "result.h"

#include <memory>
#include <string>
#include <vector>

namespace orbwvr {

class database {
  private:
    class impl;
    std::shared_ptr<impl> impl_;
    template <typename StartOperation>
    async<result> launch_result_operation(StartOperation start_operation);

  public:
    database(const std::string &conn_string);
    database(const database &other) = delete;
    database &operator=(const database &other) = delete;
    database(database &&other) noexcept;
    database &operator=(database &&other) noexcept;
    ~database();
    async<result> query(std::string sql);
    async<result> query_params(std::string sql,
                               std::vector<std::string> params);
    async<result> prepare(std::string statement_name,
                          std::string sql);
    async<result> query_prepared(std::string statement_name,
                                 std::vector<std::string> params);
};
} // namespace orbwvr