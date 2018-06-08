#pragma once

#include <Windows.h>
#include <functional>

typedef void (__stdcall* pfnGetAPIParam)(void* pUser, int iMsgType, const char* pszParam, int iParam, bool bParam);
#define PIPTIMEOUT 5000
#define BUFSIZE 4096

class PipeClient;
typedef struct
{
	OVERLAPPED oOverlap;
	HANDLE hPipeInst;
	char chRequest[BUFSIZE];
	DWORD cbRead;
	char chReply[BUFSIZE];
	DWORD cbToWrite;
	PipeClient* pPSvr;
}PIPEINST, *LPPIPEINST;

class PipeClient
{
public:
	PipeClient();
	~PipeClient();
	void SetGetAPIParamCB(pfnGetAPIParam fnAPIParamCB, void* pUser);
	bool InitTransfer(const std::string& szUUID);
	void PostExeFireInfo(const std::string& msg);

	static void __stdcall fProducerEventThd(void* pVoid);

	void SendMsg2(int iMsgType, const char* pszParam, int iParam, bool bParam);

private:
	bool PreEvt(const std::string& szUUID);

	void ReviceConn();

private:
	HANDLE m_pPipe;
	HANDLE m_hEvt;
	OVERLAPPED m_oConnect;
	BOOL m_fSuccess;
	void* m_pFireUser;
	pfnGetAPIParam m_fb;
	LPPIPEINST m_lpPipeInst;
};