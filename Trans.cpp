#include "Trans.h"
#include <thread>
#include "hlog/hlogDef.h"
#include <strsafe.h>
#include "proto/proto/lyocx.pb.h"

static VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap);
static VOID WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD cbWritten, LPOVERLAPPED lpOverLap);
static std::string GetErrorMeg();

PipeServer::PipeServer() 
	: m_pPipe(INVALID_HANDLE_VALUE)
	, m_pFireUser(NULL)
	, m_fb(NULL)
	, m_lpPipeInst(NULL)
{
	m_hEvt[0] = INVALID_HANDLE_VALUE;
	m_hEvt[1] = INVALID_HANDLE_VALUE;

	m_lpPipeInst = (LPPIPEINST)GlobalAlloc(GPTR, sizeof(PIPEINST));
	m_lpPipeInst->pPSvr = this;
}

PipeServer::~PipeServer()
{
	SetEvent(m_hEvt[1]);
	if(m_pPipe != INVALID_HANDLE_VALUE)
		CloseHandle(m_pPipe);
}

void PipeServer::SetFireInfoCB(pfnFireInfoCB fnFireInfoCB, void* pUser)
{
	m_pFireUser = pUser;
	m_fb = fnFireInfoCB;
}

void PipeServer::SetConnectCallback(std::function<void(void*)> fb)
{
	m_fbc = fb;
}

bool PipeServer::InitTransfer(const std::string& szUUID)
{
	if (PreEvt() && (m_fPendingIO = CreateAndConnectInst(szUUID)))
	{
		std::thread threadProducer(fProducerEventThd, this);
		threadProducer.detach();
		return true;
	}
	else
		return false;
}

void PipeServer::fProducerEventThd(void* pVoid)
{
	PipeServer* pThis = static_cast<PipeServer*>(pVoid);
	if (nullptr != pThis)
	{
		pThis->ReviceConn();
		return;
	}

	return;
}

bool PipeServer::CreateAndConnectInst(const std::string& szUUID)
{
	DWORD dwReturn;
	std::wstring szPipeName = L"\\\\.\\pipe\\";
	std::wstring widestr = std::wstring(szUUID.begin(), szUUID.end());
	szPipeName += widestr;

	m_pPipe = CreateNamedPipe(
		szPipeName.c_str(),		// pipe name
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		1,			// one instance only
		BUFSIZE*sizeof(char),	// output buffer size
		BUFSIZE * sizeof(char), // input buffer size
		PIPTIMEOUT,	// client time-out
		NULL);	// defautl security attributes

	if (m_pPipe == INVALID_HANDLE_VALUE)
		return false;

	return ConnectTo();
}

bool PipeServer::ConnectTo()
{
	BOOL fConnected, fPendingIO = FALSE;
	fConnected = ConnectNamedPipe(m_pPipe, &m_oConnect);
	if (fConnected)
		return false;

	switch (GetLastError())
	{
	case ERROR_IO_PENDING:
		fPendingIO = TRUE;
		break;

	case ERROR_PIPE_CONNECTED:
		if (SetEvent(m_oConnect.hEvent))
			break;

	default:
		return false;
	}

	return fPendingIO;
}

bool PipeServer::PreEvt()
{
	m_hEvt[0] = CreateEvent(
		NULL, // default security attribute
		TRUE, // manual reset event
		TRUE, // initial state = signaled
		NULL);	// unnamed event object
	m_hEvt[1] = CreateEvent(
		NULL, // default security attribute
		TRUE, // manual reset event
		TRUE, // initial state = signaled
		NULL);	// unnamed event object


	if (m_hEvt[0] == NULL || m_hEvt[1] == NULL)
		return false;

	m_oConnect.hEvent = m_hEvt[0];
	ResetEvent(m_hEvt[1]);
	return true;
}

