#include "storage/MySqlMgr.h"
#include "config/ConfigMgr.h"

MysqlMgr::MysqlMgr()
{
    auto& cfg = ConfigMgr::Init();
    const auto& host = cfg["Mysql"]["Host"];
    const auto& port = cfg["Mysql"]["Port"];
    const auto& pwd = cfg["Mysql"]["Password"];
    const auto& schema = cfg["Mysql"]["Database"];
    const auto& user = cfg["Mysql"]["User"];

	//mysql 必须加scheem 就像redis-plus-plus —— tcp://127.0.0.1:3306
	std::string url = host;
	if(url.find("://") == std::string::npos)
	{
		url = "tcp://" + host + ":" + port;
	}
	else
	{
		const auto scheme_pos = url.find("://");
		const auto host_pos = (scheme_pos == std::string::npos) ? 0 : (scheme_pos + 3);
		if(url.find(':', host_pos) == std::string::npos)
		{
			url += ":" + port;
		}
	}

	std::cout << "MySQL url: " << url << "  scheme: " << schema << std::endl;
    pool_.reset(new MysqlPool(url, user, pwd, schema, 5));
}

MysqlMgr::~MysqlMgr()
{
    if (pool_) {
        pool_->Close();
    }
}

int MysqlMgr::RegisterUser(const std::string& name,
                                      const std::string& email,
                                      const std::string& password_hash)
{
    if (!pool_) {
        return -1;
    }

    auto con = pool_->getConnection();
    if (!con || !con->_con) {
        return -1;
    }

    Defer return_connection([this, &con]() {
        pool_->returnConnection(std::move(con));
    });

    try {
        auto check_stmt = std::unique_ptr<sql::PreparedStatement>(
            con->_con->prepareStatement(
                "SELECT id FROM users WHERE email = ? LIMIT 1"));

        check_stmt->setString(1, email);
        auto check_result = std::unique_ptr<sql::ResultSet>(
            check_stmt->executeQuery());

        if (check_result->next()) {
            return 0;
        }


        auto stmt = std::unique_ptr<sql::PreparedStatement>(
            con->_con->prepareStatement(
                "INSERT INTO users "
                "(username, email, password_hash, display_name) "
                "VALUES (?, ?, ?, ?)"));
        stmt->setString(1, name);
        stmt->setString(2, email);
        stmt->setString(3, password_hash);
        stmt->setString(4, name);

        if(stmt->executeUpdate() != 1) {
            return -1;
        }

        auto id_stmt = std::unique_ptr<sql::Statement>(
            con->_con->createStatement());

        auto id_result = std::unique_ptr<sql::ResultSet>(
            id_stmt->executeQuery("SELECT LAST_INSERT_ID()"));

        return id_result->next() ? id_result->getInt(1) : -1;
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return e.getErrorCode() == 1062 ? 0 : -1;
    }
}


bool MysqlMgr::CheckPwd(const std::string& email, std::string& password_hash)
{
    auto con = pool_->getConnection();
    if(con == nullptr) {
        return false;
    }

    Defer defer([this, &con](){
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "SELECT password_hash FROM users WHERE email = ?"));
        pstmt->setString(1, email);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        password_hash.clear();
        if (res->next()) {
            password_hash = res->getString("password_hash").asStdString();
            return !password_hash.empty();
        }

        return false;
    }
    catch(sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
    }
}


bool MysqlMgr::UpdatePwd(const std::string& email, const std::string& newpwd)
{
    auto con = pool_->getConnection();
    if(con == nullptr) {
        return false;
    }

    Defer defer([this, &con](){
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "UPDATE users SET password_hash = ? WHERE email = ?"));

        pstmt->setString(1, newpwd);
        pstmt->setString(2, email);

        int updateCount = pstmt->executeUpdate();

        std::cout << "UPDATE ROWS: " << updateCount << std::endl;
        return updateCount;
    }
    catch(sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
    }
}



bool MysqlMgr::GetUserInfo(const std::string& email, UserInfo& user)
{
    auto con = pool_->getConnection();
    if(con == nullptr) {
        return false;
    }

    Defer defer([this, &con](){
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "SELECT id, username FROM users WHERE email = ?"
        ));

        pstmt->setString(1, email);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (res->next()) {
            user.uid = res->getInt("id");
            user.username = res->getString("username").asStdString();
            user.email = email;
            return true;
        }

        return false;
    }
    catch(sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
		std::cerr << " (MySQL error code: " << e.getErrorCode();
		std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
		return false;
    }
}



bool MysqlMgr::GetMeetingRecently(int uid, std::vector<RecentMeetingInfo>& meetings)
{
    // 每次查询前先清空，查询为空时客户端即可按空列表处理。
    meetings.clear();

    if (!pool_ || uid <= 0) {
        return false;
    }

    auto con = pool_->getConnection();
    if (!con || !con->_con) {
        return false;
    }

    Defer defer([this, &con](){
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "SELECT m.meeting_code, m.title, m.status, "
            "m.scheduled_at, m.max_participants, "
            "m.meeting_password_hash IS NOT NULL AS requires_password, "
            "u.display_name AS host_display_name, "
            "u.avatar_url AS host_avatar_url "
            "FROM meeting_participants AS mp "
            "JOIN meetings AS m ON m.id = mp.meeting_id "
            "JOIN users AS u ON u.id = m.host_user_id "
            "WHERE mp.user_id = ? "
            "AND mp.participation_status = 0 "
            "AND m.status = 0 "
            "ORDER BY m.scheduled_at ASC, m.created_at DESC "
            "LIMIT 5"
        ));

        pstmt->setInt(1, uid);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            RecentMeetingInfo meeting;
            meeting.meeting_code = res->getString("meeting_code").asStdString();
            meeting.title = res->getString("title").asStdString();
            meeting.host_display_name = res->getString("host_display_name").asStdString();
            if (!res->isNull("host_avatar_url")) {
                meeting.host_avatar_url = res->getString("host_avatar_url").asStdString();
            }
            meeting.status = static_cast<MeetingStatus>(res->getUInt("status"));
            meeting.requires_password = res->getBoolean("requires_password");
            meeting.max_participants = static_cast<std::uint16_t>(
                res->getUInt("max_participants"));
            if (!res->isNull("scheduled_at")) {
                meeting.scheduled_at = res->getString("scheduled_at").asStdString();
            }
            meetings.push_back(std::move(meeting));
        }

        return true;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}
