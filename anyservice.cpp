#undef UNICODE
#undef _UNICODE
#include <stdio.h>
#include <windows.h>
#include <getopt.h>

/* ������ѡ��ö��ֵ */
enum {
    show_help,
    service_remove,
    service_install,
    service_autostart,
    service_stopchild,
    service_norestart,
    service_displayname,
    service_description,
    service_interactive,
    service_dependence,
    service_username,
    service_password,
    service_workdir
};

/* �����в���ֵ */
LPCSTR servicename;     //��������
LPCSTR displayname;     //��ʾ����
LPSTR  description;     //��������
LPCSTR username;        //�û�����
LPCSTR password;        //�û�����
LPCSTR workdir;         //����Ŀ¼
LPCSTR dependency[10];  //��������
char commandline[8192]; //������
bool interactive;       //�Ƿ񽻻�
bool auto_start;        //�Զ�����
bool stop_child;        //ͣ�ӽ���
bool no_restart;        //������

bool service_running;           //�����Ƿ�������
SERVICE_STATUS service_status;        //����״̬
SERVICE_STATUS_HANDLE status_handle;  //״̬���
HANDLE process_handle;                //���̾��
DWORD language_id;                    //���Ա�ʶ

int main_argc;     //main������������
char **main_argv;  //main���������б�

const char *const
usage = "Usage:\n"
        "anyservice --remove  <ServcieName>\n"
        "                     Remove service\n"
        "anyservice --install <ServcieName> [optional] <Application> [Arguments]\n"
        "                     Install service\n"
        "    optional:\n"
        "        --workdir     WorkDir      Working directory of application.\n"
        "        --displayname DisplayName  Display name of the service.\n"
        "        --description Description  Description of the service.\n"
        "        --dependence  Dependence   Dependences of the service.\n"
        "        --username    Username     Username of the service.\n"
        "        --password    Password     Password of the service.\n"
        "        --interactive              Install an interactive service.\n"
        "        --stopchild                Stop subprocess when service stop.\n"
        "        --norestart                Do not restart when process stop.\n"
        "        --auto                     Auto start during system startup.\n";


/* Safely copy two strings. */
size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t srclen = strlen(src);
    size_t cplen = srclen < size ? srclen : size - 1;
    memcpy(dst, src, cplen);
    dst[cplen] = '\0';
    return srclen;
}


/*
   ��ӡϵͳ�����Ĵ�����Ϣ
   ����: ������(0��ʾ���»�ȡ)
   ����ֵ: ������
*/
int print_last_error(int last_error = 0) {
    if (last_error == 0) {
        last_error = GetLastError();
    }

    LPTSTR buffer;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, last_error, language_id, (LPTSTR)&buffer, 0, NULL
    );

    fprintf(stderr, buffer);
    LocalFree(buffer);
    return last_error;
}


/*
   �����в����Ƿ���Ҫ��˫����
   �����հס�˫����ʱ���˫����
   ����ֵ: true(��Ҫ��˫����)
           false(�����˫����)
*/
bool need_quote(const char *str) {
    while (*str) {
        switch (*str++) {
            case '"':
            case ' ':
            case '\t':
                return true;
        }
    }
    return false;
}


/*
   ��б�������Ƿ���Ҫת��
   ����ֵ: true(��Ҫת��)
           false(����ת��)
*/
bool need_escape(const char *str) {
    for (; *str == '\\'; str++);
    return *str == '"' || *str == '\0';
}


/*
   ����������(ת��|��˫����)
   ����:
     dst: OUT, �����Ĳ���
     src: IN,  Ҫ����Ĳ���
   ����ֵ: the end of dst
 */
char *quote_string(char *dst, const char *src) {
    if (!need_quote(src)) {
        while (*src)
            *dst++ = *src++;
        return dst;
    }

    bool escape;
    *dst++ = '"';
    while (*src) {
        switch (*src) {
            case '"':
                *dst++ = '\\';
                *dst++ = *src++;
                break;
            case '\\':
                escape = need_escape(src);
                while (*src == '\\') {
                    if (escape)
                        *dst++ = '\\';
                    *dst++ = *src++;
                }
                break;
            default:
                *dst++ = *src++;
        }
    }

    *dst++ = '"';
    return dst;
}


