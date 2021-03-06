/*

	Author     : Jungle Wei
	Create Date: 2011-08
	Description: For Online Judge Core
*/

#include <windows.h>
#include <process.h>
#include <iostream>
#include <conio.h>
#include <stdlib.h>
#include <io.h>
#include <time.h>
#include <queue>
#include <cstring>
#include <string>
#include <sstream>
#include <cstdlib>
#include "tlhelp32.h"

#include"product\thirdpart32\cjson\cJSON.h"

#include "product\judge\include\judge_inc.h"


using namespace std;


char INI_filename[] = STARTUP_CFG;//系统配置文件

int isDeleteTemp=0;
int isRestrictedFunction=0;
int  limitJudge=50;
DWORD OutputLimit=10000;
char workPath[MAX_PATH];
char judgeLogPath[MAX_PATH];
int JUDGE_LOG_BUF_SIZE = 200;
char logPath[MAX_PATH]="log\\";
char judgePath[MAX_PATH];

#define PORT 5000
#define BUFFER 1024

int g_sock_port=PORT;

int g_judge_mode = JUDGE_MODE_ACM;

typedef struct tagJudge_Data_S
{
	int solutionId;

}JUDGE_DATA_S;

queue <JUDGE_DATA_S> g_JudgeQueue; /* 全局队列 */

extern void pdt_debug_print(const char *format, ...);

ULONG Judge_DebugSwitch(ULONG st)
{
	g_oj_debug_switch = st;

	return OS_TRUE;
}

/* #pragma comment(linker, "/subsystem:windows /ENTRY:mainCRTStartup") */

int Judge_InitSocket()
{
	write_log(JUDGE_INFO,"Start initialization of Socket...");

	WSADATA wsaData;
    WORD sockVersion = MAKEWORD(2, 2);

	if(WSAStartup(sockVersion, &wsaData) != 0)
		return 0;

	g_sListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(g_sListen == INVALID_SOCKET)
	{
		write_log(JUDGE_SYSTEM_ERROR,"create socket error");
		return 0;
	}

	sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(g_sock_port);
	sin.sin_addr.S_un.S_addr = INADDR_ANY;


	int trybind=50;
	int ret=0;

	ret = bind(g_sListen,(LPSOCKADDR)&sin,sizeof(sin));

	while(ret == SOCKET_ERROR && trybind > 0)
	{
		bind(g_sListen,(LPSOCKADDR)&sin,sizeof(sin));
		//write_log(JUDGE_SYSTEM_ERROR,"bind failed:%d , it will try later...",WSAGetLastError());
		trybind--;
		Sleep(100);
	}

	if(ret<0)
	{
		while (ret == SOCKET_ERROR)
		{
			g_sock_port++;
			sin.sin_port = htons(g_sock_port);
			ret =  bind(g_sListen,(LPSOCKADDR)&sin,sizeof(sin));
			if (ret != SOCKET_ERROR)
			{
				char szPort[10] = {0};
				(void)itoa(g_sock_port, szPort ,10);
				WritePrivateProfileString("System","sock_port",szPort,INI_filename);
			}
			Sleep(10);
		}
	}

	pdt_debug_print("Info: Socket bind port(%u) ok.", g_sock_port);

	write_log(JUDGE_INFO,"Bind success...");

	//进入监听状态
	int trylisten=50; //重试listen次数
	while((ret=listen(g_sListen,20))==SOCKET_ERROR&&trylisten)
	{
		write_log(JUDGE_SYSTEM_ERROR,"listen failed:%d , it will try later..",WSAGetLastError());
		trylisten--;
		Sleep(100);
	}

	if(ret<0)
	{
		write_log(JUDGE_SYSTEM_ERROR,"Listen failed...");
		pdt_debug_print("Error: Listen port(%u) failed......[code:%u]", g_sock_port, GetLastError());

		closesocket(g_sListen);
		WSACleanup();
		return 0;
	}

	pdt_debug_print("Info: Socket listen ok.");
	write_log(JUDGE_INFO,"Listen success...");

	return 1;
}
//////////////////////////////////////////////////////////////end socket

void Judge_Destroy()
{
	closesocket(g_sListen);
}

/**
 * 初始化系统设置
 */
