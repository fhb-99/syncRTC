#include "storage/MySqlMgr.h"
#include "config/ConfigMgr.h"

#include <cppconn/datatype.h>

#include <random>

namespace {

std::string GenerateMeetingCode()
{
    std::random_device seed;
    std::mt19937 generator(seed());
    std::uniform_int_distribution<int> digit(0, 9);

    std::string meeting_code;
    meeting_code.reserve(8);
    for (int index = 0; index < 8; ++index) {
        meeting_code.push_back(static_cast<char>('0' + digit(generator)));
    }
    return meeting_code;
}

} // namespace

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
            "u.display_name AS creator_display_name, "
            "u.avatar_url AS creator_avatar_url "
            "FROM meeting_participants AS mp "
            "JOIN meetings AS m ON m.id = mp.meeting_id "
            "JOIN users AS u ON u.id = m.creator_user_id "
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
            meeting.creator_display_name = res->getString("creator_display_name").asStdString();
            if (!res->isNull("creator_avatar_url")) {
                meeting.creator_avatar_url = res->getString("creator_avatar_url").asStdString();
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

bool MysqlMgr::CreateMeeting(const CreateMeetingInfo& create_info,
                             RecentMeetingInfo& meeting)
{
    meeting = {};
    if (!pool_ || create_info.user_id <= 0 || create_info.title.empty() ||
        create_info.title.size() > 200) {
        return false;
    }

    auto con = pool_->getConnection();
    if (!con || !con->_con) {
        return false;
    }

    Defer return_connection([this, &con]() {
        pool_->returnConnection(std::move(con));
    });

    bool committed = false;
    Defer finish_transaction([&con, &committed]() {
        try {
            if (!committed) {
                con->_con->rollback();
            }
            con->_con->setAutoCommit(true);
        }
        catch (const sql::SQLException&) {
            // 连接即将归还连接池，避免析构阶段继续抛出异常。
        }
    });

    try {
        // 为返回给客户端的会议卡片补齐创建者资料，不再额外查询近期会议列表。
        std::unique_ptr<sql::PreparedStatement> user_stmt(con->_con->prepareStatement(
            "SELECT display_name, avatar_url FROM users WHERE id = ? LIMIT 1"));
        user_stmt->setInt(1, create_info.user_id);
        std::unique_ptr<sql::ResultSet> user_result(user_stmt->executeQuery());
        if (!user_result->next()) {
            return false;
        }

        meeting.title = create_info.title;
        meeting.creator_display_name = user_result->getString("display_name").asStdString();
        if (!user_result->isNull("avatar_url")) {
            meeting.creator_avatar_url = user_result->getString("avatar_url").asStdString();
        }
        meeting.status = MeetingStatus::kScheduled;
        meeting.requires_password = !create_info.password_hash.empty();
        meeting.max_participants = 30;
        meeting.scheduled_at = create_info.scheduled_at;

        con->_con->setAutoCommit(false);

        bool meeting_inserted = false;
        for (int attempt = 0; attempt < 5; ++attempt) {
            meeting.meeting_code = GenerateMeetingCode();
            try {
                std::unique_ptr<sql::PreparedStatement> meeting_stmt(con->_con->prepareStatement(
                    "INSERT INTO meetings "
                    "(meeting_code, title, creator_user_id, "
                    "meeting_password_hash, scheduled_at, started_at) "
                    "VALUES (?, ?, ?, ?, ?, ?)"));
                meeting_stmt->setString(1, meeting.meeting_code);
                meeting_stmt->setString(2, create_info.title);
                meeting_stmt->setInt(3, create_info.user_id);
                if (create_info.password_hash.empty()) {
                    meeting_stmt->setNull(4, sql::DataType::VARCHAR);
                }
                else {
                    meeting_stmt->setString(4, create_info.password_hash);
                }
                if (create_info.scheduled_at.empty()) {
                    meeting_stmt->setNull(5, sql::DataType::TIMESTAMP);
                    meeting_stmt->setNull(6, sql::DataType::TIMESTAMP);
                }
                else {
                    meeting_stmt->setString(5, create_info.scheduled_at);
                    meeting_stmt->setString(6, create_info.scheduled_at);
                }

                if (meeting_stmt->executeUpdate() == 1) {
                    meeting_inserted = true;
                    break;
                }
            }
            catch (const sql::SQLException& e) {
                // 会议号有唯一索引；极低概率碰撞时重新生成即可。
                if (e.getErrorCode() != 1062) {
                    throw;
                }
            }
        }

        if (!meeting_inserted) {
            return false;
        }

        std::unique_ptr<sql::Statement> id_stmt(con->_con->createStatement());
        std::unique_ptr<sql::ResultSet> id_result(
            id_stmt->executeQuery("SELECT LAST_INSERT_ID()"));
        if (!id_result->next()) {
            return false;
        }

        const std::uint64_t meeting_id = id_result->getUInt64(1);
        std::unique_ptr<sql::PreparedStatement> participant_stmt(con->_con->prepareStatement(
            "INSERT INTO meeting_participants (meeting_id, user_id) VALUES (?, ?)"));
        participant_stmt->setUInt64(1, meeting_id);
        participant_stmt->setInt(2, create_info.user_id);
        if (participant_stmt->executeUpdate() != 1) {
            return false;
        }

        con->_con->commit();
        committed = true;
        return true;
    }
    catch (const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}



bool MysqlMgr::GetHistoryMeeting(std::vector<HistoryMeetingInfo>& meetings)
{
    // 如果数据过多，还要进行分页处理


    auto con = pool_->getConnection();
    if(con == nullptr) {
        return false;
    }

    Defer defer([this, &con](){
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "SELECT m.meeting_code, m.title, user.display_name, "
            "user.avatar_url, m.started_at, m.ended_at "
            "FROM meetings AS m "
            "JOIN users AS user ON m.creator_user_id = user.id "
            "WHERE m.created_at < NOW() "
            "ORDER BY m.created_at DESC "
            "LIMIT 5"
        ));

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while(res->next()) {
            HistoryMeetingInfo meeting;
            meeting.meeting_code = res->getString("meeting_code").asStdString();
            meeting.title = res->getString("title").asStdString();
            meeting.creator_display_name = res->getString("display_name").asStdString();
            if(!res->isNull("avatar_url")) {
                meeting.creator_avatar_url = res->getString("creator_avatar_url").asStdString();
            }
            meeting.started_at = res->getString("started_at").asStdString();
            if(!res->isNull("ended_at")) {
                meeting.ended_at = res->getString("ended_at").asStdString();
            }

            meetings.push_back(std::move(meeting));
        }

        return true;
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}
std::string meeting_code;       // 用户复制或输入的会议号
    std::string title;              // 会议标题
    std::string creator_display_name; // 创建者名称
    std::string creator_avatar_url;   // 创建者头像
    std::string started_at;         // 会议正式开始时间，一般就是预约开始时间
    std::string ended_at;           // 会议结束时间