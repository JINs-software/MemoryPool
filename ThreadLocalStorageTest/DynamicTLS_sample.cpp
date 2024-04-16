#include "DynamicTLS_sample.h"
#include <Windows.h>

DWORD g_dwTlsIndex;

void InitGlobalTlsIndex() {
	g_dwTlsIndex = TlsAlloc();
}

void DynamicTlsSampleFunction(stSomeStruct* psomest)
{
	if (psomest != NULL) {
		// ȣ���ڴ� �� �Լ��� �ʱ�ȭ�Ϸ� ��.

		// �����͸� ������ ������ �Ҵ�� ���� �ִ��� Ȯ��
		if (TlsGetValue(g_dwTlsIndex) == NULL) {
			// ������ �Ҵ�Ǿ� ���� �ʴٸ�, �� �Լ��� �ش� �����忡 ���� ���ʷ� ȣ��� ���
			PVOID dynamicAllocSpace = HeapAlloc(GetProcessHeap(), 0, sizeof(*psomest));
			TlsSetValue(g_dwTlsIndex, dynamicAllocSpace);
		}

		// ������ �����ϱ� ���� �޸� ������ ����, ���Ӱ� ���޵� ���� ����
		memcpy(TlsGetValue(g_dwTlsIndex), psomest, sizeof(*psomest));
	}
	else {
		// ȣ���ڰ� �ռ� �Լ��� �̹� �ʱ�ȭ��. 
		// �ռ� ����� �����͸� Ȱ���Ͽ� ������ �۾��� �����Ϸ� ��.
		// �����Ͱ� ����� ������ ����Ű�� �ּ� ���� ����.
		psomest = (stSomeStruct*)TlsGetValue(g_dwTlsIndex);
	}
}
