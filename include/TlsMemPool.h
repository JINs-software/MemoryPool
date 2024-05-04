#pragma once
#include <Windows.h>
#include <map>
#include <unordered_map>
#include <mutex>

#define MEMORY_USAGE_TRACKING
#if defined(MEMORY_USAGE_TRACKING)
struct stMemoryPoolUseInfo {
	size_t tlsMemPoolUnitCnt = 0;
	size_t lfMemPoolFreeCnt = 0;
	size_t mallocCnt = 0;
};
#endif

#define ALLOC_MEM_LOG
#if defined(ALLOC_MEM_LOG)
struct stAllocMemLog {
	UINT_PTR address = 0;
	short refCnt = 0;
	std::string log = "";
};
#endif


#define DEFAULT_MEM_POOL_SIZE	20000

template<typename T>
class TlsMemPoolManager;


#define MEM_POOL_NODE

////////////////////////////////////////////////////////////////////////////////
// TlsMemPool
////////////////////////////////////////////////////////////////////////////////
#if defined(MEM_POOL_NODE)
template<typename T>
struct stMemPoolNode {
	T unit;
	alignas(16) stMemPoolNode* next;
};
#endif

template<typename T>
class TlsMemPool {
	template<typename T>
	friend class TlsMemPoolManager;
private:	// private ������ -> ������ ������ ���´�. 
	// placementNew == true, Alloc / Free �� placement_new, ~() �Ҹ��� ȣ��
	// placementNew == false, �޸� Ǯ������ �����ڱ��� ȣ��� ��ü�κ��� ������ ���۵Ǿ�� �� (240417 ����)
	TlsMemPool(size_t unitCnt, size_t capacity, bool referenceFlag = false, bool placementNew = false);
	
	// TO DO: ��ü new(placementNew) ȣ�� �� ������ �� �ִ� ���� ���� ��Ÿ�Ϸ� ..
	//TlsMemPool(size_t unitCnt, size_t capacity, bool referenceFlag = false, bool placementNew = false, UINT arg);

	~TlsMemPool();

public:
	T* AllocMem(SHORT refCnt = 1, std::string log = "");
	void FreeMem(T* address, std::string log = "");
	void FreeMemNew(T* address);
	void IncrementRefCnt(T* address, USHORT refCnt = 1, std::string log = "");

private:
	TlsMemPoolManager<T>* m_MemPoolMgr;
#if defined(MEM_POOL_NODE)
	stMemPoolNode<T>* m_FreeFront;
#else
	PBYTE	m_FreeFront;
#endif
	size_t	m_UnitCnt;
	size_t	m_Capacity;
	bool	m_PlacementNewFlag;
	bool	m_ReferenceFlag;
};

template<typename T>
TlsMemPool<T>::TlsMemPool(size_t unitCnt, size_t capacity, bool referenceFlag,  bool placementNew)
	: m_FreeFront(NULL), m_UnitCnt(unitCnt), m_Capacity(capacity), m_ReferenceFlag(referenceFlag), m_PlacementNewFlag(placementNew)
{
	if (m_Capacity > 0) {
#if defined(MEM_POOL_NODE)
		m_FreeFront = (stMemPoolNode<T>*)calloc(m_UnitCnt, sizeof(stMemPoolNode<T>));
		if (m_FreeFront == NULL) {
			DebugBreak();
		}

		stMemPoolNode<T>* nodePtr = (stMemPoolNode<T>*)(m_FreeFront);
		for (size_t idx = 0; idx < m_UnitCnt; idx++) {
			if (m_PlacementNewFlag == false) {
				T* tptr = reinterpret_cast<T*>(nodePtr);
				new (tptr) T;
			}

			nodePtr->next = nodePtr + 1;
			nodePtr += 1;
		}
		nodePtr -= 1;
		nodePtr->next = NULL;	// �� ������ ������ NULL
#else
		// ���� �Ҵ� �Լ��� m_UnitCnt * (ũ��) ��ŭ�� ûũ�� �Ҵ�޴� ������ ĳ�� ȿ���� �� ������ ���ؼ�...
		m_FreeFront = (PBYTE)calloc(m_UnitCnt, sizeof(T) + sizeof(UINT_PTR));
		if (m_FreeFront == NULL) {
			DebugBreak();
		}
		PBYTE ptr = m_FreeFront;
		for (size_t idx = 0; idx < m_UnitCnt; idx++) {
			if (m_PlacementNewFlag == false) {
				T* tptr = reinterpret_cast<T*>(ptr);
				new (tptr) T;
			}
			ptr += sizeof(T);
			*(reinterpret_cast<PUINT_PTR>(ptr)) = reinterpret_cast<UINT_PTR>(ptr + sizeof(UINT_PTR));
			ptr += sizeof(UINT_PTR);
		}
		ptr -= sizeof(UINT_PTR);
		*(reinterpret_cast<PUINT_PTR>(ptr)) = NULL;	// �� ������ ������ NULL
#endif
	}
}

