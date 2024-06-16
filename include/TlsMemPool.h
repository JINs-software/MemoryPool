#pragma once
#include <Windows.h>
#include <map>
#include <unordered_map>
#include <mutex>

//#define MEMORY_USAGE_TRACKING
#define	MEMORY_POOL_ALLOC_FREE_TRACKING
//struct stMemoryPoolUseInfo {
//	size_t	tlsMemPoolUnitCnt = 0;
//	size_t	tlsMemPoolAllocCnt = 0;
//	size_t	tlsMemPoolFreeCnt = 0;
//	size_t	tlsInjectMemCnt = 0;
//};
//=> �����÷ο� ���� �߻�, ī��Ʈ ������ ���� �Ǵ� �޸� Ǯ �����ڰ� ���� �����Ѵٸ�, �����÷ο찡 �߻��Ͽ���, Alloc/Free Cnt�� ������ ��

template<typename T>
class TlsMemPoolManager;

////////////////////////////////////////////////////////////////////////////////
// TlsMemPool
////////////////////////////////////////////////////////////////////////////////
template<typename T>
struct stMemPoolNode {
	T unit;
	alignas(16) stMemPoolNode* next;
};

template<typename T>
class TlsMemPool {
	template<typename T>
	friend class TlsMemPoolManager;
private:	// private ������ -> ������ ������ ���´�. 
	// placementNew == true, Alloc / Free �� placement_new, ~() �Ҹ��� ȣ��
	// placementNew == false, �޸� Ǯ������ �����ڱ��� ȣ��� ��ü�κ��� ������ ���۵Ǿ�� �� (240417 ����)
	template<typename... Args>
	TlsMemPool(size_t unitCnt, size_t capacity, bool referenceFlag = false, bool placementNew = false, Args... args);	// �������ڸ� ��ü ������ �μ��� ����
	
	// TO DO: ��ü new(placementNew) ȣ�� �� ������ �� �ִ� ���� ���� ��Ÿ�Ϸ� ..
	//TlsMemPool(size_t unitCnt, size_t capacity, bool referenceFlag = false, bool placementNew = false, UINT arg);
	~TlsMemPool();

public:
	template<typename... Args>
	T* AllocMem(SHORT refCnt = 1, Args... args);
	void FreeMem(T* address);
	void IncrementRefCnt(T* address, USHORT refCnt = 1);

	inline size_t GetMemPoolCapacity() {
		return sizeof(stMemPoolNode<T>) * m_UnitCapacity;
	}
	inline size_t GetMemPoolSize() {
		return sizeof(stMemPoolNode<T>) * m_UnitCount;
	}

private:
	template<typename... Args>
	void InjectNewMem(T* address, Args... args);

private:
	TlsMemPoolManager<T>* m_MemPoolMgr;
	stMemPoolNode<T>* m_FreeFront;
	size_t	m_UnitCount;
	size_t	m_UnitCapacity;
	bool	m_PlacementNewFlag;
	bool	m_ReferenceFlag;

	DWORD	m_ThreadID;
};

template<typename T>
template<typename... Args>
TlsMemPool<T>::TlsMemPool(size_t unitCnt, size_t capacity, bool referenceFlag,  bool placementNew, Args... args)
	: m_MemPoolMgr(NULL), m_FreeFront(NULL), m_UnitCount(unitCnt), m_UnitCapacity(capacity), m_ReferenceFlag(referenceFlag), m_PlacementNewFlag(placementNew)
{
	m_ThreadID = GetThreadId(GetCurrentThread());

	if (m_UnitCount > 0) {
		m_FreeFront = (stMemPoolNode<T>*)calloc(m_UnitCount, sizeof(stMemPoolNode<T>));
		if (m_FreeFront == NULL) {
			DebugBreak();
		}

		stMemPoolNode<T>* nodePtr = (stMemPoolNode<T>*)(m_FreeFront);
		for (size_t idx = 0; idx < m_UnitCount; idx++) {
			if (!m_PlacementNewFlag) {
				T* tptr = reinterpret_cast<T*>(nodePtr);
				new (tptr) T(args...);
			}

			nodePtr->next = nodePtr + 1;
			nodePtr += 1;
		}
		nodePtr -= 1;
		nodePtr->next = NULL;	// �� ������ ������ NULL
	}
}

