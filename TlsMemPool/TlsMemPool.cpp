#include "TlsMemPool.h"

////////////////////////////////////////////////////////////////////////////////
// TlsMemPool
////////////////////////////////////////////////////////////////////////////////
template<typename T>
PBYTE TlsMemPool<T>::AllocMem() {
	PBYTE ret = NULL;

	if (m_FreeFront == NULL) {
		// �Ҵ� ���� ����..
		m_MemPoolMgr->Alloc();
	}

	ret = m_FreeFront;
	if (ret != NULL) {
		m_FreeFront = static_cast<PBYTE>(*static_cast<PUINT_PTR>(m_FreeFront + sizeof(T)));
		if (m_UnitCnt == 0) {
			DebugBreak();
		}
		m_UnitCnt--;
	}

	return ret;
}

template<typename T>
void TlsMemPool<T>::FreeMem(T* address) {
	if (m_UnitCnt < m_MaxFreeListSize) {
		PBYTE ptr = address;
		ptr += sizeof(T);
		*reinterpret_cast<PUINT_PTR>(ptr) = static_cast<UINT_PTR>(m_FreeFront);
		m_FreeFront = static_cast<PBYTE>(address);
		m_UnitCnt++;
	}
	else {
		m_MemPoolMgr->Free(address);
	}
}


////////////////////////////////////////////////////////////////////////////////
// TlsMemPoolManager
////////////////////////////////////////////////////////////////////////////////
template<typename T>
TlsMemPoolManager<T>::TlsMemPoolManager()
{
	m_TlsIMainIndex = TlsAlloc();
	m_TlsSurpIndex = TlsAlloc();
}

template<typename T>
DWORD TlsMemPoolManager<T>::AllocTlsMemPool(size_t initUnitCnt) {
	if (TlsGetValue(m_TlsIMainIndex) == NULL) {
		// TlsMemPool ����
		TlsMemPool<T>* newTlsMemPool = new TlsMemPool<T>();
		if (newTlsMemPool == NULL) {
			DebugBreak();
		}
		TlsSetValue(m_TlsIMainIndex, newTlsMemPool);

		// LockFreeMemPool ����
		LockFreeMemPool* newLockFreeMemPool = new LockFreeMemPool();

		DWORD thID = GetThreadId(GetCurrentThread());
		{
			std::lock_guard<std::mutex> lockGuard(m_ThMemPoolMapMtx);
			m_ThMemPoolMap.insert({ thID , newLockFreeMemPool });
		}

		newTlsMemPool->m_MemPoolMgr = this;
		newTlsMemPool->m_UnitCnt = initUnitCnt;

		// The calloc function allocates storage space for an array of number elements, each of length size bytes.Each element is initialized to 0.
		newTlsMemPool->m_FreeFront = (PBYTE)calloc(newTlsMemPool->m_UnitCnt, sizeof(T) + sizeof(UINT_PTR));
		PBYTE ptr = newTlsMemPool->m_FreeFront;
		for (size_t idx = 0; idx < newTlsMemPool->m_UnitCnt - 1; idx++) {
			ptr += sizeof(T);
			*(static_cast<PUINT_PTR>(ptr)) = static_cast<UINT_PTR>(ptr + sizeof(UINT_PTR));
			ptr += sizeof(UINT_PTR);
		}

		if (newTlsMemPool->m_FreeFront == NULL) {
			DebugBreak();
		}
	}

	return m_TlsIMainIndex;
}

template<typename T>
void TlsMemPoolManager<T>::Alloc()
{
	TlsMemPool<T>* tlsMemPool = TlsGetValue(m_TlsIMainIndex);
	LockFreeMemPool* lfMemPool = TlsGetValue(m_TlsSurpIndex);
	// �ڽ��� �������� SurplusFront Ȯ��
	// NULL�� �ƴ϶��, m_FreeFront�� m_SurplusFront SWAP..
	if (lfMemPool->GetFreeCnt() > 0) {
		tlsMemPool.m_FreeFront = lfMemPool->AllocAll(tlsMemPool.m_UnitCnt);
	}

	// NULL�̶��, �ٸ� ��������� SurplusFront�� SWAP
	if (lfMemPool->m_FreeFront != NULL) {
		return;
	}
	else {
		// ���� ũ�Ⱑ ū �޸� Ǯ ã��
		LockFreeMemPool* maxFreeCntPool = NULL;
		size_t maxCnt = 0;
		{
			std::lock_guard<std::mutex> lockGuard(m_ThMemPoolMapMtx);
			for (auto iter = m_ThMemPoolMap.begin(); iter != m_ThMemPoolMap.end(); iter++) {
				if (iter->second > maxCnt) {
					
				}
			}
		}
	}

	// �ٸ� ��������� SurplusFront�� ��� NULL�̶��, 
	// �Ҵ�
}

template<typename T>
void TlsMemPoolManager<T>::Free(T* address)
{
	reinterpret_cast<LockFreeMemPool*>(TlsGetValue(m_TlsSurpIndex))->Free();
}