template<typename T>
TlsMemPool<T>::~TlsMemPool() {
	if (m_PlacementNewFlag == false) {
		// �ʱ� ������ ȣ�� ��Ŀ����� �޸� Ǯ ��ü�� �Ҹ��ڰ� ȣ��� �� ���� ��ü���� �Ҹ��ڸ� ȣ��
		while (m_FreeFront != NULL) {
			reinterpret_cast<T*>(m_FreeFront)->~T();
#if defined(MEM_POOL_NODE)
			m_FreeFront = m_FreeFront->next;
#else
			m_FreeFront = reinterpret_cast<PBYTE>(*reinterpret_cast<PUINT_PTR>(m_FreeFront + sizeof(T)));
#endif
		}
	}
}

template<typename T>
T* TlsMemPool<T>::AllocMem(SHORT refCnt, std::string log) {
#if defined(ALLOC_MEM_LOG)
	USHORT allocMemLogIdx = InterlockedIncrement16((short*)&m_MemPoolMgr->m_AllocLogIndex);
#endif

#if defined(MEM_POOL_NODE)
	stMemPoolNode<T>* node = NULL;

	if (m_FreeFront == NULL) {
		// �Ҵ� ���� ����..
		// �޸� Ǯ �����ڿ��� �Ҵ��� ��û�Ѵ�.
		m_MemPoolMgr->Alloc();
	}

	node = m_FreeFront;
	if (node != NULL) {
		m_FreeFront = m_FreeFront->next;
		if (m_UnitCnt == 0) {
			DebugBreak();
		}
		m_UnitCnt--;
	}

#if defined(MEMORY_USAGE_TRACKING)
	InterlockedIncrement64((int64*)&m_MemPoolMgr->totalAllocMemCnt);
	m_MemPoolMgr->ResetMemInfo(m_UnitCnt);
#endif

	if (m_ReferenceFlag) {
		//SHORT* refCntPtr = reinterpret_cast<SHORT*>(&node->next);
		//*refCntPtr = refCnt;

		//SHORT* refCntPtr = reinterpret_cast<SHORT*>(reinterpret_cast<PBYTE>(&node) + sizeof(T));
		SHORT* refCntPtr = reinterpret_cast<SHORT*>(reinterpret_cast<PBYTE>(&node->next) + sizeof(stMemPoolNode<T>*));
		refCntPtr -= 1;
		*refCntPtr = refCnt;
#if defined(MEMORY_USAGE_TRACKING)
		for (SHORT i = 0; i < refCnt; i++) {
			InterlockedIncrement64((int64*)&m_MemPoolMgr->totalIncrementRefCnt);
		}
#endif
	}

	T* ret = reinterpret_cast<T*>(node);
	if (m_PlacementNewFlag) {
		new (ret) T;
	}
#else
	PBYTE ptr = NULL;

	if (m_FreeFront == NULL) {
		// �Ҵ� ���� ����..
		// �޸� Ǯ �����ڿ��� �Ҵ��� ��û�Ѵ�.
		m_MemPoolMgr->Alloc();
	}

	ptr = m_FreeFront;
	if (ptr != NULL) {
		m_FreeFront = reinterpret_cast<PBYTE>(*reinterpret_cast<PUINT_PTR>(m_FreeFront + sizeof(T)));
		if (m_UnitCnt == 0) {
			DebugBreak();
		}
		m_UnitCnt--;
	}

#if defined(MEMORY_USAGE_TRACKING)
	InterlockedIncrement64((int64*)&m_MemPoolMgr->totalAllocMemCnt);
	m_MemPoolMgr->ResetMemInfo(m_UnitCnt);
#endif

	if (m_ReferenceFlag) {
		SHORT* refCntPtr = reinterpret_cast<SHORT*>(ptr + sizeof(T));
		*refCntPtr = refCnt;
#if defined(MEMORY_USAGE_TRACKING)
		for (SHORT i = 0; i < refCnt; i++) {
			InterlockedIncrement64((int64*)&m_MemPoolMgr->totalIncrementRefCnt);
		}
#endif
	}

	T* ret = reinterpret_cast<T*>(ptr);
	if (m_PlacementNewFlag) {
		new (ret) T;
	}
#endif


#if defined(ALLOC_MEM_LOG)
	m_MemPoolMgr->m_AllocLog[allocMemLogIdx].address = (UINT_PTR)ret;
	m_MemPoolMgr->m_AllocLog[allocMemLogIdx].refCnt = refCnt;
	m_MemPoolMgr->m_AllocLog[allocMemLogIdx].log = log;

	m_MemPoolMgr->m_AllocMapMtx.lock();
	if (m_MemPoolMgr->m_AllocMap.find((UINT_PTR)ret) == m_MemPoolMgr->m_AllocMap.end()) {
		m_MemPoolMgr->m_AllocMap.insert({ (UINT_PTR)ret, refCnt });
	}
	else {
		DebugBreak();
	}
	m_MemPoolMgr->m_AllocMapMtx.unlock();
#endif
	return ret;
}