void Judge_InitConfigData()
{
	g_sock_port=GetPrivateProfileInt("System","sock_port",PORT,INI_filename);
	GetPrivateProfileString("System","JudgePath","",judgePath,sizeof(judgePath),INI_filename);

	g_judge_mode=GetPrivateProfileInt("Judge","judge_mode",0,INI_filename);
	isDeleteTemp=GetPrivateProfileInt("Judge","DeleteTemp",0,INI_filename);
	limitJudge=GetPrivateProfileInt("Judge","LimitJudge",20,INI_filename);
	OutputLimit=GetPrivateProfileInt("Judge","OutputLimit",10000,INI_filename);
	JUDGE_LOG_BUF_SIZE=GetPrivateProfileInt("Judge","judge_logbuf_size",500,INI_filename);
	GL_vjudge_enable=GetPrivateProfileInt("Judge","vjudge_enable",OS_NO,INI_filename);
	isRestrictedFunction=GetPrivateProfileInt("Judge","isRestrictedFunction",0,INI_filename);
	GetPrivateProfileString("Judge","WorkingPath","",workPath,sizeof(workPath),INI_filename);

	GetPrivateProfileString("Judge","JudgeLogPath","",judgeLogPath,sizeof(judgeLogPath),INI_filename);

	GetPrivateProfileString("MySQL","url","",Mysql_url,sizeof(Mysql_url),INI_filename);
	GetPrivateProfileString("MySQL","username","NULL",Mysql_username,sizeof(Mysql_username),INI_filename);
	GetPrivateProfileString("MySQL","password","NULL",Mysql_password,sizeof(Mysql_password),INI_filename);
	GetPrivateProfileString("MySQL","table","",Mysql_table,sizeof(Mysql_table),INI_filename);
	Mysql_port=GetPrivateProfileInt("MySQL","port",0,INI_filename);

	write_log(JUDGE_INFO,"Socketport:%d, Workpath:%s",g_sock_port,workPath);
	write_log(JUDGE_INFO,"MySQL:%s %s %s %s %d",Mysql_url,Mysql_username,Mysql_password,Mysql_table,Mysql_port);

}

/**
 * 该次提交的系统设置
 */
void Judge_InitJudgePath(JUDGE_SUBMISSION_ST *pstJudgeSubmission)
{
	if (NULL == pstJudgeSubmission)
	{
		write_log(JUDGE_ERROR,"Judge_InitJudgePath ERROR, pstJudgeSubmission is NULL....");
		return ;
	}

	if( (_access(workPath, 0 )) == -1 )
	{
		CreateDirectory(workPath,NULL);
	}

	char keyname[100]={0};
	sprintf(keyname,"Language%d", pstJudgeSubmission->stSolution.languageId);

	GetPrivateProfileString("Language",keyname,"",pstJudgeSubmission->languageName,100,INI_filename);

	GetPrivateProfileString("LanguageExt",pstJudgeSubmission->languageName,"",pstJudgeSubmission->languageExt,10,INI_filename);

	GetPrivateProfileString("LanguageExe",pstJudgeSubmission->languageName,"",pstJudgeSubmission->languageExe,10,INI_filename);

	GetPrivateProfileString("CompileCmd",pstJudgeSubmission->languageName,"",pstJudgeSubmission->compileCmd,1024,INI_filename);

	GetPrivateProfileString("RunCmd",pstJudgeSubmission->languageName,"",pstJudgeSubmission->runCmd,1024,INI_filename);

	GetPrivateProfileString("SourcePath",pstJudgeSubmission->languageName,"",pstJudgeSubmission->sourcePath,1024,INI_filename);

	GetPrivateProfileString("ExePath",pstJudgeSubmission->languageName,"",pstJudgeSubmission->exePath,1024,INI_filename);

	pstJudgeSubmission->isTranscoding=GetPrivateProfileInt("Transcoding",pstJudgeSubmission->languageName,0,INI_filename);

	pstJudgeSubmission->limitIndex=GetPrivateProfileInt("TimeLimit",pstJudgeSubmission->languageName,1,INI_filename);

	pstJudgeSubmission->nProcessLimit=GetPrivateProfileInt("ProcessLimit",pstJudgeSubmission->languageName,1,INI_filename);


	char buf[1024];
	sprintf(buf, "%d", pstJudgeSubmission->stSolution.solutionId);
	string name = buf;
	string compile_string=pstJudgeSubmission->compileCmd;
	replace_all_distinct(compile_string,"%PATH%",workPath);
	replace_all_distinct(compile_string,"%SUBPATH%",pstJudgeSubmission->subPath);
	replace_all_distinct(compile_string,"%NAME%",name);
	replace_all_distinct(compile_string,"%EXT%",pstJudgeSubmission->languageExt);
	replace_all_distinct(compile_string,"%EXE%",pstJudgeSubmission->languageExe);
	strcpy(pstJudgeSubmission->compileCmd,compile_string.c_str());       /* 编译命令行 */

	string runcmd_string=pstJudgeSubmission->runCmd;
	replace_all_distinct(runcmd_string,"%PATH%",workPath);
	replace_all_distinct(runcmd_string,"%SUBPATH%",pstJudgeSubmission->subPath);
	replace_all_distinct(runcmd_string,"%NAME%",name);
	replace_all_distinct(runcmd_string,"%EXT%",pstJudgeSubmission->languageExt);
	replace_all_distinct(runcmd_string,"%EXE%",pstJudgeSubmission->languageExe);
	strcpy(pstJudgeSubmission->runCmd,runcmd_string.c_str());			 /* 运行命令行*/

	string sourcepath_string=pstJudgeSubmission->sourcePath;
	replace_all_distinct(sourcepath_string,"%PATH%",workPath);
	replace_all_distinct(sourcepath_string,"%SUBPATH%",pstJudgeSubmission->subPath);
	replace_all_distinct(sourcepath_string,"%NAME%",name);
	replace_all_distinct(sourcepath_string,"%EXT%",pstJudgeSubmission->languageExt);
	strcpy(pstJudgeSubmission->sourcePath,sourcepath_string.c_str());		 /* 源程序路径*/

	string exepath_string=pstJudgeSubmission->exePath;
	replace_all_distinct(exepath_string,"%PATH%",workPath);
	replace_all_distinct(exepath_string,"%SUBPATH%",pstJudgeSubmission->subPath);
	replace_all_distinct(exepath_string,"%NAME%",name);
	replace_all_distinct(exepath_string,"%EXE%",pstJudgeSubmission->languageExe);
	strcpy(pstJudgeSubmission->exePath,exepath_string.c_str());				 /* 可执行文件路径*/

	sprintf(pstJudgeSubmission->DebugFile,"%s%s%s.txt",workPath,pstJudgeSubmission->subPath,name.c_str());  /* debug文件路径*/
	sprintf(pstJudgeSubmission->ErrorFile,"%s%s%s_re.txt",workPath,pstJudgeSubmission->subPath,name.c_str());  /* re文件路径*/

	if( (_access(judgeLogPath, 0 )) == -1 )
	{
		CreateDirectory(judgeLogPath,NULL);
	}

}