template<typename T>
TlsMemPool<T>::~TlsMemPool() {
	if (m_PlacementNewFlag == false) {
		// �ʱ� ������ ȣ�� ��Ŀ����� �޸� Ǯ ��ü�� �Ҹ��ڰ� ȣ��� �� ���� ��ü���� �Ҹ��ڸ� ȣ��
		while (m_FreeFront != NULL) {
			reinterpret_cast<T*>(m_FreeFront)->~T();
			m_FreeFront = m_FreeFront->next;
		}
	}
}

template<typename T>
template<typename... Args>
T* TlsMemPool<T>::AllocMem(SHORT refCnt, Args... args) {
	stMemPoolNode<T>* node = NULL;

	if (m_FreeFront == NULL) {
		// �Ҵ� ���� ����..
		if (m_MemPoolMgr != NULL) {
			// �޸� Ǯ �����ڿ��� �Ҵ��� ��û�Ѵ�.
			m_MemPoolMgr->Alloc(args...);
		}
		else {
			T* newAlloc = reinterpret_cast<T*>(malloc(sizeof(stMemPoolNode<T>)));
			InjectNewMem(newAlloc, args...);
		}
	}

	node = m_FreeFront;
	if (node != NULL) {
		m_FreeFront = m_FreeFront->next;
		if (m_UnitCount == 0) {
			DebugBreak();
		}
		m_UnitCount--;
	}

	if (m_ReferenceFlag) {
		SHORT* refCntPtr = reinterpret_cast<SHORT*>(reinterpret_cast<PBYTE>(&node->next) + sizeof(stMemPoolNode<T>*));
		refCntPtr -= 1;
		*refCntPtr = refCnt;
	}

	T* ret = reinterpret_cast<T*>(node);
	if (m_PlacementNewFlag) {
		new (ret) T(args...);
	}

	m_MemPoolMgr->ResetMemPoolUsageCount(true);

	return ret;
}

template<typename T>

