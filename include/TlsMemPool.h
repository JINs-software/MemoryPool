#pragma once
#include <Windows.h>
#include <map>
#include <unordered_map>
#include <mutex>

#define MEMORY_USAGE_TRACKING
#if defined(MEMORY_USAGE_TRACKING)
struct stMemAllocInfo {
	size_t tlsMemPoolUnitCnt = 0;
	size_t lfMemPoolFreeCnt = 0;
	size_t mallocCnt = 0;
};
#endif

#define DEFAULT_MEM_POOL_SIZE	20000
#define DEFAULT_SURPLUS_SIZE	20000

template<typename T>
class TlsMemPoolManager;

template<typename T>
class TlsMemPool {
	template<typename T>
	friend class TlsMemPoolManager;
private:	// private ������ -> ������ ������ ���´�. 
	// placementNew == true, Alloc / Free �� placement_new, ~() �Ҹ��� ȣ��
	// placementNew == false, �޸� Ǯ������ �����ڱ��� ȣ��� ��ü�κ��� ������ ���۵Ǿ�� �� (240417 ����)
	TlsMemPool(size_t unitCnt, size_t capacity, bool placementNew = false);
	~TlsMemPool();

public:
	T* AllocMem();
	void FreeMem(T* address);

private:
	TlsMemPoolManager<T>* m_MemPoolMgr;
	PBYTE	m_FreeFront;
	size_t	m_UnitCnt;
	size_t	m_Capacity;
	bool	m_PlacementNewFlag;
};

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
	TlsMemPoolManager(size_t defaultMemPoolUnitCnt, size_t surplusListSize);

	DWORD AllocTlsMemPool(size_t memPoolUnitCnt = 0);
	inline DWORD GetTlsMemPoolIdx() { return m_TlsIMainIndex; }
	inline TlsMemPool<T>& GetTlsMemPool() { return *reinterpret_cast<TlsMemPool<T>*>(TlsGetValue(m_TlsIMainIndex)); }

	void Alloc();
	void Free(T* address);

private:
	DWORD m_TlsIMainIndex;
	DWORD m_TlsSurpIndex;
	DWORD m_TlsMallocCnt;
	size_t m_DefaultMemPoolUnitCnt;
	size_t m_SurplusListSize;

	std::map<DWORD, LockFreeMemPool*> m_ThMemPoolMap;
	std::mutex m_ThMemPoolMapMtx;

#if defined(MEMORY_USAGE_TRACKING)
public:
	std::unordered_map<DWORD, stMemAllocInfo> thMemInfo;
	void ResetMemInfo(size_t tlsMemPoolUnit) {
		DWORD thID = GetThreadId(GetCurrentThread());
		LockFreeMemPool* lfMemPool = reinterpret_cast<LockFreeMemPool*>(TlsGetValue(m_TlsSurpIndex));
		thMemInfo[thID].tlsMemPoolUnitCnt = tlsMemPoolUnit;
		thMemInfo[thID].lfMemPoolFreeCnt = lfMemPool->GetFreeCnt();
		thMemInfo[thID].mallocCnt = *(size_t*)TlsGetValue(m_TlsMallocCnt);
	}
	std::unordered_map<DWORD, stMemAllocInfo> GetMemInfo() {
		return thMemInfo;
	}
#endif
};