/**
 *  初始化pstJudgeSubmission数据，并且为该次提交创建一个工作目录
 */
void Judge_InitSubmissionData(JUDGE_SUBMISSION_ST *pstJudgeSubmission)
{
	pstJudgeSubmission->stSolution.verdictId = V_Q;
	pstJudgeSubmission->stSolution.reJudge = 0;

	pstJudgeSubmission->isTranscoding = 0;
	pstJudgeSubmission->limitIndex = 1;
	pstJudgeSubmission->nProcessLimit = 1;

	time_t timep;
	time(&timep);
	srand((int)time(0)*3);
	pstJudgeSubmission->ulSeed = timep + rand();
	sprintf(pstJudgeSubmission->subPath, "%d_%u\\", pstJudgeSubmission->stSolution.solutionId, pstJudgeSubmission->ulSeed);

	char fullPath[1024] = {0};
	sprintf(fullPath, "%s%s", workPath, pstJudgeSubmission->subPath);
	while( (_access(fullPath, 0 )) != -1 )
	{
		write_log(JUDGE_INFO,"Gernerate another Seed...(%u)", pstJudgeSubmission->ulSeed);
		Sleep(10);
		pstJudgeSubmission->ulSeed = timep + rand();

		sprintf(pstJudgeSubmission->subPath, "%d_%u\\", pstJudgeSubmission->stSolution.solutionId, pstJudgeSubmission->ulSeed);
		sprintf(fullPath, "%s%s", workPath, pstJudgeSubmission->subPath);
	}
	CreateDirectory(fullPath,NULL);
}


/**
 * 创建运行沙盒
 */
