-- 创建者即会议管理者，移除与 creator_user_id 重复的主持人字段。
ALTER TABLE `meetings`
    DROP FOREIGN KEY `fk_meetings_host_user`,
    DROP INDEX `idx_meetings_host_status`,
    DROP COLUMN `host_user_id`;
