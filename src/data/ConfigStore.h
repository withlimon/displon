// SQLite-backed rule store (plan Module E). Unencrypted in v1 (open decision in plan Section 10).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "engine/BlockRule.h"
#include "engine/Schedule.h"

struct sqlite3;
struct Sqlite3Closer { void operator()(sqlite3* db) const noexcept; };

class ConfigStore {
public:
    // utf8Path may be ":memory:". On failure returns nullptr and fills *error.
    static std::unique_ptr<ConfigStore> Open(const std::string& utf8Path, std::string* error = nullptr);

    bool SaveRule(const BlockRule& rule);          // upsert by id; invalid rules are rejected
    std::vector<BlockRule> LoadRules() const;      // insertion order
    bool DeleteRule(const Guid& id);

    bool SaveSchedule(const ScheduleEntry& e);          // upsert by id; invalid entries rejected
    std::vector<ScheduleEntry> LoadSchedules() const;   // corrupt/edited rows are skipped
    bool DeleteSchedule(const Guid& id);

private:
    explicit ConfigStore(std::unique_ptr<sqlite3, Sqlite3Closer> db) : db_(std::move(db)) {}
    bool CreateSchema(std::string* error);

    std::unique_ptr<sqlite3, Sqlite3Closer> db_;
};