HANDLE Judge_CreateSandBox(JUDGE_SUBMISSION_ST *pstJudgeSubmission)
{
	HANDLE hjob =CreateJobObject(NULL,NULL);
	if(hjob!=NULL)
	{
		JOBOBJECT_BASIC_LIMIT_INFORMATION jobli;
		 memset(&jobli,0,sizeof(jobli));
		jobli.LimitFlags=JOB_OBJECT_LIMIT_PRIORITY_CLASS|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
		jobli.PriorityClass=IDLE_PRIORITY_CLASS;
		jobli.ActiveProcessLimit=pstJudgeSubmission->nProcessLimit;
		if(SetInformationJobObject(hjob,JobObjectBasicLimitInformation,&jobli,sizeof(jobli)))
		{
			JOBOBJECT_BASIC_UI_RESTRICTIONS jobuir;
			jobuir.UIRestrictionsClass=JOB_OBJECT_UILIMIT_NONE;
			jobuir.UIRestrictionsClass |=JOB_OBJECT_UILIMIT_EXITWINDOWS;
			jobuir.UIRestrictionsClass |=JOB_OBJECT_UILIMIT_READCLIPBOARD ;
			jobuir.UIRestrictionsClass |=JOB_OBJECT_UILIMIT_WRITECLIPBOARD ;
			jobuir.UIRestrictionsClass |=JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS;
			jobuir.UIRestrictionsClass |=JOB_OBJECT_UILIMIT_HANDLES;

			if(SetInformationJobObject(hjob,JobObjectBasicUIRestrictions,&jobuir,sizeof(jobuir)))
			{
				return hjob;
			}
			else
			{
				write_log(JUDGE_SYSTEM_ERROR,"SetInformationJobObject  JOBOBJECT_BASIC_UI_RESTRICTIONS   [Error:%d]\n",GetLastError());
			}
		}
		else
		{
			write_log(JUDGE_SYSTEM_ERROR,"SetInformationJobObject  JOBOBJECT_BASIC_LIMIT_INFORMATION   [Error:%d]\n",GetLastError());
		}
	}
	else
	{
		write_log(JUDGE_SYSTEM_ERROR,"CreateJobObject     [Error:%d]\n",GetLastError());
	}
	return NULL;
}

/**
 * 判断是否可以正确运行
 */
bool Judge_ProcessToSandbox(HANDLE job,PROCESS_INFORMATION p)
{
	if(AssignProcessToJobObject(job,p.hProcess))
	{
		return true;
	} else {
        judge_outstring("AssignProcessToJobObject Error:%s\r\n",GetLastError());
		write_log(JUDGE_SYSTEM_ERROR,"AssignProcessToJobObject Error:%s",GetLastError());
        return false;
	}

}

/**
 * 编译程序线程
 */
unsigned _stdcall Judge_CompileThread(void *pData)
{
	JUDGE_SUBMISSION_ST *pstJudgeSubmission = (JUDGE_SUBMISSION_ST *)pData;

	if (NULL == pstJudgeSubmission)
	{
		write_log(JUDGE_ERROR,"Judge_CompileThread ERROR, pstJudgeSubmission is NULL....");
		return 0;
	}
    judge_outstring("进入了编译线程...\r\n");

    system(pstJudgeSubmission->compileCmd);//调用系统指令编译代码

    judge_outstring("调用了编译指令:%s\r\n",pstJudgeSubmission->compileCmd);

	write_log(JUDGE_INFO,"End Judge_CompileThread...");

	return 0;
}

/**
 * 调用编译线程完成代码编译，并且生成编译文件到指定目录
 */
int Judge_CompileProc(JUDGE_SUBMISSION_ST *pstJudgeSubmission)
{
	if (NULL == pstJudgeSubmission)
	{
		write_log(JUDGE_ERROR,"Judge_CompileProc ERROR, pstJudgeSubmission is NULL....");
		return 0;
	}

	if(strcmp(pstJudgeSubmission->runCmd,"NULL")==0) return 1;

	HANDLE hThread_com;

	hThread_com = (HANDLE)_beginthreadex(NULL, NULL, Judge_CompileThread, (void*)pstJudgeSubmission, 0, NULL);
	if(hThread_com == NULL)
	{
		write_log(JUDGE_ERROR,"Create Judge_CompileThread Error");
		CloseHandle(hThread_com);
		return 0;
	}

	write_log(JUDGE_INFO,"Create Judge_CompileThread ok...");

	DWORD status_ = WaitForSingleObject(hThread_com,30000);
	if(status_ > 0)
	{
		write_log(JUDGE_WARNING,"Compile over time_limit");
	}

	write_log(JUDGE_INFO,"WaitForSingleObject wait time ok...");

	if( (_access(pstJudgeSubmission->exePath, 0 )) != -1 )
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

BOOL Judge_ExistException(DWORD dw)
{
	switch(dw)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		return TRUE;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return TRUE;
	case EXCEPTION_BREAKPOINT:
		return TRUE;
	case EXCEPTION_SINGLE_STEP:
		return TRUE;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		return TRUE;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		return TRUE;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		return TRUE;
	case EXCEPTION_FLT_INEXACT_RESULT:
		return TRUE;
	case EXCEPTION_FLT_INVALID_OPERATION:
		return TRUE;
	case EXCEPTION_FLT_OVERFLOW:
		return TRUE;
	case EXCEPTION_FLT_STACK_CHECK:
		return TRUE;
	case EXCEPTION_FLT_UNDERFLOW:
		return TRUE;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		return TRUE;
	case EXCEPTION_INT_OVERFLOW:
		return TRUE;
	case EXCEPTION_PRIV_INSTRUCTION:
		return TRUE;
	case EXCEPTION_IN_PAGE_ERROR:
		return TRUE;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		return TRUE;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		return TRUE;
	case EXCEPTION_STACK_OVERFLOW:
		return TRUE;
	case EXCEPTION_INVALID_DISPOSITION:
		return TRUE;
	case EXCEPTION_GUARD_PAGE:
		return TRUE;
	case EXCEPTION_INVALID_HANDLE:
		return TRUE;
	default:
		return FALSE;
	}
}