template<typename T>
void TlsMemPool<T>::FreeMem(T* address, std::string log) {
#if defined(ALLOC_MEM_LOG)
	m_MemPoolMgr->m_AllocMapMtx.lock();
	if (m_MemPoolMgr->m_AllocMap.find((UINT_PTR)address) != m_MemPoolMgr->m_AllocMap.end()) {
		short refCnt = InterlockedDecrement16(&m_MemPoolMgr->m_AllocMap[(UINT_PTR)address]);
		if (refCnt == 0) {
			m_MemPoolMgr->m_AllocMap.erase((UINT_PTR)address);
		}
	}
	else {
		DebugBreak();
	}
	m_MemPoolMgr->m_AllocMapMtx.unlock();

	USHORT allocMemLogIdx = InterlockedIncrement16((short*)&m_MemPoolMgr->m_AllocLogIndex);
	m_MemPoolMgr->m_AllocLog[allocMemLogIdx].address = (UINT_PTR)address;
	m_MemPoolMgr->m_AllocLog[allocMemLogIdx].log = log;
#endif

#if defined(MEM_POOL_NODE)

	stMemPoolNode<T>* node = reinterpret_cast<stMemPoolNode<T>*>(address);

	if (m_ReferenceFlag) {
		//SHORT* refCntPtr = reinterpret_cast<SHORT*>(&node->next);
		//SHORT refCnt = InterlockedDecrement16(refCntPtr);

		SHORT* refCntPtr = reinterpret_cast<SHORT*>(reinterpret_cast<PBYTE>(&node->next) + sizeof(stMemPoolNode<T>*));
		refCntPtr -= 1;
		SHORT refCnt = InterlockedDecrement16(refCntPtr);

#if defined(ALLOC_MEM_LOG)
		m_MemPoolMgr->m_AllocLog[allocMemLogIdx].refCnt = refCnt;
#endif

#if defined(MEMORY_USAGE_TRACKING)
		InterlockedIncrement64((int64*)&m_MemPoolMgr->totalDecrementRefCnt);
#endif
		if (refCnt > 0) {
			return;
		}

		if (refCnt < 0) {
			// �ǵ����� ���� �帧
			DebugBreak();
		}
	}

	if (m_PlacementNewFlag) {
		address->~T();
	}

	if (m_UnitCnt < m_Capacity) {
		node->next = m_FreeFront;
		m_FreeFront = node;
		m_UnitCnt++;
	}
	else {
		m_MemPoolMgr->Free(address);
	}

#if defined(MEMORY_USAGE_TRACKING)
	InterlockedIncrement64((int64*)&m_MemPoolMgr->totalFreeMemCnt);
	m_MemPoolMgr->ResetMemInfo(m_UnitCnt);
#endif

#else

	if (m_ReferenceFlag) {
		SHORT* refCntPtr = reinterpret_cast<SHORT*>(address + 1);
		SHORT refCnt = InterlockedDecrement16(refCntPtr);
#if defined(MEMORY_USAGE_TRACKING)
		InterlockedIncrement64((int64*)&m_MemPoolMgr->totalDecrementRefCnt);
#endif
		if (refCnt > 0) {
			return;
		}
		
		if (refCnt < 0) {
			// �ǵ����� ���� �帧
			DebugBreak();
		}
	}

	if (m_PlacementNewFlag) {
		address->~T();
	}

	if (m_UnitCnt < m_Capacity) {
		PBYTE ptr = reinterpret_cast<PBYTE>(address);
		ptr += sizeof(T);
		*reinterpret_cast<PUINT_PTR>(ptr) = reinterpret_cast<UINT_PTR>(m_FreeFront);
		m_FreeFront = reinterpret_cast<PBYTE>(address);
		m_UnitCnt++;
	}
	else {
		m_MemPoolMgr->Free(address);
	}

#if defined(MEMORY_USAGE_TRACKING)
	InterlockedIncrement64((int64*)&m_MemPoolMgr->totalFreeMemCnt);
	m_MemPoolMgr->ResetMemInfo(m_UnitCnt);
#endif

#endif
}