/*
   ƴ�ӳ����������
   ����ֵ: ������������
*/
char *prepare_cmdline(int argc, char *argv[]) {
    char *ptr = quote_string(commandline, argv[1]);
    for (int i = 2; i < argc; i++) {
        *ptr++ = ' ';
        ptr = quote_string(ptr, argv[i]);
    }
    *ptr = '\0';
    return commandline;
}


/*
   ������ִ���ӳ���
   ����ֵ: true(�ɹ�)
           false(ʧ��)
 */
bool create_process(int argc, char *argv[]) {
    char workdir[MAX_PATH];
    if (argv[0][0] == '.' && argv[0][1] == '\0') {
        strlcpy(workdir, argv[1], MAX_PATH);
        char *ptr = strrchr(workdir, '\\');
        *(ptr ? ptr : workdir) = '\0';
    } //endif
    else {
        strlcpy(workdir, argv[0], MAX_PATH);
    }

    bool result;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    result = CreateProcess(
                 argv[1],
                 prepare_cmdline(argc, argv),
                 NULL, NULL, FALSE,
                 NORMAL_PRIORITY_CLASS, NULL,
                 *workdir ? workdir : NULL,
                 &si, &pi
             );

    if (result) {
        process_handle = pi.hProcess;
        CloseHandle(pi.hThread);
    }
    return result;
}


/* �ȴ���������״̬ */
bool wait_for_status(SC_HANDLE service_handle, DWORD status) {
    if (!QueryServiceStatus(service_handle, &service_status)) {
        print_last_error();
        return false;
    }

    DWORD wait_time;
    DWORD start_tick_count = GetTickCount();
    DWORD old_checkpoint = service_status.dwCheckPoint;

    while (service_status.dwCurrentState == status) {
        wait_time = service_status.dwWaitHint / 10;
        Sleep(wait_time > 10000 ? 10000 :
              (wait_time < 1000 ? 1000 : wait_time));
        if (!QueryServiceStatus(service_handle, &service_status)) {
            print_last_error();
            return false;
        }
        if (service_status.dwCheckPoint > old_checkpoint) {
            old_checkpoint = service_status.dwCheckPoint;
            start_tick_count = GetTickCount();
        } //endif
        else {
            if (GetTickCount() - start_tick_count > service_status.dwWaitHint)
                break;
        }
    }

    return true;
}


/*
   ����ָ���ķ���
   ����ֵ: true(�ɹ�)
           false(ʧ��)
*/
bool start_service(SC_HANDLE service_handle) {
    printf("Starting service.\n");
    if (!StartService(service_handle, 0, NULL)) {
        print_last_error();
        return false;
    }

    if (!wait_for_status(service_handle, SERVICE_START_PENDING))
        return false;

    if (service_status.dwCurrentState == SERVICE_RUNNING) {
        printf("Service started.\n");
        return true;
    } //endif
    else {
        printf("Service not started.\n");
        if (service_status.dwWin32ExitCode)
            print_last_error(service_status.dwWin32ExitCode);
        return false;
    }
}


/*
   ƴ�ӷ��������в���
   ����ֵ: �����Ĳ���
*/
char *prepare_parameter(const char *path, int argc, char *argv[]) {
    char *ptr = quote_string(commandline, path);
    *ptr++ = ' ';
    if (stop_child) {
        ptr = quote_string(ptr, "--stopchild");
        *ptr++ = ' ';
    }

    if (no_restart) {
        ptr = quote_string(ptr, "--norestart");
        *ptr++ = ' ';
    }

    ptr = quote_string(ptr, workdir);
    for (int i = 0; i < argc; i++) {
        *ptr++ = ' ';
        ptr = quote_string(ptr, argv[i]);
    }
    *ptr = '\0';
    return commandline;
}


/* ׼�������ķ��� */
char *prepare_dependencies() {
    int dpn_len = 0;
    for (int i = 0; i < 10 && dependency[i]; i++)
        dpn_len += strlen(dependency[i]) + 1;

    if (dpn_len == 0)
        return NULL;

    char *dependencies = (char *)malloc(dpn_len + 1);
    if (dependencies == NULL)
        return NULL;

    dpn_len = 0;
    for (int i = 0; i < 10 && dependency[i]; i++) {
        strcpy(dependencies + dpn_len, dependency[i]);
        dpn_len += strlen(dependency[i]) + 1;
    }

    dependencies[dpn_len] = '\0';
    return dependencies;
}