void PipeServer::ReviceConn()
{
	DWORD dwWait;
	while (true)
	{
		dwWait = WaitForMultipleObjectsEx(2, m_hEvt, FALSE, INFINITE, TRUE);
		switch (dwWait)
		{
		case WAIT_OBJECT_0 + 0:
			// pipe action
			if (m_fPendingIO)
			{
				DWORD cbRet;
				if (!GetOverlappedResult(m_pPipe, &m_oConnect, &cbRet, FALSE))
				{
					LOG_PLUGIN_ERROR("======>pipe server: %s ", GetErrorMeg().c_str());
					return;
				}
			}
			ResetEvent(m_oConnect.hEvent);
			if (m_fbc != nullptr)
				m_fbc(m_pPipe);
			if (m_lpPipeInst)
			{
				m_lpPipeInst->hPipeInst = m_pPipe;
				BOOL fRead = ReadFileEx(m_lpPipeInst->hPipeInst,
					m_lpPipeInst->chRequest,
					BUFSIZE * sizeof(char),
					(LPOVERLAPPED)m_lpPipeInst,
					(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);
				if (!fRead)
				{
					LOG_PLUGIN_ERROR("======>pipe server: %s ", GetErrorMeg().c_str());
				}
			}
			break;
		case WAIT_OBJECT_0 + 1:
			// stop PipeServer
			LOG_PLUGIN_INFO("======>stop pipe server");
			return;

		case WAIT_IO_COMPLETION:
			break;

		default:
			return;
		}
	}
}

void PipeServer::SendMsg2(int iMsgType, const char* pszMsg, int iMsg)
{
	if (m_fb)
		m_fb(iMsgType, pszMsg, iMsg, m_pFireUser);
}

VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap)
{
	LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
	if (dwErr == 0)
	{
		if (cbBytesRead != 0)
		{
			ocx::FireReq req;
			std::string strdata(lpPipeInst->chRequest);
			req.ParseFromString(strdata);

			int iMsgType = req.cmd();
			const char* pszMsg = (req.strmsg()).c_str();
			int iMsg = req.intmsg();
			lpPipeInst->pPSvr->SendMsg2(iMsgType, pszMsg, iMsg);

			ZeroMemory(lpPipeInst->chRequest, BUFSIZE);
		}

		BOOL fRead = ReadFileEx(lpPipeInst->hPipeInst,
			lpPipeInst->chRequest,
			BUFSIZE * sizeof(char),
			(LPOVERLAPPED)lpPipeInst,
			(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);
		if (!fRead)
		{
			LOG_PLUGIN_ERROR("======>pipe server: %s ", GetErrorMeg().c_str());
		}
	}
	else
	{
		DWORD cbRet;
		if (!GetOverlappedResult(lpPipeInst->hPipeInst, &lpPipeInst->oOverlap, &cbRet, FALSE))
		{
			LOG_PLUGIN_INFO("======>pipe server: %s ", GetErrorMeg().c_str());
		}
	}
}

std::string GetErrorMeg()
{
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return "";

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		errorMessageID,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&messageBuffer,
		0,
		NULL);

	std::string message(messageBuffer, size);
	return message;
}

void PipeServer::PostMsgInfo(const std::string& msg)
{
	ZeroMemory(m_lpPipeInst->chReply, BUFSIZE);
	strcpy_s(m_lpPipeInst->chReply, BUFSIZE, msg.c_str());
	m_lpPipeInst->cbToWrite = (strlen(m_lpPipeInst->chReply) + 1) * sizeof(char);


	BOOL fWrite = FALSE;
	fWrite = WriteFileEx(m_lpPipeInst->hPipeInst,
		m_lpPipeInst->chReply,
		m_lpPipeInst->cbToWrite,
		(LPOVERLAPPED)m_lpPipeInst,
		(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedWriteRoutine);

	if (!fWrite)
	{
		LOG_PLUGIN_ERROR("======>pipe server send %s : %s ", msg.c_str(), GetErrorMeg().c_str());
	}
}

VOID WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD cbWritten, LPOVERLAPPED lpOverLap)
{
	LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
	if (dwErr == 0)
	{
	}
	else
	{
		DWORD cbRet;
		if (!GetOverlappedResult(lpPipeInst->hPipeInst, &lpPipeInst->oOverlap, &cbRet, FALSE))
		{
			LOG_PLUGIN_INFO("======>pipe server: %s ", GetErrorMeg().c_str());
		}
	}
}