/**
 *  程序运行线程
 */
unsigned _stdcall Judge_RunProgramThread(void *pData) //ac
{
    /* cmd/c solution.exe <data.in >data.out 2>error.txt */
    /*ChildOut_Write是子进程的输出句柄，ChildOut_Read是父进程用于读取子进程输出的句柄*/
    HANDLE ChildOut_Read, ChildOut_Write;

    SECURITY_ATTRIBUTES saAttr = {0};//以windows内核最低权限创建对象

    JUDGE_SUBMISSION_ST *pstJudgeSubmission = (JUDGE_SUBMISSION_ST *)pData;

    if (NULL == pstJudgeSubmission)
    {
        write_log(JUDGE_ERROR,"Judge_RunProgramThread ERROR, pstJudgeSubmission is NULL....");
        return 0;
    }

    judge_outstring("进入了代码执行线程\r\n");

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    CreatePipe(&ChildOut_Read, &ChildOut_Write, &saAttr, 0);
    SetHandleInformation(ChildOut_Read, HANDLE_FLAG_INHERIT, 0);

    SetErrorMode(SEM_NOGPFAULTERRORBOX );

    STARTUPINFO StartupInfo = {0};
    StartupInfo.cb = sizeof(STARTUPINFO);
    StartupInfo.hStdOutput = ChildOut_Write;
    StartupInfo.dwFlags |= STARTF_USESTDHANDLES;

    write_log(JUDGE_INFO,"CreateProcess(%s)", pstJudgeSubmission->runCmd);
    judge_outstring("准备执行CreateProcess(%s)\r\n",pstJudgeSubmission->runCmd);
    /* |CREATE_NEW_CONSOLE */
    // TODO: 找到了程序运行命令

    PROCESS_INFORMATION pi;

    if(CreateProcess(NULL,pstJudgeSubmission->runCmd,NULL,NULL,TRUE,0,NULL,NULL,&StartupInfo,&pstJudgeSubmission->pProRunInfo)){
        judge_outstring("CreateProcess ok...\r\n");
        pstJudgeSubmission->hJob = Judge_CreateSandBox(pstJudgeSubmission);
        if(pstJudgeSubmission->hJob != NULL) {
            judge_outstring("Judge_CreateSandBox ok...\r\n");
            if(Judge_ProcessToSandbox(pstJudgeSubmission->hJob, pstJudgeSubmission->pProRunInfo))
            {
                judge_outstring("Judge_ProcessToSandbox ok...\r\n");

                ResumeThread(pstJudgeSubmission->pProRunInfo.hThread);
                CloseHandle(pstJudgeSubmission->pProRunInfo.hThread);

                judge_outstring("正在执行代码运行线程...\r\n");

                char buffer[BUFSIZE];
                DWORD read = 0;
                char index_str[2];
                int index=1;
                string str="";
                if (ReadFile(ChildOut_Read, buffer, sizeof(buffer) - 1, &read, NULL)){
                    str.append(buffer);
                }

                CloseHandle(ChildOut_Write);
                CloseHandle(ChildOut_Read);
                ChildOut_Read=NULL;

                if (str.size() < JSONBUFSIZE)
                {
                    pstJudgeSubmission->stSolution.verdictId = V_AC;
                    SQL_updateSolutionJsonResult(pstJudgeSubmission->stSolution.solutionId, str.c_str());
                    strcpy(pstJudgeSubmission->result_Json, str.c_str());
                } else{
                    judge_outstring("程序的运行结果过长");
                }

                return 1;
            } else{
                judge_outstring("ProcessToSandBox Error:%s\r\n",GetLastError());
            }
        } else{
            judge_outstring("Judge_CreateSandBox Error:%s\r\n",GetLastError());
        }

    } else{
        judge_outstring("CreateProcess failed...\r\n");
    }

    return 0;
}

