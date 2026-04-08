mutable std::mutex mutex_;
std::unordered_map<uint32_t, Scene> scenes_;           // room_id -> scene
std::unordered_map<uint32_t, uint32_t> player_scene_;  // player_id -> room_id

asio::io_context* io_context_ = nullptr;
std::shared_ptr<asio::thread_pool> logic_thread_pool_;
std::shared_ptr<asio::thread_pool> dispatch_thread_pool_;
std::shared_ptr<asio::strand<asio::thread_pool::executor_type>> dispatch_strand_;
UdpServer* udp_server_ = nullptr;
ServerConfig config_;
PlayerRolesConfig player_roles_config_;
EnemyTypesConfig enemy_types_config_;
ItemsConfig items_config_;
UpgradeConfig upgrade_config_;