template<typename T>
inline void TlsMemPool<T>::FreeMemNew(T* address)
{
	if (!m_PlacementNewFlag) {
		new (address) T;
	}

	if (m_UnitCnt < m_Capacity) {
#if defined(MEM_POOL_NODE)
		stMemPoolNode<T>* node = reinterpret_cast<stMemPoolNode<T>*>(address);
		node->next = m_FreeFront;
		m_FreeFront = node;
#else
		PBYTE ptr = reinterpret_cast<PBYTE>(address);
		ptr += sizeof(T);
		*reinterpret_cast<PUINT_PTR>(ptr) = reinterpret_cast<UINT_PTR>(m_FreeFront);
		m_FreeFront = reinterpret_cast<PBYTE>(address);
#endif
		m_UnitCnt++;
	}
	else {
		m_MemPoolMgr->Free(address);
	}

#if defined(MEMORY_USAGE_TRACKING)
	m_MemPoolMgr->ResetMemInfo(m_UnitCnt);
#endif
}

template<typename T>
inline void TlsMemPool<T>::IncrementRefCnt(T* address, USHORT refCnt, std::string log)
{
#if defined(ALLOC_MEM_LOG)
	m_MemPoolMgr->m_AllocMapMtx.lock();
	if (m_MemPoolMgr->m_AllocMap.find((UINT_PTR)address) != m_MemPoolMgr->m_AllocMap.end()) {
		for (USHORT i = 0; i < refCnt; i++) {
			InterlockedIncrement16(&m_MemPoolMgr->m_AllocMap[(UINT_PTR)address]);
		}
	}
	else {
		DebugBreak();
	}
	m_MemPoolMgr->m_AllocMapMtx.unlock();

	USHORT allocMemLogIdx = InterlockedIncrement16((short*)&m_MemPoolMgr->m_AllocLogIndex);
	m_MemPoolMgr->m_AllocLog[allocMemLogIdx].address = (UINT_PTR)address;
	m_MemPoolMgr->m_AllocLog[allocMemLogIdx].log = log;
#endif

	if (m_ReferenceFlag) {
#if defined(MEM_POOL_NODE)
		stMemPoolNode<T>* node = reinterpret_cast<stMemPoolNode<T>*>(address);
#else
		SHORT* refCntPtr = reinterpret_cast<SHORT*>(address + 1);
#endif
		for (USHORT i = 0; i < refCnt; i++) {
#if defined(MEM_POOL_NODE)
			//SHORT* refCntPtr = reinterpret_cast<SHORT*>(&node->next);
			SHORT* refCntPtr = reinterpret_cast<SHORT*>(reinterpret_cast<PBYTE>(&node->next) + sizeof(stMemPoolNode<T>*));
			refCntPtr -= 1;
			SHORT refCntResult = InterlockedIncrement16(refCntPtr);
#else
			InterlockedIncrement16((SHORT*)refCntPtr);
#endif

#if defined(ALLOC_MEM_LOG)
			m_MemPoolMgr->m_AllocLog[allocMemLogIdx].refCnt = refCntResult;
#endif

#if defined(MEMORY_USAGE_TRACKING)
			InterlockedIncrement64((int64*)&m_MemPoolMgr->totalIncrementRefCnt);
#endif
		}
	}
}