int Judge_RunLocalSolution(JUDGE_SUBMISSION_ST *pstJudgeSubmission)
{
	if (NULL == pstJudgeSubmission)
	{
		write_log(JUDGE_ERROR,"Judge_RunLocalSolution ERROR, pstJudgeSubmission is NULL....");
		return 0;
	}
	   pstJudgeSubmission->dwProStatusCode = 0;

		HANDLE hThread_run;
		hThread_run = (HANDLE)_beginthreadex(NULL, NULL, Judge_RunProgramThread, (void*)pstJudgeSubmission, 0, NULL);
		if(hThread_run == NULL)
		{
			write_log(JUDGE_ERROR,"Create thread error");
			CloseHandle(hThread_run);
		}

		write_log(JUDGE_ERROR,"Create Judge_RunProgramThread ok...");

		DWORD status_ = WaitForSingleObject(hThread_run, 9000);
		if(status_>0)
		{
			write_log(JUDGE_INFO,"hThread_run TIME LIMIT");
			pstJudgeSubmission->stSolution.verdictId = V_TLE;
            judge_outstring("four %d", pstJudgeSubmission->stSolution.verdictId);
		}

		//get process state
		GetExitCodeProcess(pstJudgeSubmission->pProRunInfo.hProcess, &(pstJudgeSubmission->dwProStatusCode));

	    if(Judge_ExistException(pstJudgeSubmission->dwProStatusCode)) {
		  pstJudgeSubmission->stSolution.verdictId=V_RE;
            judge_outstring("one %d", pstJudgeSubmission->stSolution.verdictId);
	   } else if(pstJudgeSubmission->dwProStatusCode == STILL_ACTIVE) {
			TerminateProcess(pstJudgeSubmission->pProRunInfo.hProcess, 0);
		} else{
			pstJudgeSubmission->stSolution.verdictId = V_AC;
            judge_outstring("three %d", pstJudgeSubmission->stSolution.verdictId);
		}

	    TerminateJobObject(pstJudgeSubmission->hJob,0);
		CloseHandle(pstJudgeSubmission->pProRunInfo.hProcess);
		CloseHandle(pstJudgeSubmission->hJob);pstJudgeSubmission->hJob = NULL;
	return 0;

}

int Judge_Local(JUDGE_SUBMISSION_ST *pstJudgeSubmission)
{
	write_log(JUDGE_INFO,"Enter Judge_Local...");

    judge_outstring("准备进行编译...\r\n");
	if(0 == Judge_CompileProc(pstJudgeSubmission))
	{
        judge_outstring("编译出现异常...\r\n");
        pstJudgeSubmission->stSolution.verdictId = V_CE;
        SQL_updateCompileInfo(pstJudgeSubmission);
	}
	else
	{
        judge_outstring("编译正确，准备运行...\r\n");
		write_log(JUDGE_INFO,"Start Run...");
		Judge_RunLocalSolution(pstJudgeSubmission);
	}

	write_log(JUDGE_INFO,"End Judge_Local...");

	return OS_TRUE;
}

int Judge_SendToJudger(int solutionId, int port,char *ip)
{

	SOCKET sClient_hdu;

    sClient_hdu = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(sClient_hdu == INVALID_SOCKET)
	{
		pdt_debug_print("Judge_SendToJudger socket error");
		return OS_ERR;
	}

	sockaddr_in servAddr;
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(port);
	servAddr.sin_addr.S_un.S_addr =inet_addr(ip);

	if(connect(sClient_hdu,(sockaddr*)&servAddr,sizeof(servAddr))==SOCKET_ERROR)
	{
		pdt_debug_print("Judge_SendToJudger connect error");
		closesocket(sClient_hdu);
		return OS_ERR;
	}

	send(sClient_hdu,(const char*)&solutionId,sizeof(solutionId),0);

	closesocket(sClient_hdu);

	return OS_OK;
}


