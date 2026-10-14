#include "data/ConfigStore.h"

#include <sqlite3.h>

#include "common/Utf.h"

void Sqlite3Closer::operator()(sqlite3* db) const noexcept { sqlite3_close_v2(db); }

namespace {

class Statement {
public:
    Statement(sqlite3* db, const char* sql) { ok_ = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) == SQLITE_OK; }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool ok() const { return ok_; }
    sqlite3_stmt* get() const { return stmt_; }
    void BindText(int i, const std::string& s) const {
        sqlite3_bind_text(stmt_, i, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
    }
private:
    sqlite3_stmt* stmt_ = nullptr;
    bool ok_ = false;
};

constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS rules(
    id TEXT PRIMARY KEY,
    type INTEGER NOT NULL,
    target TEXT NOT NULL,
    title_action INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS sessions(
    id TEXT PRIMARY KEY,
    started_ms INTEGER NOT NULL,
    unlock_ms INTEGER NOT NULL,
    duration_min INTEGER NOT NULL,
    block_task_manager INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS schedules(
    id TEXT PRIMARY KEY,
    spec TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS history(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,
    started_ms INTEGER NOT NULL,
    ended_ms INTEGER NOT NULL,
    outcome TEXT NOT NULL);
PRAGMA user_version = 1;
)sql";

}  // namespace

std::unique_ptr<ConfigStore> ConfigStore::Open(const std::string& utf8Path, std::string* error) {
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(utf8Path.c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    std::unique_ptr<sqlite3, Sqlite3Closer> db(raw);
    if (rc != SQLITE_OK) {
        if (error) *error = raw ? sqlite3_errmsg(raw) : "sqlite3_open_v2 failed";
        return nullptr;
    }
    std::unique_ptr<ConfigStore> store(new ConfigStore(std::move(db)));
    if (!store->CreateSchema(error)) return nullptr;
    return store;
}

bool ConfigStore::CreateSchema(std::string* error) {
    char* msg = nullptr;
    if (sqlite3_exec(db_.get(), kSchema, nullptr, nullptr, &msg) != SQLITE_OK) {
        if (error) *error = msg ? msg : "schema creation failed";
        sqlite3_free(msg);
        return false;
    }
    return true;
}

bool ConfigStore::SaveRule(const BlockRule& rule) {
    if (!IsValidRule(rule) || rule.id.IsNull()) return false;
    Statement st(db_.get(),
                 "INSERT INTO rules(id,type,target,title_action) VALUES(?1,?2,?3,?4) "
                 "ON CONFLICT(id) DO UPDATE SET type=excluded.type, target=excluded.target, "
                 "title_action=excluded.title_action");
    if (!st.ok()) return false;
    st.BindText(1, rule.id.ToString());
    sqlite3_bind_int(st.get(), 2, static_cast<int>(rule.type));
    st.BindText(3, WideToUtf8(rule.target));
    sqlite3_bind_int(st.get(), 4, static_cast<int>(rule.titleAction));
    return sqlite3_step(st.get()) == SQLITE_DONE;
}

std::vector<BlockRule> ConfigStore::LoadRules() const {
    std::vector<BlockRule> out;
    Statement st(db_.get(), "SELECT id,type,target,title_action FROM rules ORDER BY rowid");
    if (!st.ok()) return out;
    while (sqlite3_step(st.get()) == SQLITE_ROW) {
        const auto* idText = reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 0));
        const auto* target = reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 2));
        const int type = sqlite3_column_int(st.get(), 1);
        const int action = sqlite3_column_int(st.get(), 3);
        if (!idText || !target) continue;
        auto id = Guid::FromString(idText);
        // The file is on disk and can be edited: validate before trusting it.
        if (!id || type < 0 || type > static_cast<int>(RuleType::AllowListFolder) || action < 0 ||
            action > static_cast<int>(WindowTitleAction::Suppress))
            continue;
        BlockRule r;
        r.id = *id;
        r.type = static_cast<RuleType>(type);
        r.target = Utf8ToWide(target);
        r.titleAction = static_cast<WindowTitleAction>(action);
        if (IsValidRule(r)) out.push_back(std::move(r));
    }
    return out;
}

bool ConfigStore::DeleteRule(const Guid& id) {
    Statement st(db_.get(), "DELETE FROM rules WHERE id=?1");
    if (!st.ok()) return false;
    st.BindText(1, id.ToString());
    return sqlite3_step(st.get()) == SQLITE_DONE && sqlite3_changes(db_.get()) > 0;
}

bool ConfigStore::SaveSchedule(const ScheduleEntry& e) {
    if (!IsValidSchedule(e)) return false;
    Statement st(db_.get(),
                 "INSERT INTO schedules(id,spec) VALUES(?1,?2) ON CONFLICT(id) DO UPDATE SET spec=excluded.spec");
    if (!st.ok()) return false;
    st.BindText(1, e.id.ToString());
    st.BindText(2, SerializeSchedule(e));
    return sqlite3_step(st.get()) == SQLITE_DONE;
}

std::vector<ScheduleEntry> ConfigStore::LoadSchedules() const {
    std::vector<ScheduleEntry> out;
    Statement st(db_.get(), "SELECT id,spec FROM schedules ORDER BY rowid");
    if (!st.ok()) return out;
    while (sqlite3_step(st.get()) == SQLITE_ROW) {
        const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 0));
        const auto* spec = reinterpret_cast<const char*>(sqlite3_column_text(st.get(), 1));
        if (!id || !spec) continue;
        if (auto e = ParseSchedule(id, spec)) out.push_back(std::move(*e));
    }
    return out;
}

bool ConfigStore::DeleteSchedule(const Guid& id) {
    Statement st(db_.get(), "DELETE FROM schedules WHERE id=?1");
    if (!st.ok()) return false;
    st.BindText(1, id.ToString());
    return sqlite3_step(st.get()) == SQLITE_DONE && sqlite3_changes(db_.get()) > 0;
}