////////////////////////////////////////////////////////////////////////////////
// TlsMemPoolManager
////////////////////////////////////////////////////////////////////////////////
template<typename T>
class TlsMemPoolManager {
	class LockFreeMemPool {
		struct LockFreeNode {
			volatile UINT_PTR ptr;
			volatile UINT_PTR cnt;

		};
	public:
		LockFreeMemPool() {
			m_FreeFront.ptr = NULL;
			m_FreeFront.cnt = 0;
		}
		T* AllocLFM(size_t& allocCnt);
		void FreeLFM(T* address);
		size_t GetFreeCnt();
		void Resize(size_t resizeCnt);

	private:

		alignas(128) LockFreeNode m_FreeFront;

		short m_Increment = 0;
		const unsigned long long mask = 0x0000'FFFF'FFFF'FFFF;
	};


public:
	TlsMemPoolManager();
	TlsMemPoolManager(size_t defaultMemPoolUnitCnt, size_t defaultMemPoolCapcity, bool refCntMemPool = false, bool placementNewMemPool = false);

	//DWORD AllocTlsMemPool(size_t memPoolUnitCnt = 0);
	DWORD AllocTlsMemPool(size_t memPoolUnitCnt = 0, size_t memPoolCapacity = 0);
	inline DWORD GetTlsMemPoolIdx() { return m_TlsIMainIndex; }
	inline TlsMemPool<T>& GetTlsMemPool() { return *reinterpret_cast<TlsMemPool<T>*>(TlsGetValue(m_TlsIMainIndex)); }

	void Alloc();
	void Free(T* address);

private:
	DWORD	m_TlsIMainIndex;
	DWORD	m_TlsSurpIndex;
	DWORD	m_TlsMallocCnt;
	size_t	m_DefaultMemPoolUnitCnt;
	size_t	m_DefaultMemPoolCapacity;
	bool	m_MemPoolReferenceFlag;
	bool	m_MemPoolPlacementNewFlag;

	std::map<DWORD, LockFreeMemPool*> m_ThMemPoolMap;
	std::mutex m_ThMemPoolMapMtx;

#if defined(MEMORY_USAGE_TRACKING)
public:
	// ��ü �޸� ����
	size_t totalAllocMemCnt = 0;
	size_t totalFreeMemCnt = 0;
	size_t totalIncrementRefCnt = 0;
	size_t totalDecrementRefCnt = 0;
	inline size_t GetTotalAllocMemCnt() {
		return totalAllocMemCnt;
	}
	inline size_t GetTotalFreeMemCnt() {
		return totalFreeMemCnt;
	}
	inline size_t GetTotalIncrementRefCnt() {
		return totalIncrementRefCnt;
	}
	inline size_t GetTotalDecrementRefCnt() {
		return totalDecrementRefCnt;
	}