unsigned _stdcall  Judge_Proc(void *pData)
{
	int ret = OS_OK;
	int isExist = OS_NO;
	int solutionId = *(int *)pData;
	char *pJsonBuf = NULL;
	JUDGE_SUBMISSION_ST stJudgeSubmission;


	//初始化JUDGE_SUBMISSION_ST数据
	pJsonBuf = (char*)malloc(JSONBUFSIZE);
	if (NULL == pJsonBuf)
	{
		return OS_ERR;
	}

	memset(pJsonBuf, 0, JSONBUFSIZE);
	memset(&stJudgeSubmission, 0, sizeof(stJudgeSubmission));
	stJudgeSubmission.result_Json = pJsonBuf;

	stJudgeSubmission.stSolution.solutionId = solutionId;
	Judge_InitSubmissionData(&stJudgeSubmission);
	write_log(JUDGE_INFO, "Start judge solution %d.", stJudgeSubmission.stSolution.solutionId);
    //从数据库中获取Solution
	ret = SQL_getSolutionByID(stJudgeSubmission.stSolution.solutionId, &(stJudgeSubmission.stSolution), &isExist);
	if (OS_ERR == ret || OS_NO == isExist)
	{
		pdt_debug_print("No such solution %d.", stJudgeSubmission.stSolution.solutionId);
		return OS_ERR;
	}
	//初始化程序编译，运行等指令
	Judge_InitJudgePath(&stJudgeSubmission);
    //获取运行的源代码
	ret = SQL_getSolutionSource(&stJudgeSubmission);
	if (OS_OK != ret)
	{
		pdt_debug_print("SQL_getSolutionSource failed.(solutionId=%d)", stJudgeSubmission.stSolution.solutionId);
		write_log(JUDGE_INFO,"SQL_getSolutionSource failed.(solutionId=%d)", stJudgeSubmission.stSolution.solutionId);
		return OS_ERR;
	}
	write_log(JUDGE_INFO,"Do SQL_getSolutionSource ok. (solutionId=%d)", stJudgeSubmission.stSolution.solutionId);

    //获取本地运行结果
	ret = Judge_Local(&stJudgeSubmission);

	write_log(JUDGE_INFO,"Do Judge finish. (solutionId=%d)", stJudgeSubmission.stSolution.solutionId);

	SQL_updateSolution(stJudgeSubmission.stSolution.solutionId,stJudgeSubmission.stSolution.verdictId);
	string time_string_;
	API_TimeToString(time_string_, stJudgeSubmission.stSolution.submitDate);
	judge_outstring("\r\n -----------------------"
				"\r\n     *Judge verdict*"
				"\r\n -----------------------"
				"\r\n SolutionId   : %3d"
				"\r\n Lang.        : %3s"
				"\r\n Return code  : %3u"
				"\r\n Verdict      : %3s"
				"\r\n Submit Date  : %3s"
				"\r\n Username     : %3s"
				"\r\n Json Result  : %s"
				"\r\n -----------------------\r\n",
					stJudgeSubmission.stSolution.solutionId,
					stJudgeSubmission.languageName,
					stJudgeSubmission.dwProStatusCode,
					VERDICT_NAME[stJudgeSubmission.stSolution.verdictId],
					time_string_.c_str(), stJudgeSubmission.stSolution.username,
					stJudgeSubmission.result_Json);
	free(pJsonBuf);
	return OS_OK;
}

void Judge_PushQueue(int solutionId)
{
	JUDGE_DATA_S jd = {0};

	jd.solutionId = solutionId;
	g_JudgeQueue.push(jd);
}

/* virtual-judge & local-judge 应分两个队列 */
unsigned _stdcall Judge_DispatchThread(void *pEntry)
{
	JUDGE_DATA_S jd;

	for (;;)
	{
		if(g_JudgeQueue.size()>limitJudge)
		{
			return 0;
		}

		if(!g_JudgeQueue.empty())
		{
				jd = g_JudgeQueue.front();

				/* 启动评判 */
				Judge_Proc((void*)&(jd.solutionId));
				//_beginthreadex(NULL, NULL, Judge_Proc, (void*)&(jd.solutionId), 0, NULL);

				g_JudgeQueue.pop();
		}

		Sleep(1);
	}

	return 0;
}


unsigned _stdcall Judge_ListenThread(void *pEntry)
{
	sockaddr_in remoteAddr;
	SOCKET sClient;
	int nAddrLen = sizeof(remoteAddr);
	JUDGE_DATA_S j;

	while(TRUE)
	{
		sClient = accept(g_sListen, (SOCKADDR*)&remoteAddr, &nAddrLen);
		if(sClient == INVALID_SOCKET)
		{
			write_log(JUDGE_ERROR,"Accept() Error");
			continue;
		}

		int ret=recv(sClient,(char*)&j,sizeof(j),0);
		if(ret>0)
		{
			write_log(JUDGE_INFO,"Push SolutionId:%d into Judge Queue....",j.solutionId);
			g_JudgeQueue.push(j);
		}
		Sleep(1);
	}

	write_log(JUDGE_ERROR,"ListenThread Crash");
	closesocket(sClient);

	return 0;
}

long WINAPI Judge_ExceptionFilter(EXCEPTION_POINTERS * lParam){
	pdt_debug_print("Judge Thread Exit...[code:%u]", GetLastError());
	write_log(JUDGE_ERROR,"Judge Thread Exit after 10 second...(GetLastError=%u)",GetLastError());
	Sleep(1);

	/* ShellExecuteA(NULL,"open",judgePath,NULL,NULL,SW_SHOWNORMAL); */

	closesocket(g_sListen);
	WSACleanup();

	return EXCEPTION_EXECUTE_HANDLER;
}

