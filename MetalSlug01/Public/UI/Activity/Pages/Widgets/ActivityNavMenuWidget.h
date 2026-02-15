	/**
	 * @brief 从ActivitySubsystem填充导航项数据
	 * @note 自动获取所有可用的活动作为导航项
	 */
	void PopulateNavItemsFromSubsystem();
	
	/**
	 * @brief 直接从DataTable加载导航项数据（备用方案）
	 * @note 当Subsystem不可用时使用
	 */
	void LoadNavItemsFromDataTable();
	
	// 红点相关函数