	// ������ �� �޸� ����
	std::unordered_map<DWORD, stMemoryPoolUseInfo> thMemInfo;
	void ResetMemInfo(size_t tlsMemPoolUnit) {
		DWORD thID = GetThreadId(GetCurrentThread());
		LockFreeMemPool* lfMemPool = reinterpret_cast<LockFreeMemPool*>(TlsGetValue(m_TlsSurpIndex));
		thMemInfo[thID].tlsMemPoolUnitCnt = tlsMemPoolUnit;
		thMemInfo[thID].lfMemPoolFreeCnt = lfMemPool->GetFreeCnt();
		thMemInfo[thID].mallocCnt = *(size_t*)TlsGetValue(m_TlsMallocCnt);
	}
	
	std::unordered_map<DWORD, stMemoryPoolUseInfo> GetMemInfo() {
		return thMemInfo;
	}

#if defined(ALLOC_MEM_LOG)
	// �޸� Alloc �α�
	std::vector<stAllocMemLog> m_AllocLog;
	USHORT m_AllocLogIndex;

	std::map<UINT_PTR, short> m_AllocMap;
	std::mutex m_AllocMapMtx;
#endif

#endif
};

template<typename T>
TlsMemPoolManager<T>::TlsMemPoolManager()
	: TlsMemPoolManager(DEFAULT_MEM_POOL_SIZE, DEFAULT_MEM_POOL_SIZE)
{
	//TlsMemPoolManager(DEFAULT_MEM_POOL_SIZE, DEFAULT_SURPLUS_SIZE);
	// => "������ ����"�� ���� ������ ����� 0���� �ʱ�ȭ�ȴ�(?)
}
template<typename T>
TlsMemPoolManager<T>::TlsMemPoolManager(size_t defaultMemPoolUnitCnt, size_t defaultMemPoolCapcity, bool refCntMemPool, bool placementNewMemPool)
	: m_DefaultMemPoolUnitCnt(defaultMemPoolUnitCnt), m_DefaultMemPoolCapacity(defaultMemPoolCapcity), m_MemPoolReferenceFlag(refCntMemPool), m_MemPoolPlacementNewFlag(placementNewMemPool)
{
	m_TlsIMainIndex = TlsAlloc();
	m_TlsSurpIndex = TlsAlloc();
	m_TlsMallocCnt = TlsAlloc();

#if defined(ALLOC_MEM_LOG)
	m_AllocLog.resize(USHRT_MAX + 1);
	m_AllocLogIndex = -1;
#endif
}

//template<typename T>
//DWORD TlsMemPoolManager<T>::AllocTlsMemPool(size_t memPoolUnitCnt) {
//	if (TlsGetValue(m_TlsIMainIndex) == NULL) {
//		// TlsMemPool ����
//		TlsMemPool<T>* newTlsMemPool;// = new TlsMemPool<T>();
//		if (memPoolUnitCnt == 0) {
//			newTlsMemPool = new TlsMemPool<T>(m_DefaultMemPoolUnitCnt, m_DefaultMemPoolUnitCnt);
//		}
//		else {
//			newTlsMemPool = new TlsMemPool<T>(memPoolUnitCnt, memPoolUnitCnt);
//		}
//		if (newTlsMemPool == NULL) {
//			DebugBreak();
//		}
//		newTlsMemPool->m_MemPoolMgr = this;
//		TlsSetValue(m_TlsIMainIndex, newTlsMemPool);
//
//		// LockFreeMemPool ����
//		LockFreeMemPool* newLockFreeMemPool = new LockFreeMemPool();
//		TlsSetValue(m_TlsSurpIndex, newLockFreeMemPool);
//
//		DWORD thID = GetThreadId(GetCurrentThread());
//		{
//			// m_ThMemPoolMap�� ��Ƽ-������ ���� ������ �߻��� �� ����
//			std::lock_guard<std::mutex> lockGuard(m_ThMemPoolMapMtx);
//			m_ThMemPoolMap.insert({ thID , newLockFreeMemPool });
//		}
//
//#if defined(MEMORY_USAGE_TRACKING)
//		TlsSetValue(m_TlsMallocCnt, new size_t(0));
//		thMemInfo.insert({ thID, { 0 } });
//#endif
//	}
//
//	return m_TlsIMainIndex;
//}