int GetProcessThreadList()
{
	HANDLE hThreadSnap;
	THREADENTRY32 th32;
	DWORD th32ProcessID = GetCurrentProcessId();

	printf(" ProcessID: %ld\n", th32ProcessID);

	hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, th32ProcessID);

	if (hThreadSnap == INVALID_HANDLE_VALUE)
	{
		return 1;
	}

	th32.dwSize = sizeof(THREADENTRY32);
	if (!Thread32First(hThreadSnap, &th32))
	{
		CloseHandle(hThreadSnap);
		return 1;
	}

	do
	{
		if (th32.th32OwnerProcessID == th32ProcessID)
		{
			printf(" ThreadID: %ld\n", th32.th32ThreadID);

		}
	}while(Thread32Next(hThreadSnap, &th32));

	CloseHandle(hThreadSnap);
	return 0;
}

int OJ_Init()
{
	SetUnhandledExceptionFilter(Judge_ExceptionFilter);
 	SetErrorMode(SEM_NOGPFAULTERRORBOX );

	if( (_access(logPath, 0 )) == -1 )
	{
		CreateDirectory(logPath,NULL);
	}

	Judge_InitConfigData();

	Judge_DebugSwitch(JUDGE_DEBUG_OFF);

	write_log(JUDGE_INFO,"Info: OJ_Init OK....");
	pdt_debug_print("Info: OJ_Init OK....");
		
	return OS_OK;
}

int OJ_InitData()
{

	if(SQL_InitMySQL()==0)
	{
		write_log(JUDGE_ERROR,"Init MySQL JUDGE_ERROR...");
		pdt_debug_print("Error: Judge can not connect to MySQL.");
	}

	if(Judge_InitSocket()==0)
	{
		write_log(JUDGE_ERROR,"Init Socket JUDGE_ERROR...");
		pdt_debug_print("Error: Judge task killed itself...[code:%u]", GetLastError());
	}

	write_log(JUDGE_INFO,"Info: OJ_InitData OK....");
	pdt_debug_print("Info: OJ_InitData OK....");
	
}


/*
unsigned long _beginthreadex( void *security,
								unsigned stack_size,
								unsigned ( __stdcall *start_address )( void * ),
								void *arglist,
								unsigned initflag,
								unsigned *thrdaddr );

//第1个参数：安全属性，NULL为默认安全属性
//第2个参数：指定线程堆栈的大小。如果为0，则线程堆栈大小和创建它的线程的相同。一般用0
//第3个参数：指定线程函数的地址，也就是线程调用执行的函数地址(用函数名称即可，函数名称就表示地址)
//第4个参数：传递给线程的参数的指针，可以通过传入对象的指针，在线程函数中再转化为对应类的指针
//第5个参数：线程初始状态，0:立即运行；CREATE_SUSPEND：suspended（悬挂）
//第6个参数：用于记录线程ID的地址

*/

unsigned _stdcall  OJ_TaskEntry(void *pEntry)
{
	write_log(JUDGE_INFO,"Running Judge Core...");

	(void)OJ_InitData();

	_beginthreadex(NULL, 0, Judge_DispatchThread, NULL, NULL, NULL);
	_beginthreadex(NULL, 0, Judge_ListenThread, NULL, NULL, NULL);

	//WaitForSingleObject(handle, INFINITE);
	//CloseHandle(handle);

	write_log(JUDGE_INFO,"Judge Task init ok...");

	/* 循环读取消息队列 */
	for(;;)
	{
		/* 放权 */
		Sleep(10);
	}

	closesocket(g_sListen);
	WSACleanup();

	return 0;
}

APP_INFO_S g_judgeAppInfo =
{
	NULL,
	"Judge",
	OJ_Init,
	OJ_TaskEntry
};


void Judge_RegAppInfo()
{
	RegistAppInfo(&g_judgeAppInfo);

}

void Judge_ShowCfgContent()
{
	HANDLE hFile ;
	hFile= CreateFile(STARTUP_CFG, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_READONLY, NULL);
	if (hFile <= 0)
	{
		write_log(JUDGE_ERROR,"CreateFile inFileName(%s) Error:%s", STARTUP_CFG, GetLastError());
	}

	BOOL flag = FALSE;
	while (true)
	{
		char buffer[BUFSIZE] = {0};
		DWORD BytesRead, BytesWritten;
		flag = ReadFile(hFile, buffer, BUFSIZE, &BytesRead, NULL);
		if (!flag || (BytesRead == 0)) break;

		judge_outstring("%s",buffer);

		if (!flag){ break;}
	}

	CloseHandle(hFile);

}

#if 0
int main(int argc, char **argv)
{
	OJ_TaskEntry();
	return 0;
}
#endif