/*
   ���ݲ�����װָ������
   ����ֵ: ������(0��ʾ�ɹ�)
*/
int install_service(int argc, char *argv[]) {
    TCHAR path[MAX_PATH];
    if (!GetModuleFileName(NULL, path, MAX_PATH))
        return print_last_error();

    SC_HANDLE scmanger_handle = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scmanger_handle == NULL)
        return print_last_error();

    char *dependencies = prepare_dependencies();
    DWORD start_type = auto_start ? SERVICE_AUTO_START : SERVICE_DEMAND_START;
    DWORD service_type = SERVICE_WIN32_OWN_PROCESS;
    if (interactive) {
        service_type |= SERVICE_INTERACTIVE_PROCESS;
    }

    SC_HANDLE
    service_handle = CreateService(
                         scmanger_handle, servicename,
                         displayname ? displayname : servicename,
                         SERVICE_ALL_ACCESS,
                         service_type, start_type,
                         SERVICE_ERROR_NORMAL,
                         prepare_parameter(path, argc, argv),
                         NULL, NULL,
                         dependencies,
                         username, password
                     );

    free(dependencies);
    if (service_handle == NULL) {
        int last_error = print_last_error();
        CloseServiceHandle(scmanger_handle);
        return last_error;
    }

    if (description) {
        SERVICE_DESCRIPTION sd = { description };
        if (!ChangeServiceConfig2(service_handle, SERVICE_CONFIG_DESCRIPTION, (LPVOID)&sd))
            print_last_error();
    }

    printf("Service \"%s\" installed.\n", servicename);
    if (auto_start) {
        start_service(service_handle);
    }

    CloseServiceHandle(service_handle);
    CloseServiceHandle(scmanger_handle);
    return 0;
}


/*
   ָֹͣ���ķ���
   ����ֵ: true(�ɹ�)
           false(ʧ��)
*/
bool stop_service(SC_HANDLE service_handle) {
    if (!QueryServiceStatus(service_handle, &service_status)) {
        print_last_error();
        return false;
    }

    if (service_status.dwCurrentState != SERVICE_RUNNING)
        return true;

    printf("Stopping service.\n");
    if (!ControlService(service_handle, SERVICE_CONTROL_STOP, &service_status)) {
        print_last_error();
        return false;
    }

    if (!wait_for_status(service_handle, SERVICE_STOP_PENDING))
        return false;

    if (service_status.dwCurrentState == SERVICE_STOPPED) {
        printf("Service stopped.\n");
        return true;
    } //endif
    else {
        printf("Service not stopped.\n");
        return false;
    }
}


/*
   ɾ��ָ������
   ����ֵ: ������(0��ʾ�ɹ�)
*/
int remove_service() {
    SC_HANDLE scmanger_handle = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scmanger_handle == NULL)
        return print_last_error();

    SC_HANDLE service_handle = OpenService(scmanger_handle, servicename, SERVICE_ALL_ACCESS);
    if (service_handle == NULL) {
        int last_error = print_last_error();
        CloseServiceHandle(scmanger_handle);
        return last_error;
    }

    stop_service(service_handle);
    int last_error =
        DeleteService(service_handle) ? 0
        : print_last_error();
    if (last_error == 0) {
        printf("Service \"%s\" removed.\n", servicename);
    }

    CloseServiceHandle(service_handle);
    CloseServiceHandle(scmanger_handle);
    return last_error;
}


/*
   ���� taskkill ��ֹ����
   ����: �Ƿ���ֹ�ӽ���
*/
void terminate_process(bool tree) {
    DWORD process_id = GetProcessId(process_handle);
    if (process_id) {
        sprintf(commandline,
                tree ? "taskkill /F /PID %d /T"
                :      "taskkill /F /PID %d",
                process_id);
        system(commandline);
    }
}


/*
   ������ƺ���
   ����: ������
*/
void WINAPI CtrlHandler(DWORD request) {
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            service_status.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(status_handle, &service_status);
            service_running = false;
            if (process_handle) {
                if (!stop_child) {
                    if (!TerminateProcess(process_handle, 0))
                        terminate_process(false);
                } //endif
                else {
                    terminate_process(true);
                }
            }
    }
}


