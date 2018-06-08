#pragma once

#include <Windows.h>
#include <functional>

typedef void (__stdcall* pfnFireInfoCB)(int iMsgType, const char* pszMsg, int iMsg, void* pUser);
#define PIPTIMEOUT 5000
#define BUFSIZE 4096

class PipeServer;
typedef struct
{
	OVERLAPPED oOverlap;
	HANDLE hPipeInst;
	char chRequest[BUFSIZE];
	DWORD cbRead;
	char chReply[BUFSIZE];
	DWORD cbToWrite;
	PipeServer* pPSvr;
}PIPEINST, *LPPIPEINST;

class PipeServer
{
public:
	PipeServer();
	~PipeServer();
	void SetFireInfoCB(pfnFireInfoCB fnFireInfoCB, void* pUser);
	void SetConnectCallback(std::function<void(void*)> fb);
	bool InitTransfer(const std::string& szUUID);
	void PostMsgInfo(const std::string& msg);

	void SendMsg2(int iMsgType, const char* pszMsg, int iMsg);

	static void __stdcall fProducerEventThd(void* pVoid);

private:
	bool CreateAndConnectInst(const std::string& szUUID);
	bool ConnectTo();
	bool PreEvt();

	void ReviceConn();

private:
	HANDLE m_pPipe;
	HANDLE m_hEvt[2];
	OVERLAPPED m_oConnect;
	BOOL m_fSuccess;
	BOOL m_fPendingIO;
	void* m_pFireUser;
	pfnFireInfoCB m_fb;
	LPPIPEINST m_lpPipeInst;
	std::function<void(void*)> m_fbc;
};