
	/**
	 * @brief 强制刷新所有页面，重新获取内存数据
	 * @note 用于调试目的，强制所有UI组件重新获取最新内存数据
	 */
	void ForceRefreshAllPages();

	/**
	 * @brief 游戏关闭时自动保存内存数据到磁盘
	 * @note 在游戏退出前调用，确保所有修改都被持久化
	 */
	void AutoSaveOnGameExit();