/*
   ��������������
   ����: argv[0] ������
*/
void WINAPI ServiceMain(DWORD argc, LPTSTR *argv) {
    service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
    service_status.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
    service_status.dwCurrentState = SERVICE_START_PENDING;
    service_status.dwServiceSpecificExitCode = 0;
    service_status.dwWin32ExitCode = 0;
    service_status.dwCheckPoint = 0;
    service_status.dwWaitHint = 0;

    status_handle = RegisterServiceCtrlHandler("", CtrlHandler);
    if (status_handle == NULL)
        return;

    SetServiceStatus(status_handle, &service_status);
    service_running = true;

    while (service_running) {
        if (process_handle == NULL) {
            if (!create_process(main_argc, main_argv)) {
                service_status.dwCurrentState = SERVICE_STOPPED;
                service_status.dwWin32ExitCode = GetLastError();
                SetServiceStatus(status_handle, &service_status);
                return;
            }

            if (service_status.dwCurrentState == SERVICE_START_PENDING) {
                service_status.dwCurrentState = SERVICE_RUNNING;
                SetServiceStatus(status_handle, &service_status);
            }

            if (process_handle) {
                WaitForSingleObject(process_handle, INFINITE);
                CloseHandle(process_handle);
                process_handle = NULL;
                if (no_restart) {
                    service_running = false;
                }
            }
        }
        if (service_running) {
            Sleep(5000);
        }
    }

    service_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(status_handle, &service_status);
}


int main(int argc, char *argv[]) {
    static struct option long_option[] = {
        "help", no_argument, NULL, show_help,
        "remove", required_argument, NULL, service_remove,
        "install", required_argument, NULL, service_install,
        "displayname", required_argument, NULL, service_displayname,
        "description", required_argument, NULL, service_description,
        "dependence", required_argument, NULL, service_dependence,
        "interactive", no_argument, NULL, service_interactive,
        "username", required_argument, NULL, service_username,
        "password", required_argument, NULL, service_password,
        "workdir", required_argument, NULL, service_workdir,
        "stopchild", no_argument, NULL, service_stopchild,
        "norestart", no_argument, NULL, service_norestart,
        "auto", no_argument, NULL, service_autostart,
        NULL, 0, NULL, 0,
    };

    opterr = 1;
    workdir = ".";
    int dpn_cnt = 0;
    int opt, cmd = 0;

    while ((opt = getopt_long_only(argc, argv, "+", long_option, NULL)) != -1) {
        switch (opt) {
            case show_help:
                printf(usage);
                return 0;

            case service_install:
            case service_remove:
                servicename = optarg;
                cmd = opt;
                break;

            case service_displayname:
                displayname = optarg;
                break;

            case service_description:
                description = optarg;
                break;

            case service_dependence:
                if (dpn_cnt < 10)
                    dependency[dpn_cnt++] = optarg;
                break;

            case service_stopchild:
                stop_child = true;
                break;

            case service_norestart:
                no_restart = true;
                break;

            case service_autostart:
                auto_start = true;
                break;

            case service_interactive:
                interactive = true;
                break;

            case service_username:
                username = optarg;
                break;

            case service_password:
                password = optarg;
                break;

            case service_workdir:
                workdir = optarg;
                break;

            default:
                printf(usage);
                return 1;
        }
    }

    switch (cmd) {
        case service_remove:
            language_id = SetThreadUILanguage(0);
            return remove_service();

        case service_install:
            if (optind >= argc) {
                printf("Missing application name.\n");
                printf(usage);
                return 1;
            }
            language_id = SetThreadUILanguage(0);
            return install_service(argc - optind, argv + optind);
    }

    main_argc = argc - optind;
    main_argv = argv + optind;
    SERVICE_TABLE_ENTRY service_table[2];
    service_table[0].lpServiceName = (char *)"";
    service_table[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain;
    service_table[1].lpServiceName = NULL;
    service_table[1].lpServiceProc = NULL;

    if (!StartServiceCtrlDispatcher(service_table)) {
        int last_error = GetLastError();
        if (last_error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            //ERROR_FAILED_SERVICE_CONTROLLER_CONNECT: ������ģʽ
            printf(usage);
            return 0;
        }
        print_last_error(last_error);
        return last_error;
    }

    return 0;
}
