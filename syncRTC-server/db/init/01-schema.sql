CREATE DATABASE IF NOT EXISTS `syncrtc`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;

USE `syncrtc`;

CREATE TABLE IF NOT EXISTS `users` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `username` VARCHAR(64) NOT NULL,
    `email` VARCHAR(254) NOT NULL,
    `password_hash` VARCHAR(255) NOT NULL,
    `display_name` VARCHAR(128) NOT NULL,
    `avatar_url` VARCHAR(512) DEFAULT NULL,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `email_verified_at` DATETIME(3) DEFAULT NULL,
    `last_login_at` DATETIME(3) DEFAULT NULL,
    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_users_username` (`username`),
    UNIQUE KEY `uk_users_email` (`email`),
    KEY `idx_users_status_created_at` (`status`, `created_at`)
) ENGINE=InnoDB COMMENT='User accounts';

CREATE TABLE IF NOT EXISTS `user_contacts` (
    `user_id` BIGINT UNSIGNED NOT NULL,
    `contact_user_id` BIGINT UNSIGNED NOT NULL,
    `relation_status` TINYINT UNSIGNED NOT NULL DEFAULT 1,
    `alias` VARCHAR(128) DEFAULT NULL,
    `remark` VARCHAR(255) DEFAULT NULL,
    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (`user_id`, `contact_user_id`),
    KEY `idx_user_contacts_contact_status` (`contact_user_id`, `relation_status`),
    CONSTRAINT `chk_user_contacts_not_self`
        CHECK (`user_id` <> `contact_user_id`),
    CONSTRAINT `fk_user_contacts_user`
        FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_user_contacts_contact_user`
        FOREIGN KEY (`contact_user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB COMMENT='Directed user contact relationships';

CREATE TABLE IF NOT EXISTS `meetings` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `meeting_code` VARCHAR(12) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    `title` VARCHAR(200) NOT NULL,
    `creator_user_id` BIGINT UNSIGNED NOT NULL,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `visibility` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `meeting_password_hash` VARCHAR(255) DEFAULT NULL,
    `max_participants` SMALLINT UNSIGNED NOT NULL DEFAULT 30,
    `scheduled_at` DATETIME(3) DEFAULT NULL,
    `started_at` DATETIME(3) DEFAULT NULL,
    `ended_at` DATETIME(3) DEFAULT NULL,
    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_meetings_meeting_code` (`meeting_code`),
    KEY `idx_meetings_creator_created_at` (`creator_user_id`, `created_at`),
    KEY `idx_meetings_status_scheduled_at` (`status`, `scheduled_at`),
    CONSTRAINT `chk_meetings_meeting_code_length`
        CHECK (CHAR_LENGTH(`meeting_code`) BETWEEN 6 AND 12),
    CONSTRAINT `chk_meetings_max_participants`
        CHECK (`max_participants` BETWEEN 2 AND 30),
    CONSTRAINT `fk_meetings_creator_user`
        FOREIGN KEY (`creator_user_id`) REFERENCES `users` (`id`) ON DELETE RESTRICT
) ENGINE=InnoDB COMMENT='Meeting lifecycle and access settings';

CREATE TABLE IF NOT EXISTS `meeting_participants` (
    `meeting_id` BIGINT UNSIGNED NOT NULL,
    `user_id` BIGINT UNSIGNED NOT NULL,
    `participation_status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `planned_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    `joined_at` DATETIME(3) DEFAULT NULL,
    `left_at` DATETIME(3) DEFAULT NULL,
    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3),
    PRIMARY KEY (`meeting_id`, `user_id`),
    KEY `idx_meeting_participants_user_status_planned_at`
        (`user_id`, `participation_status`, `planned_at`),
    KEY `idx_meeting_participants_meeting_status`
        (`meeting_id`, `participation_status`),
    CONSTRAINT `chk_meeting_participants_status`
        CHECK (`participation_status` IN (0, 1, 2)),
    CONSTRAINT `chk_meeting_participants_leave_after_join`
        CHECK (
            `left_at` IS NULL
            OR (`joined_at` IS NOT NULL AND `left_at` >= `joined_at`)
        ),
    CONSTRAINT `fk_meeting_participants_meeting`
        FOREIGN KEY (`meeting_id`) REFERENCES `meetings` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_meeting_participants_user`
        FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB COMMENT='Planned and actual meeting participation';
