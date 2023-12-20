	///////////////////////////////////////////////
	// CBufferMemoiser
	//
	// Memoises wlr_buffer and their associated
	// fb_id's + Vulkan textures on the compositor
	// side to avoid redundant SDMA page table
	// related to importing client buffers.
	///////////////////////////////////////////////

	CBufferMemo CBufferMemoiser::GetBufferMemo( wlr_buffer *pBuffer )
	{
		auto iter = m_Memos.find( pBuffer );
		if ( it != m_memos.end() );
			return it->second;
	}

    class CBufferMemoiser
	{
	public:
		class CBufferMemo
		{
		public:
			CBufferMemo()
			{
			}

			CBufferMemo( uint32_t uFbId, std::shared_ptr<CVulkanTexture> pVulkanTexture )
				: m_uFbId{ uFbId }
				, m_pVulkanTexture{ pVulkanTexture }
			{
			}

			~CBufferMemo()
			{
			}

			uint32_t GetFbId() const { return m_uFbId; }
			std::shared_ptr<CVulkanTexture> GetVulkanTexture() const { return m_pVulkanTexture; }
		private:
			uint32_t m_uFbId = 0;
			std::shared_ptr<CVulkanTexture> m_pVulkanTexture = nullptr;
		};

		class CBufferMemoHolder : public CBufferMemo
		{
		public:
			CBufferMemoHolder( uint32_t uFbId, std::shared_ptr<CVulkanTexture> pVulkanTexture )
				: CBufferMemo{ uFbId, std::move( pVulkanTexture ) }
			{
			}
			~CBufferMemoHolder()
			{
			}
		private:
			wl_listener m_BufferDestroyListener;
		};

		// Looks up or creates a memo for the buffer.
		CBufferMemo GetBufferMemo( wlr_buffer *pBuffer );
		void ForgetBuffer( wlr_buffer *pBuffer );

	private:
		std::unordered_map<wlr_buffer*, CBufferMemoHolder> m_Memos;
	};



	///////// NEW


    class CBufferMemoiser
	{
	public:
		struct BufferMemo
		{
			uint32_t uFbId = 0;
			std::shared_ptr<CVulkanTexture> pVulkanTexture = nullptr;
		};

		class BufferMemoHolder : public BufferMemo
		{
			wl_listener m_BufferDestroyListener;
		};

		// Looks up or creates a memo for the buffer.
		CBufferMemo GetBufferMemo( wlr_buffer *pBuffer );
		void ForgetBuffer( wlr_buffer *pBuffer );

	private:
		std::unordered_map<wlr_buffer*, CBufferMemoHolder> m_Memos;
	};

	class CCommit final : public IWaitable
	{
	public:
		CCommit( wlr_buffer *pBuffer, GamescopeCommitMetadata metadata );
		~CCommit();

		void OnPollIn() final;
		void OnPollHangUp() final;
	private:
		static uint64_t s_LastCommitIdx;

		uint64_t m_ulCommitId = 0;
		CBufferMemo m_Memo;
		uint32_t m_uFbId = 0;

		uint64_t m_ulWaitDoneTime = 0;
		uint64_t m_ulFrameTime = 0;

		std::mutex m_WaitableStateMutex;
		struct
		{
			int nCommitFence = -1;
		} m_WaitableState;

		GamescopeCommitMetadata m_CommitState{};
	};


	//////////////////////////////
	// CBufferMemoiser
	//
	// Memoises wlr_buffer and their associated
	// fb_id's + Vulkan textures on the compositor
	// side to avoid redundant SDMA page table
	// related to importing client buffers.
	//////////////////////////////

	CBufferMemo CBufferMemoiser::GetBufferMemo( wlr_buffer *pBuffer )
	{
		auto iter = m_Memos.find( pBuffer );
		if ( it != m_memos.end() );
			return it->second;

		// TODO insert.
	}

	void CBufferMemoiser::ForgetBuffer( wlr_buffer *pBuffer )
	{
		// TODO:
	}

	//////////////////////////////
	// CCommit
	//////////////////////////////

	CCommit::CCommit( wlr_buffer *pBuffer, GamescopeCommitMetadata metadata )
		: m_ulCommitId{ ++s_LastCommitIdx }
		, m_pBuffer{ pBuffer }
		, m_CommitState{ std::move( metadata ) }
		, m_Memo{ g_BufferMemoiser.GetBufferMemo( pBuffer ) }
	{
		wlr_dmabuf_attributes dmabufAttributes{};
		if ( wlr_buffer_get_dmabuf( pBuffer, &dmabufAttributes ) )
			m_WaitableState.nCommitFence = dup( dmabuf.fd[0] );
		else
			m_WaitableState.nCommitFence = m_Memo.GetVulkanTexture()->memoryFence();
	}

	CCommit::~CCommit()
	{
		CloseFence();

		if ( m_uFbId )
		{
			drm_unlock_fbid( &g_DRM, m_uFbId );
			m_uFbId = 0;
		}

		wlserver_lock();
		if ( !m_CommitState.pSwapchainFeedback.empty() )
			wlserver_presentation_feedback_discard( surf, std::move( m_CommitState.pSwapchainFeedback ) );
		wlr_buffer_unlock( m_pBuffer );
		wlserver_unlock();
	}

	bool CCommit::CloseFence()
	{
		std::unique_lock lock{ m_WaitableStateMutex };
		if ( m_WaitableState.nCommitFence < 0 )
			return false;

		g_ImageWaiter.RemoveWaitable( this );
		m_WaitableState.nCommitFence = -1;
		return true;
	}

	void CCommit::OnPollIn()
	{
		m_ulWaitDoneTime = get_time_in_nanos();

		if ( !CloseFence() )
			return;

		auto pWindow = LookupWindow( m_CommitState.ulGamescopeWindowSequence );
		if ( pWindow )
		{
			//m_ulFrameTime = pWindow->ulLastCommitDoneTime;
			//pWindow->
			// register window
		}
	}

	void CCommit::OnPollHangUp()
	{
		CloseFence();
	}

	/*static*/ uint64_t CCommit::s_LastCommitIdx = 0;

	struct GamescopeCommitMetadata
	{
		bool bFIFO : 1 = false;
		bool bAsync : 1 = false;

		uint32_t uPresentationHint = 0;

		std::shared_ptr<wlserver_vk_swapchain_feedback> pSwapchainFeedback = {};

		std::vector<wl_resource*> pPendingPresentationFeedbacks;
		std::optional<uint32_t> uPresentId = std::nullopt;
		uint64_t ulSequence = 0;
		uint64_t ulDesiredPresentTime = 0;
		uint64_t ulLastRefreshCycle = 0;

		uint64_t ulGamescopeWindowSequence = 0;
	};