void TlsMemPool<T>::FreeMem(T * address) {
	stMemPoolNode<T>* node = reinterpret_cast<stMemPoolNode<T>*>(address);

	if (m_ReferenceFlag) {
		SHORT* refCntPtr = reinterpret_cast<SHORT*>(reinterpret_cast<PBYTE>(&node->next) + sizeof(stMemPoolNode<T>*));
		refCntPtr -= 1;
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

	if (m_UnitCount < m_UnitCapacity) {
		node->next = m_FreeFront;
		m_FreeFront = node;
		m_UnitCount++;
	}
	else {
		m_MemPoolMgr->Free(address);
	}

	m_MemPoolMgr->ResetMemPoolUsageCount(false);
}

template<typename T>
template<typename... Args>
inline void TlsMemPool<T>::InjectNewMem(T* address, Args... args)
{
	if (!m_PlacementNewFlag) {
		new (address) T(args...);
	}

	if (m_UnitCount < m_UnitCapacity) {
		stMemPoolNode<T>* node = reinterpret_cast<stMemPoolNode<T>*>(address);
		node->next = m_FreeFront;
		m_FreeFront = node;
		m_UnitCount++;
	}
	else {
		m_MemPoolMgr->Free(address);
	}
}

template<typename T>
inline void TlsMemPool<T>::IncrementRefCnt(T * address, USHORT refCnt) {

	if (m_ReferenceFlag) {
		stMemPoolNode<T>* node = reinterpret_cast<stMemPoolNode<T>*>(address);
		for (USHORT i = 0; i < refCnt; i++) {
			SHORT* refCntPtr = reinterpret_cast<SHORT*>(reinterpret_cast<PBYTE>(&node->next) + sizeof(stMemPoolNode<T>*));
			refCntPtr -= 1;
			SHORT refCntResult = InterlockedIncrement16(refCntPtr);

#if defined(MEMORY_USAGE_TRACKING)
			InterlockedIncrement64((int64*)&m_MemPoolMgr->totalIncrementRefCnt);
#endif
		}
	}
	else {
		DebugBreak();
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
	TlsMemPoolManager() : TlsMemPoolManager(0, 0) {}
	TlsMemPoolManager(size_t defaultMemUnitCnt = 0, size_t defaultMemUnitCapacity = 0, bool memUnitReferenceFlag = false, bool memUnitPlacementNewFlag = false)
		: m_DefaultMemUnitCnt(defaultMemUnitCnt), m_DefaultMemUnitCapacity(defaultMemUnitCapacity), m_MemUnitReferenceFlag(memUnitReferenceFlag), m_MemUnitPlacementNewFlag(memUnitPlacementNewFlag)
	{
		m_TlsIMainIndex = TlsAlloc();
		m_TlsSurpIndex = TlsAlloc();
	}

	template<typename... Args>
	DWORD AllocTlsMemPool(size_t memUnitCnt = 0, size_t memUnitCapacity = 0, Args... args);

	inline DWORD GetTlsMemPoolIdx() { return m_TlsIMainIndex; }

	inline TlsMemPool<T>& GetTlsMemPool() { return *reinterpret_cast<TlsMemPool<T>*>(TlsGetValue(m_TlsIMainIndex)); }

	template<typename... Args>
	void Alloc(Args... args);

	void Free(T* address);

private:
	DWORD	m_TlsIMainIndex;
	DWORD	m_TlsSurpIndex;
	size_t	m_DefaultMemUnitCnt;
	size_t	m_DefaultMemUnitCapacity;
	bool	m_MemUnitReferenceFlag;
	bool	m_MemUnitPlacementNewFlag;

	std::map<DWORD, LockFreeMemPool*> m_ThMemPoolMap;
	std::mutex m_ThMemPoolMapMtx;

#if defined	MEMORY_POOL_ALLOC_FREE_TRACKING
public:
	//size_t totalAllocMemCnt = 0;
	//size_t totalFreeMemCnt = 0;
	//size_t totalInjectedMemCnt = 0;
	//
	//size_t allocatedMemUnitCnt = 0;
	//
	//inline size_t GetTotalAllocMemCnt() {
	//	totalAllocMemCnt = 0;
	//
	//	AcquireSRWLockShared(&m_ThreadMemInfoSrwLock);
	//	for (auto iter = m_ThreadMemInfo.begin(); iter != m_ThreadMemInfo.end(); iter++) {
	//		totalAllocMemCnt += iter->second.tlsMemPoolAllocCnt;
	//	}
	//	ReleaseSRWLockShared(&m_ThreadMemInfoSrwLock);
	//
	//	return totalAllocMemCnt;
	//}
	//inline size_t GetTotalFreeMemCnt() {
	//	totalFreeMemCnt = 0;
	//
	//	AcquireSRWLockShared(&m_ThreadMemInfoSrwLock);
	//	for (auto iter = m_ThreadMemInfo.begin(); iter != m_ThreadMemInfo.end(); iter++) {
	//		totalFreeMemCnt += iter->second.tlsMemPoolFreeCnt;
	//	}
	//	ReleaseSRWLockShared(&m_ThreadMemInfoSrwLock);
	//
	//	return totalFreeMemCnt;
	//}
	//inline size_t GetTotalInjectedMemCnt() {
	//	totalInjectedMemCnt = 0;
	//
	//	AcquireSRWLockShared(&m_ThreadMemInfoSrwLock);
	//	for (auto iter = m_ThreadMemInfo.begin(); iter != m_ThreadMemInfo.end(); iter++) {
	//		totalInjectedMemCnt += iter->second.tlsInjectMemCnt;
	//	}
	//	ReleaseSRWLockShared(&m_ThreadMemInfoSrwLock);
	//
	//	return totalInjectedMemCnt;
	//}
	//inline size_t GetAllocatedMemUnitCnt() {
	//	size_t allocMemCnt = GetTotalAllocMemCnt();
	//	size_t freeMemCnt = GetTotalFreeMemCnt();
	//	if (allocMemCnt < freeMemCnt) {
	//		DebugBreak();
	//	}
	//	return allocMemCnt - freeMemCnt;
	//}
	//
	//// ������ �� �޸� ����
	//std::unordered_map<DWORD, stMemoryPoolUseInfo>  m_ThreadMemInfo;
	//SRWLOCK											m_ThreadMemInfoSrwLock;
	//void ResetMemInfo(DWORD thID, size_t tlsMemPoolUnitCnt, size_t tlsMemPoolAllocCnt, size_t tlsMemPoolFreeCnt, size_t tlsInjectMemCnt) {
	//	AcquireSRWLockShared(&m_ThreadMemInfoSrwLock);
	//	m_ThreadMemInfo[thID].tlsMemPoolUnitCnt = tlsMemPoolUnitCnt;
	//	m_ThreadMemInfo[thID].tlsMemPoolAllocCnt = tlsMemPoolAllocCnt;
	//	m_ThreadMemInfo[thID].tlsMemPoolFreeCnt = tlsMemPoolFreeCnt;
	//	m_ThreadMemInfo[thID].tlsInjectMemCnt = tlsInjectMemCnt;
	//	ReleaseSRWLockShared(&m_ThreadMemInfoSrwLock);
	//}

	// => �����÷ο� ���� �߻�

	UINT64 m_TotalAllocMemCount = 0;
	UINT64 m_TotalFreeMemCount = 0;
	INT64 m_AllocatedMemUnitCount = 0;
	UINT64 m_MallocCount = 0;

	inline void ResetMemPoolUsageCount(bool allocFlag) {
		if (allocFlag) {
			InterlockedIncrement64(&m_AllocatedMemUnitCount);
			InterlockedIncrement64((INT64*)&m_TotalAllocMemCount);
		}
		else {
			InterlockedDecrement64(&m_AllocatedMemUnitCount);
			InterlockedIncrement64((INT64*)&m_TotalFreeMemCount);
		}
	}

	inline UINT64 GetTotalAllocMemCnt() {
		return m_TotalAllocMemCount;
	}
	inline UINT64 GetTotalFreeMemCnt() {
		return m_TotalFreeMemCount;
	}
	inline INT64 GetAllocatedMemUnitCnt() {
		if (m_AllocatedMemUnitCount < 0) {
			DebugBreak();
		}
		return m_AllocatedMemUnitCount;
	}
	inline UINT64 GetMallocCount() {
		return m_MallocCount;
	}

#elif defined(MEMORY_USAGE_TRACKING)
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
	inline size_t GetAllocatedMemUnitCnt() {
		return allocatedMemUnitCnt;
	}
	inline size_t GetTotalIncrementRefCnt() {
		return totalIncrementRefCnt;
	}
	inline size_t GetTotalDecrementRefCnt() {
		return totalDecrementRefCnt;
	}

	// ������ �� �޸� ����
	std::unordered_map<DWORD, stMemoryPoolUseInfo> thMemInfo;
	void ResetMemInfo(size_t tlsMemPoolUnit, size_t mallocCnt) {
		DWORD thID = GetThreadId(GetCurrentThread());
		LockFreeMemPool* lfMemPool = reinterpret_cast<LockFreeMemPool*>(TlsGetValue(m_TlsSurpIndex));
		thMemInfo[thID].tlsMemPoolUnitCnt = tlsMemPoolUnit;
		thMemInfo[thID].lfMemPoolFreeCnt = lfMemPool->GetFreeCnt();
		//thMemInfo[thID].mallocCnt = *(size_t*)TlsGetValue(m_TlsMallocCnt);
		thMemInfo[thID].mallocCnt = mallocCnt;
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
template<typename... Args>
inline DWORD TlsMemPoolManager<T>::AllocTlsMemPool(size_t memUnitCnt, size_t memUnitCapacity, Args... args)
{
	if (TlsGetValue(m_TlsIMainIndex) == NULL) {
		if (memUnitCnt == 0) {
			memUnitCnt = m_DefaultMemUnitCnt;
		}
		if (memUnitCapacity == 0) {
			memUnitCapacity = m_DefaultMemUnitCapacity;
		}

		// TlsMemPool ����
		TlsMemPool<T>* newTlsMemPool = new TlsMemPool<T>(memUnitCnt, memUnitCapacity, m_MemUnitReferenceFlag, m_MemUnitPlacementNewFlag, args...);
		if (newTlsMemPool == NULL) {
			DebugBreak();
		}
		newTlsMemPool->m_MemPoolMgr = this;
		TlsSetValue(m_TlsIMainIndex, newTlsMemPool);

		// LockFreeMemPool ����
		LockFreeMemPool* newLockFreeMemPool = new LockFreeMemPool();
		TlsSetValue(m_TlsSurpIndex, newLockFreeMemPool);

		DWORD thID = GetThreadId(GetCurrentThread());
		
		// ������ �� �޸� Ǯ �� ����
		m_ThMemPoolMapMtx.lock();
		m_ThMemPoolMap.insert({ thID , newLockFreeMemPool });
		m_ThMemPoolMapMtx.unlock();

#if defined(MEMORY_USAGE_TRACKING)
		ResetMemInfo(memUnitCnt, 0);
#endif
	}

	return m_TlsIMainIndex;
}

template<typename T>
template<typename... Args>
void TlsMemPoolManager<T>::Alloc(Args... args)
{
	TlsMemPool<T>* tlsMemPool = reinterpret_cast<TlsMemPool<T>*>(TlsGetValue(m_TlsIMainIndex));
	LockFreeMemPool* lfMemPool = reinterpret_cast<LockFreeMemPool*>(TlsGetValue(m_TlsSurpIndex));

	// �޸� Ǯ ������ �������� �����帶�� �����ϴ� ��-���� �޸� Ǯ�� ���� �޸𸮰� �ִ��� Ȯ���Ѵ�.
	// ���� ��-���� �޸� Ǯ�� ���� �޸𸮰� �ִٸ� �� freeFront�� swap �Ѵ�.
	if (lfMemPool->GetFreeCnt() > 0) {
		tlsMemPool->m_FreeFront = reinterpret_cast<stMemPoolNode<T>*>(lfMemPool->AllocLFM(tlsMemPool->m_UnitCount));
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
			DWORD exgTargetThID;
			size_t maxCnt = 0;
			{
				std::lock_guard<std::mutex> lockGuard(m_ThMemPoolMapMtx);

				typename std::map<DWORD, LockFreeMemPool*>::iterator iter = m_ThMemPoolMap.begin();
				for (; iter != m_ThMemPoolMap.end(); iter++) {
					LockFreeMemPool* lfmp = iter->second;
					if (lfmp->GetFreeCnt() > maxCnt) {
						maxFreeCntPool = lfmp;
						exgTargetThID = iter->first;
					}
				}
			}

			if (maxFreeCntPool != NULL) {
				tlsMemPool->m_FreeFront = reinterpret_cast<stMemPoolNode<T>*>(maxFreeCntPool->AllocLFM(tlsMemPool->m_UnitCount));

#if defined(MEMORY_USAGE_TRACKING)
				//m_ThMemPoolMap
				thMemInfo[exgTargetThID].lfMemPoolFreeCnt = maxFreeCntPool->GetFreeCnt();
#endif
			}
			else {
				T* newAlloc = reinterpret_cast<T*>(malloc(sizeof(stMemPoolNode<T>)));
				tlsMemPool->InjectNewMem(newAlloc, args...);

				InterlockedIncrement64((INT64*)&m_MallocCount);
			}
		} while (tlsMemPool->m_FreeFront == NULL);
	}
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
