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
            "AND mp.participation_status IN (0, 1) "
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



bool MysqlMgr::GetHistoryMeeting(int uid, std::vector<HistoryMeetingInfo>& meetings)
{
    meetings.clear();

    if (!pool_ || uid <= 0) {
        return false;
    }

    // 如果数据过多，还要进行分页处理


    auto con = pool_->getConnection();
    if (!con || !con->_con) {
        return false;
    }

    Defer defer([this, &con](){
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "SELECT m.meeting_code, m.title, m.started_at, m.ended_at, "
            "creator.display_name AS creator_display_name, "
            "creator.avatar_url AS creator_avatar_url "
            "FROM meeting_participants AS mp "
            "JOIN meetings AS m ON m.id = mp.meeting_id "
            "JOIN users AS creator ON creator.id = m.creator_user_id "
            "WHERE mp.user_id = ? "
            "AND mp.participation_status = 1 "
            "AND m.status = 2 "
            "ORDER BY m.ended_at DESC, m.created_at DESC "
            "LIMIT 5"
        ));

        pstmt->setInt(1, uid);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        while(res->next()) {
            HistoryMeetingInfo meeting;
            meeting.meeting_code = res->getString("meeting_code").asStdString();
            meeting.title = res->getString("title").asStdString();
            meeting.creator_display_name = res->getString("creator_display_name").asStdString();
            if (!res->isNull("creator_avatar_url")) {
                meeting.creator_avatar_url = res->getString("creator_avatar_url").asStdString();
            }
            if (!res->isNull("started_at")) {
                meeting.started_at = res->getString("started_at").asStdString();
            }
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



bool MysqlMgr::GetMeetingInfoByCode(const std::string& meeting_code, MeetingInfo& meeting)
{
    auto con = pool_->getConnection();
    if (!con || !con->_con) {
        return false;
    }

    Defer defer([this, &con](){
        pool_->returnConnection(std::move(con));
    });

    try {
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "SELECT m.id, m.meeting_code, m.title, m.creator_user_id, "
            "m.status, m.visibility, "
            "m.meeting_password_hash IS NOT NULL AS requires_password, "
            "m.max_participants, m.scheduled_at, m.started_at, m.ended_at, "
            "m.created_at, m.updated_at "
            "FROM meetings AS m "
            "WHERE m.meeting_code = ? "
            "LIMIT 1"
        ));

        pstmt->setString(1, meeting_code);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        if (res->next()) {
            meeting.meeting_id = res->getUInt64("id");
            meeting.meeting_code = res->getString("meeting_code").asStdString();
            meeting.title = res->getString("title").asStdString();
            meeting.creator_user_id = res->getUInt64("creator_user_id");
            meeting.status = static_cast<MeetingStatus>(res->getUInt("status"));
            meeting.visibility = static_cast<MeetingVisibility>(res->getUInt("visibility"));
            meeting.requires_password = res->getBoolean("requires_password");
            meeting.max_participants = static_cast<std::uint16_t>(res->getUInt("max_participants"));

            return true;
        }

        return false;
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlMgr::GetUserDisplayNameById(int user_id, std::string& display_name)
{
    display_name.clear();
    if (!pool_ || user_id <= 0) {
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
            "SELECT COALESCE(NULLIF(display_name, ''), username) AS display_name "
            "FROM users WHERE id = ? LIMIT 1"));
        pstmt->setInt(1, user_id);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) {
            return false;
        }

        display_name = res->getString("display_name").asStdString();
        return !display_name.empty();
    }
    catch (const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlMgr::GetMeetingInfoById(std::uint64_t meeting_id, MeetingInfo& meeting)
{
    if (!pool_ || meeting_id == 0) {
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
            "SELECT m.id, m.meeting_code, m.title, m.creator_user_id, "
            "m.status, m.visibility, "
            "m.meeting_password_hash IS NOT NULL AS requires_password, "
            "m.max_participants "
            "FROM meetings AS m WHERE m.id = ? LIMIT 1"));
        pstmt->setUInt64(1, meeting_id);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next()) {
            return false;
        }

        meeting.meeting_id = res->getUInt64("id");
        meeting.meeting_code = res->getString("meeting_code").asStdString();
        meeting.title = res->getString("title").asStdString();
        meeting.creator_user_id = res->getUInt64("creator_user_id");
        meeting.status = static_cast<MeetingStatus>(res->getUInt("status"));
        meeting.visibility = static_cast<MeetingVisibility>(res->getUInt("visibility"));
        meeting.requires_password = res->getBoolean("requires_password");
        meeting.max_participants = static_cast<std::uint16_t>(res->getUInt("max_participants"));
        return true;
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlMgr::StartMeeting(std::uint64_t meeting_id, int user_id)
{
    if (!pool_ || meeting_id == 0 || user_id <= 0) {
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
            "UPDATE meetings SET status = ?, started_at = NOW(3), ended_at = NULL "
            "WHERE id = ? AND creator_user_id = ? AND status = ?"));
        pstmt->setUInt(1, static_cast<unsigned int>(MeetingStatus::kInProgress));
        pstmt->setUInt64(2, meeting_id);
        pstmt->setInt(3, user_id);
        pstmt->setUInt(4, static_cast<unsigned int>(MeetingStatus::kScheduled));
        return pstmt->executeUpdate() == 1;
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}

