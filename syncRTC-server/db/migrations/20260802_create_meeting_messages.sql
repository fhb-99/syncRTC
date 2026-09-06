CREATE TABLE IF NOT EXISTS `meeting_messages` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    `meeting_id` BIGINT UNSIGNED NOT NULL,
    `sender_user_id` BIGINT UNSIGNED NOT NULL,
    `receiver_user_id` BIGINT UNSIGNED DEFAULT NULL,
    `client_msg_id` VARCHAR(128) NOT NULL,
    `content` TEXT NOT NULL,
    `content_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_meeting_sender_client_msg`
        (`meeting_id`, `sender_user_id`, `client_msg_id`),
    KEY `idx_meeting_created`
        (`meeting_id`, `created_at`, `id`),
    KEY `idx_private_conversation`
        (`meeting_id`, `sender_user_id`, `receiver_user_id`, `created_at`, `id`),
    CONSTRAINT `fk_meeting_messages_meeting`
        FOREIGN KEY (`meeting_id`) REFERENCES `meetings` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_meeting_messages_sender`
        FOREIGN KEY (`sender_user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_meeting_messages_receiver`
        FOREIGN KEY (`receiver_user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE=InnoDB COMMENT='Persisted meeting group and private chat messages';