template<typename T>
inline DWORD TlsMemPoolManager<T>::AllocTlsMemPool(size_t memPoolUnitCnt, size_t memPoolCapacity)
{
	if (TlsGetValue(m_TlsIMainIndex) == NULL) {
		size_t unitCnt = 0;

		// TlsMemPool ����
		TlsMemPool<T>* newTlsMemPool = NULL;
		if (memPoolUnitCnt == 0 && memPoolCapacity == 0) {
			newTlsMemPool = new TlsMemPool<T>(m_DefaultMemPoolUnitCnt, m_DefaultMemPoolCapacity, m_MemPoolReferenceFlag, m_MemPoolPlacementNewFlag);
			unitCnt = m_DefaultMemPoolUnitCnt;
		}
		else if (memPoolUnitCnt == 0) {
			newTlsMemPool = new TlsMemPool<T>(m_DefaultMemPoolUnitCnt, memPoolCapacity, m_MemPoolReferenceFlag, m_MemPoolPlacementNewFlag);
			unitCnt = m_DefaultMemPoolUnitCnt;
		}
		else if (memPoolCapacity == 0) {
			newTlsMemPool = new TlsMemPool<T>(memPoolUnitCnt, m_DefaultMemPoolCapacity, m_MemPoolReferenceFlag, m_MemPoolPlacementNewFlag);
			unitCnt = memPoolUnitCnt;
		}
		else {
			newTlsMemPool = new TlsMemPool<T>(memPoolUnitCnt, memPoolCapacity, m_MemPoolReferenceFlag, m_MemPoolPlacementNewFlag);
			unitCnt = memPoolUnitCnt;
		}

		if (newTlsMemPool == NULL) {
			DebugBreak();
		}

		newTlsMemPool->m_MemPoolMgr = this;
		TlsSetValue(m_TlsIMainIndex, newTlsMemPool);

		// LockFreeMemPool ����
		LockFreeMemPool* newLockFreeMemPool = new LockFreeMemPool();
		TlsSetValue(m_TlsSurpIndex, newLockFreeMemPool);

		DWORD thID = GetThreadId(GetCurrentThread());
		{
			// m_ThMemPoolMap�� ��Ƽ-������ ���� ������ �߻��� �� ����
			std::lock_guard<std::mutex> lockGuard(m_ThMemPoolMapMtx);
			m_ThMemPoolMap.insert({ thID , newLockFreeMemPool });
		}

#if defined(MEMORY_USAGE_TRACKING)
		TlsSetValue(m_TlsMallocCnt, new size_t(0));	
		thMemInfo.insert({ thID, { 0 } });
#endif

#if defined(MEMORY_USAGE_TRACKING)
		ResetMemInfo(unitCnt);
#endif
	}

	return m_TlsIMainIndex;
}