bool MysqlMgr::EndMeeting(std::uint64_t meeting_id, int user_id)
{
    if (!pool_ || meeting_id == 0 || user_id <= 0) {
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
            "UPDATE meetings SET status = ?, ended_at = NOW(3) "
            "WHERE id = ? AND creator_user_id = ? AND status = ?"));
        pstmt->setUInt(1, static_cast<unsigned int>(MeetingStatus::kEnded));
        pstmt->setUInt64(2, meeting_id);
        pstmt->setInt(3, user_id);
        pstmt->setUInt(4, static_cast<unsigned int>(MeetingStatus::kInProgress));
        return pstmt->executeUpdate() == 1;
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}


bool MysqlMgr::GetMeetingPasswordHash(std::uint64_t meeting_id, std::string& password_hash)
{
    password_hash.clear();

    if (!pool_ || meeting_id == 0) {
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
            "SELECT meeting_password_hash FROM meetings WHERE id = ? LIMIT 1"));
        pstmt->setUInt64(1, meeting_id);

        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (!res->next() || res->isNull("meeting_password_hash")) {
            return false;
        }

        password_hash = res->getString("meeting_password_hash").asStdString();
        return !password_hash.empty();
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}


bool MysqlMgr::UpdateMeetingPart(const MeetingInfo& meeting, int uid)
{
    if (!pool_ || meeting.meeting_id == 0 || uid <= 0) {
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
        if (meeting.visibility == MeetingVisibility::kPrivate &&
            meeting.creator_user_id != static_cast<std::uint64_t>(uid)) {
            // 私密会议只允许已经在参与表中的用户入会，避免仅凭会议号闯入。
            std::unique_ptr<sql::PreparedStatement> check_stmt(con->_con->prepareStatement(
                "SELECT 1 FROM meeting_participants "
                "WHERE meeting_id = ? AND user_id = ? AND participation_status <> 2 "
                "LIMIT 1"));
            check_stmt->setUInt64(1, meeting.meeting_id);
            check_stmt->setInt(2, uid);
            std::unique_ptr<sql::ResultSet> check_result(check_stmt->executeQuery());
            if (!check_result->next()) {
                return false;
            }

            std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
                "UPDATE meeting_participants "
                "SET participation_status = 1, "
                "joined_at = IFNULL(joined_at, NOW(3)), "
                "left_at = NULL "
                "WHERE meeting_id = ? AND user_id = ? AND participation_status <> 2"));
            pstmt->setUInt64(1, meeting.meeting_id);
            pstmt->setInt(2, uid);
            pstmt->executeUpdate();
            return true;
        }

        // 公开会议允许用户通过会议号直接加入：没有参与记录就插入，有记录就幂等更新。
        std::unique_ptr<sql::PreparedStatement> pstmt(con->_con->prepareStatement(
            "INSERT INTO meeting_participants "
            "(meeting_id, user_id, participation_status, planned_at, joined_at, left_at) "
            "VALUES (?, ?, 1, NOW(3), NOW(3), NULL) "
            "ON DUPLICATE KEY UPDATE "
            "participation_status = 1, "
            "joined_at = IFNULL(joined_at, NOW(3)), "
            "left_at = NULL"));
        pstmt->setUInt64(1, meeting.meeting_id);
        pstmt->setInt(2, uid);
        pstmt->executeUpdate();
        return true;
    }
    catch(const sql::SQLException& e) {
        std::cerr << "SQLException: " << e.what();
        std::cerr << " (MySQL error code: " << e.getErrorCode();
        std::cerr << ", SQLState: " << e.getSQLState() << " )" << std::endl;
        return false;
    }
}
