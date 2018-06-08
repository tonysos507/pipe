#include "TransClient.h"
#include <thread>
#include "hlog/hlogDef.h"
#include <strsafe.h>
#include "proto/proto/lyocx.pb.h"

static VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap);
static VOID WINAPI CompletedWriteRoutine(DWORD dwErr, DWORD cbWritten, LPOVERLAPPED lpOverLap);
static std::string GetErrorMeg();

PipeClient::PipeClient()
	: m_pPipe(INVALID_HANDLE_VALUE)
	, m_hEvt(INVALID_HANDLE_VALUE)
	, m_pFireUser(NULL)
	, m_fb(NULL)
	, m_lpPipeInst(NULL)
{
	m_lpPipeInst = (LPPIPEINST)GlobalAlloc(GPTR, sizeof(PIPEINST));
	m_lpPipeInst->pPSvr = this;
}

PipeClient::~PipeClient()
{
	SetEvent(m_hEvt);
	if (m_pPipe != INVALID_HANDLE_VALUE)
		CloseHandle(m_pPipe);
}

void PipeClient::SetGetAPIParamCB(pfnGetAPIParam fnAPIParamCB, void* pUser)
{
	m_pFireUser = pUser;
	m_fb = fnAPIParamCB;
}


bool PipeClient::InitTransfer(const std::string& szUUID)
{
	if (PreEvt(szUUID))
	{
		std::thread threadProducer(fProducerEventThd, this);
		threadProducer.detach();
		return true;
	}
	else
		return false;
}

void PipeClient::fProducerEventThd(void* pVoid)
{
	PipeClient* pThis = static_cast<PipeClient*>(pVoid);
	if (nullptr != pThis)
	{
		pThis->ReviceConn();
		return;
	}

	return;
}

bool PipeClient::PreEvt(const std::string& szUUID)
{
	std::wstring szPipeName = L"\\\\.\\pipe\\";
	std::wstring widestr = std::wstring(szUUID.begin(), szUUID.end());
	szPipeName += widestr;

	m_pPipe = CreateFile(
		szPipeName.c_str(),   // pipe name 
		GENERIC_READ |  // read and write access 
		GENERIC_WRITE,
		0,              // no sharing 
		NULL,           // default security attributes
		OPEN_EXISTING,  // opens existing pipe 
		FILE_FLAG_OVERLAPPED,              // default attributes 
		NULL);          // no template file 

	m_hEvt = CreateEvent(
		NULL, // default security attribute
		TRUE, // manual reset event
		TRUE, // initial state = signaled
		NULL);	// unnamed event object

	if (m_hEvt == NULL || m_pPipe == NULL)
		return false;

	ResetEvent(m_hEvt);
	DWORD dwMode = PIPE_READMODE_MESSAGE;
	BOOL fSuccess = SetNamedPipeHandleState(
		m_pPipe,    // pipe handle 
		&dwMode,  // new pipe mode 
		NULL,     // don't set maximum bytes 
		NULL);    // don't set maximum time 
	if (!fSuccess)
		return false;

	return true;
}

void PipeClient::ReviceConn()
{
	DWORD dwWait;

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

	while (true)
	{
		LOG_EXE_INFO("======>begin to wait....");
		dwWait = WaitForSingleObjectEx(m_hEvt, INFINITE, TRUE);
		switch (dwWait)
		{
		case WAIT_OBJECT_0:
			// stop PipeServer
			LOG_EXE_INFO("======>stop pipe");
			return;

		case WAIT_IO_COMPLETION:
			LOG_EXE_INFO("======>queued execution completed");
			break;

		default:
			LOG_EXE_INFO("======>wait error");
			return;
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

void PipeClient::SendMsg2(int iMsgType, const char* pszParam, int iParam, bool bParam)
{
	if (m_fb != nullptr)
	{
		m_fb(m_pFireUser, iMsgType, pszParam, iParam, bParam);
	}
}

VOID WINAPI CompletedReadRoutine(DWORD dwErr, DWORD cbBytesRead, LPOVERLAPPED lpOverLap)
{
	LPPIPEINST lpPipeInst = (LPPIPEINST)lpOverLap;
	if (dwErr == 0)
	{
		if (cbBytesRead != 0)
		{
			ocx::MsgReq req;
			std::string strdata(lpPipeInst->chRequest);
			req.ParseFromString(strdata);
			if (req.has_cmd())
			{
				int iMsgType = req.cmd();
				const char* strJson = req.has_strjson() ? req.strjson().c_str() : nullptr;
				int iParam = req.has_intparam() ? req.intparam() : 0;
				lpPipeInst->pPSvr->SendMsg2(iMsgType, strJson, iParam, false);
			}


			ZeroMemory(lpPipeInst->chRequest, BUFSIZE);
		}

		BOOL fRead = ReadFileEx(lpPipeInst->hPipeInst,
			lpPipeInst->chRequest,
			BUFSIZE * sizeof(char),
			(LPOVERLAPPED)lpPipeInst,
			(LPOVERLAPPED_COMPLETION_ROUTINE)CompletedReadRoutine);
		if (!fRead)
		{
			LOG_EXE_INFO("======>pipe server: %s ", GetErrorMeg().c_str());
		}
	}
	else
	{
		DWORD cbRet;
		if (!GetOverlappedResult(lpPipeInst->hPipeInst, &lpPipeInst->oOverlap, &cbRet, FALSE))
		{
			LOG_EXE_INFO("======>pipe server: %s ", GetErrorMeg().c_str());
		}
	}
}

void PipeClient::PostExeFireInfo(const std::string& msg)
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
		LOG_EXE_INFO("======>pipe client: %s ", GetErrorMeg().c_str());
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
			LOG_EXE_INFO("======>pipe client: %s ", GetErrorMeg().c_str());
		}
	}
}