template<typename T>
void TlsMemPoolManager<T>::Alloc()
{
	TlsMemPool<T>* tlsMemPool = reinterpret_cast<TlsMemPool<T>*>(TlsGetValue(m_TlsIMainIndex));
	LockFreeMemPool* lfMemPool = reinterpret_cast<LockFreeMemPool*>(TlsGetValue(m_TlsSurpIndex));

	// �޸� Ǯ ������ �������� �����帶�� �����ϴ� ��-���� �޸� Ǯ�� ���� �޸𸮰� �ִ��� Ȯ���Ѵ�.
	// ���� ��-���� �޸� Ǯ�� ���� �޸𸮰� �ִٸ� �� freeFront�� swap �Ѵ�.
	if (lfMemPool->GetFreeCnt() > 0) {
#if defined(MEM_POOL_NODE)
		tlsMemPool->m_FreeFront = reinterpret_cast<stMemPoolNode<T>*>(lfMemPool->AllocLFM(tlsMemPool->m_UnitCnt));
#else
		tlsMemPool->m_FreeFront = reinterpret_cast<PBYTE>(lfMemPool->AllocLFM(tlsMemPool->m_UnitCnt));
#endif
	}

	// NULL�̶��(�ڽ��� ��-���� ť���� �޸𸮸� ���� ���Ͽ��ٴ� ��, 
	// �ٸ� ��������� ���� ��-���� �޸� Ǯ���� ��´�.
	if (tlsMemPool->m_FreeFront != NULL) {
		return;
	}
	else {
		do {
			// ���� ũ�Ⱑ ū �޸� Ǯ ã��
			LockFreeMemPool* maxFreeCntPool = NULL;
			size_t maxCnt = 0;
			{
				std::lock_guard<std::mutex> lockGuard(m_ThMemPoolMapMtx);

				typename std::map<DWORD, LockFreeMemPool*>::iterator iter = m_ThMemPoolMap.begin();
				for (; iter != m_ThMemPoolMap.end(); iter++) {
					LockFreeMemPool* lfmp = iter->second;
					if (lfmp->GetFreeCnt() > maxCnt) {
						maxFreeCntPool = lfmp;
					}
				}
			}

			if (maxFreeCntPool != NULL) {
#if defined(MEM_POOL_NODE)
				tlsMemPool->m_FreeFront = reinterpret_cast<stMemPoolNode<T>*>(maxFreeCntPool->AllocLFM(tlsMemPool->m_UnitCnt));
#else
				tlsMemPool->m_FreeFront = reinterpret_cast<PBYTE>(maxFreeCntPool->AllocLFM(tlsMemPool->m_UnitCnt));
#endif
			}
			else {
#if defined(MEM_POOL_NODE)
				T* newAlloc = reinterpret_cast<T*>(malloc(sizeof(stMemPoolNode<T>)));
#else
				T* newAlloc = reinterpret_cast<T*>(malloc(sizeof(T) + sizeof(UINT_PTR)));
#endif
				tlsMemPool->FreeMemNew(newAlloc);

				size_t* mallocCntPtr = (size_t*)TlsGetValue(m_TlsMallocCnt);
				(*mallocCntPtr)++;
			}
		} while (tlsMemPool->m_FreeFront == NULL);
	}

	// �ٸ� ��������� SurplusFront�� ��� NULL�̶��, 
	// �Ҵ�
}

template<typename T>
void TlsMemPoolManager<T>::Free(T* address) {
	LockFreeMemPool* lfMemPool = reinterpret_cast<LockFreeMemPool*>(TlsGetValue(m_TlsSurpIndex));
	lfMemPool->FreeLFM(address);
}


template<typename T>
inline T* TlsMemPoolManager<T>::LockFreeMemPool::AllocLFM(size_t& allocCnt)
{
	T* ret = NULL;

	LockFreeNode freeFront;
	do {
		freeFront = m_FreeFront;
	} while (!InterlockedCompareExchange128(reinterpret_cast<LONG64*>(&m_FreeFront), 0, 0, reinterpret_cast<LONG64*>(&freeFront)));

	allocCnt = freeFront.cnt;
	freeFront.cnt = 0;
	ret = reinterpret_cast<T*>(freeFront.ptr & mask);
	return ret;
}

template<typename T>
inline void TlsMemPoolManager<T>::LockFreeMemPool::FreeLFM(T* address)
{
	if (address != NULL) {
		UINT_PTR increment = InterlockedIncrement16(&m_Increment);
		increment <<= (64 - 16);

		PBYTE ptr = (PBYTE)address;
		ptr += sizeof(T);

		LockFreeNode freeFront;
		do {
			freeFront = m_FreeFront;
			*reinterpret_cast<PUINT_PTR>(ptr) = static_cast<UINT_PTR>(freeFront.ptr) & mask;
		} while (!InterlockedCompareExchange128(reinterpret_cast<LONG64*>(&m_FreeFront), freeFront.cnt + 1, reinterpret_cast<UINT_PTR>(address) ^ increment, reinterpret_cast<LONG64*>(&freeFront)));
	}
}

template<typename T>
inline size_t TlsMemPoolManager<T>::LockFreeMemPool::GetFreeCnt()
{
	return m_FreeFront.cnt;
}

template<typename T>
inline void TlsMemPoolManager<T>::LockFreeMemPool::Resize(size_t resizeCnt)
{
	for (size_t cnt = 0; cnt < resizeCnt; cnt++) {
		T* newNode = malloc(sizeof(T) + sizeof(UINT_PTR));
		Free(newNode);
	}
}