////////////////////////////////////////////////////////////////////////////////
// TlsMemPool
////////////////////////////////////////////////////////////////////////////////
template<typename T>
TlsMemPool<T>::TlsMemPool(size_t unitCnt, size_t capacity, bool placementNew) 
	: m_FreeFront(NULL), m_UnitCnt(unitCnt), m_Capacity(capacity), m_PlacementNewFlag(placementNew)
{
	if (m_Capacity > 0) {
		// ���� �Ҵ� �Լ��� m_UnitCnt * (ũ��) ��ŭ�� ûũ�� �Ҵ�޴� ������ ĳ�� ȿ���� �� ������ ���ؼ�...
		m_FreeFront = (PBYTE)calloc(m_UnitCnt, sizeof(T) + sizeof(UINT_PTR));
		if (m_FreeFront == NULL) {
			DebugBreak();
		}
		PBYTE ptr = m_FreeFront;
		for (size_t idx = 0; idx < m_UnitCnt - 1; idx++) {
			if (m_PlacementNewFlag == false) {
				T* tptr = reinterpret_cast<T*>(ptr);
				new (tptr) T;
			}
			ptr += sizeof(T);
			*(reinterpret_cast<PUINT_PTR>(ptr)) = reinterpret_cast<UINT_PTR>(ptr + sizeof(UINT_PTR));
			ptr += sizeof(UINT_PTR);
		}
	}
}
template<typename T>
TlsMemPool<T>::~TlsMemPool() {
	if (m_PlacementNewFlag == false) {
		// �ʱ� ������ ȣ�� ��Ŀ����� �޸� Ǯ ��ü�� �Ҹ��ڰ� ȣ��� �� ���� ��ü���� �Ҹ��ڸ� ȣ��
		while (m_FreeFront != NULL) {
			reinterpret_cast<T*>(m_FreeFront)->~T();
		}
	}
}

template<typename T>
T* TlsMemPool<T>::AllocMem() {
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
	m_MemPoolMgr->ResetMemInfo(m_UnitCnt);
#endif

	T* ret = reinterpret_cast<T*>(ptr);
	if (m_PlacementNewFlag) {
		new (ret) T;
	}

	return ret;
}

template<typename T>
void TlsMemPool<T>::FreeMem(T* address) {
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
	m_MemPoolMgr->ResetMemInfo(m_UnitCnt);
#endif
}


////////////////////////////////////////////////////////////////////////////////
// TlsMemPoolManager
////////////////////////////////////////////////////////////////////////////////
template<typename T>
TlsMemPoolManager<T>::TlsMemPoolManager()
	: TlsMemPoolManager(DEFAULT_MEM_POOL_SIZE, DEFAULT_SURPLUS_SIZE)
{
	//TlsMemPoolManager(DEFAULT_MEM_POOL_SIZE, DEFAULT_SURPLUS_SIZE);
	// => "������ ����"�� ���� ������ ����� 0���� �ʱ�ȭ�ȴ�(?)
}
template<typename T>
TlsMemPoolManager<T>::TlsMemPoolManager(size_t defaultMemPoolUnitCnt, size_t surplusListSize)
	: m_DefaultMemPoolUnitCnt(defaultMemPoolUnitCnt), m_SurplusListSize(surplusListSize)
{
	m_TlsIMainIndex = TlsAlloc();
	m_TlsSurpIndex = TlsAlloc();
	m_TlsMallocCnt = TlsAlloc();
}

template<typename T>
DWORD TlsMemPoolManager<T>::AllocTlsMemPool(size_t memPoolUnitCnt) {
	if (TlsGetValue(m_TlsIMainIndex) == NULL) {
		// TlsMemPool ����
		TlsMemPool<T>* newTlsMemPool;// = new TlsMemPool<T>();
		if (memPoolUnitCnt == 0) {
			newTlsMemPool = new TlsMemPool<T>(m_DefaultMemPoolUnitCnt, m_DefaultMemPoolUnitCnt);
		}
		else {
			newTlsMemPool = new TlsMemPool<T>(memPoolUnitCnt, memPoolUnitCnt);
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
		tlsMemPool->m_FreeFront = reinterpret_cast<PBYTE>(lfMemPool->AllocLFM(tlsMemPool->m_UnitCnt));
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
				tlsMemPool->m_FreeFront = reinterpret_cast<PBYTE>(maxFreeCntPool->AllocLFM(tlsMemPool->m_UnitCnt));
			}
			else {
				T* newAlloc = reinterpret_cast<T*>(malloc(sizeof(T) + sizeof(UINT_PTR)));
				tlsMemPool->FreeMem(newAlloc);

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
